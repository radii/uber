/* main.c
 *
 * Copyright (C) 2010 Christian Hergert <chris@dronelabs.com>
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef GETTEXT_PACKAGE
#define GETTEXT_PACKAGE "UberGraph"
#endif

#ifndef LOCALE_DIR
#define LOCALE_DIR "/usr/share/locale"
#endif

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/sysinfo.h>
#include <signal.h>
#include <sys/time.h>
#include <errno.h>
#include <linux/blktrace_api.h>

#include "uber-graph.h"
#include "uber-label.h"
#include "uber-buffer.h"
#include "uber-heat-map.h"

#ifdef DISABLE_DEBUG
#define DEBUG(f,...)
#else
#define DEBUG(f,...) g_debug(f, ## __VA_ARGS__)
#endif

typedef struct
{
	volatile gdouble swapFree;
	volatile gdouble memFree;
} MemInfo;

typedef struct
{
	volatile gdouble  cpuUsage;  /* Total cpu */
	volatile gdouble *cpusUsage; /* Per cpu */
} CpuInfo;

typedef struct
{
	volatile gdouble bytesIn;
	volatile gdouble bytesOut;
} NetInfo;

typedef struct
{
	volatile gdouble load5;
	volatile gdouble load10;
	volatile gdouble load15;
} LoadInfo;

typedef struct
{
	volatile gdouble size;
	volatile gdouble resident;
} PmemInfo;

typedef struct
{
	volatile gdouble vruntime;
} SchedInfo;

typedef struct
{
	volatile gint n_threads;
} ThreadInfo;

typedef struct
{
	volatile GAsyncQueue* q;
} IoLatInfo;

struct io_list {
	struct blk_io_trace *t;
	struct io_list *next;
};

typedef unsigned int u32;

static MemInfo    mem_info   = { 0 };
static CpuInfo    cpu_info   = { 0 };
static NetInfo    net_info   = { 0 };
static LoadInfo   load_info  = { 0 };
static PmemInfo   pmem_info  = { 0 };
static SchedInfo  sched_info = { 0 };
static ThreadInfo thread_info= { 0 };
static IoLatInfo  iolat_info = { 0 };
static GtkWidget *load_graph = NULL;
static GtkWidget *cpu_graph  = NULL;
static GtkWidget *cpu_label_hbox = NULL;
static GtkWidget *net_label_hbox = NULL;
static GtkWidget *mem_label_hbox = NULL;
static GtkWidget *load_label_hbox = NULL;
static GtkWidget *net_graph  = NULL;
static GtkWidget *mem_graph  = NULL;
static gboolean   reaped     = FALSE;
static GtkWidget *vbox       = NULL;
static GtkWidget *pmem_graph = NULL;
static GtkWidget *sched_graph  = NULL;
static GtkWidget *thread_graph = NULL;
static GPid       pid        = 0;
static GPtrArray *labels     = NULL;
static struct io_list *iolist;

static const gchar* cpu_colors[] = {
	"#73d216",
	"#f57900",
	"#3465a4",
	"#ef2929",
	"#75507b",
	"#ce5c00",
	"#c17d11",
	"#ce5c00",
};

static gboolean
get_cpu (UberGraph *graph,
         gint       line,
         gdouble   *value,
         gpointer   user_data)
{
	UberLabel *label;
	gint i = line - 1;
	gchar *str;

#if 0
	if (line == 1) {
		*value = cpu_info.cpuUsage;
	} else {
		*value = cpu_info.cpusUsage[line - 2];
	}
#endif

	*value = cpu_info.cpusUsage[i];
	str = g_strdup_printf("CPU%d  %.1f%%", i + 1, cpu_info.cpusUsage[i]);
	label = g_ptr_array_index(labels, i);
	uber_label_set_text(label, str);
	g_free(str);
	return TRUE;
}

static gboolean
get_mem (UberGraph *graph,
         gint       line,
         gdouble   *value,
         gpointer   user_data)
{
	switch (line) {
	case 1:
		*value = mem_info.memFree;
		break;
	case 2:
		*value = mem_info.swapFree;
		break;
	default:
		g_assert_not_reached();
	}
	return TRUE;
}

static gboolean
get_load (UberGraph *graph,
          gint       line,
          gdouble   *value,
          gpointer   user_data)
{
	switch (line) {
	case 1:
		*value = load_info.load5;
		break;
	case 2:
		*value = load_info.load10;
		break;
	case 3:
		*value = load_info.load15;
		break;
	default:
		g_assert_not_reached();
	}
	return TRUE;
}

static gboolean
get_net (UberGraph *graph,
         gint       line,
         gdouble   *value,
         gpointer   user_data)
{
	switch (line) {
	case 1:
		*value = net_info.bytesIn;
		break;
	case 2:
		*value = net_info.bytesOut;
		break;
	default:
		g_assert_not_reached();
	}
	return TRUE;
}

static gboolean
get_threads (UberGraph *graph,
             gint       line,
             gdouble   *value,
             gpointer   user_data)
{
	switch (line) {
	case 1:
		*value = thread_info.n_threads;
		break;
	default:
		g_assert_not_reached();
	}
	return TRUE;
}

static gboolean
get_iolat (UberHeatMap  *map,
           GArray      **values,
           gpointer      user_data)
{
	GArray *v, *sum;
	
	sum = g_array_sized_new(FALSE, TRUE, sizeof(gint), 1 /* map->nbucket */);

	while((v = g_async_queue_try_pop((GAsyncQueue *)iolat_info.q)) != NULL) {
		if (g_async_queue_length((GAsyncQueue *)iolat_info.q) == 0)
			break;
		g_array_unref(v);
	}
	*values = sum;
	return !!v;
}

static gboolean
button_pressed (GtkWidget      *graph,
                GdkEventButton *button,
                gpointer        user_data)
{
	gboolean show_cpu = FALSE;
	gboolean show_mem = FALSE;
	gboolean show_load = FALSE;
	gboolean show_net = FALSE;

	if (button->button != 1) {
		return FALSE;
	}
	if (button->type != GDK_BUTTON_PRESS) {
		return FALSE;
	}
	if (graph == cpu_graph) {
		show_cpu = !gtk_widget_get_visible(cpu_label_hbox);
	} else if (graph == net_graph) {
		show_net = !gtk_widget_get_visible(net_label_hbox);
	} else if (graph == mem_graph) {
		show_mem = !gtk_widget_get_visible(mem_label_hbox);
	} else if (graph == load_graph) {
		show_load = !gtk_widget_get_visible(load_label_hbox);
	}
	gtk_widget_set_visible(cpu_label_hbox, show_cpu);
	gtk_widget_set_visible(mem_label_hbox, show_mem);
	gtk_widget_set_visible(net_label_hbox, show_net);
	gtk_widget_set_visible(load_label_hbox, show_load);
	uber_graph_set_show_xlabel(UBER_GRAPH(load_graph), show_load);
	uber_graph_set_show_xlabel(UBER_GRAPH(net_graph), show_net);
	uber_graph_set_show_xlabel(UBER_GRAPH(cpu_graph), show_cpu);
	return FALSE;
}

static inline GtkWidget*
create_graph (void)
{
	GtkWidget *graph;
	//GtkWidget *align;

	graph = uber_graph_new();
	//align = gtk_alignment_new(.5, .5, 1., 1.);
	//gtk_alignment_set_padding(GTK_ALIGNMENT(align), 0, 0, 6, 0);
	//gtk_container_add(GTK_CONTAINER(align), graph);
	//gtk_box_pack_start(GTK_BOX(vbox), align, TRUE, TRUE, 0);
	//gtk_widget_show(align);
	gtk_widget_set_events(graph, GDK_BUTTON_PRESS_MASK);
	g_signal_connect(graph,
	                 "button-press-event",
	                 G_CALLBACK(button_pressed),
	                 NULL);
	gtk_widget_show(graph);
	return graph;
}

static inline GtkWidget*
add_label (GtkWidget   *hbox,
           const gchar *title,
           const gchar *color)
{
	GtkWidget *label;
	GdkColor gcolor;

	gdk_color_parse(color, &gcolor);
	label = uber_label_new();
	uber_label_set_text(UBER_LABEL(label), title);
	uber_label_set_color(UBER_LABEL(label), &gcolor);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, TRUE, 0);
	gtk_widget_show(label);
	return label;
}

static void
next_load (void)
{
	gdouble load5;
	gdouble load10;
	gdouble load15;
	gchar buf[1024];
	gint fd;

	fd = open("/proc/loadavg", O_RDONLY);
	read(fd, buf, sizeof(buf));
	close(fd);

	if (sscanf(buf, "%lf %lf %lf", &load5, &load10, &load15) == 3) {
		load_info.load5 = load5;
		load_info.load10 = load10;
		load_info.load15 = load15;
	}
}

static void
next_cpu (void)
{
	static gboolean initialized = FALSE;
	static gfloat u1, n1, s1, i1;
	static gfloat *_u1, *_n1, *_s1, *_i1;
	gfloat u2, n2, s2, i2;
	gfloat u3, n3, s3, i3;
	gdouble total;
	int fd;
	char buf[4096];
	char *line;
	gint i;
	gint cpu;

	if (!initialized) {
		cpu_info.cpusUsage = g_new0(gdouble, get_nprocs());
		_u1 = g_new0(gfloat, get_nprocs());
		_n1 = g_new0(gfloat, get_nprocs());
		_s1 = g_new0(gfloat, get_nprocs());
		_i1 = g_new0(gfloat, get_nprocs());
	}

	fd = open("/proc/stat", O_RDONLY);
	i = read(fd, buf, sizeof(buf));
	buf[i - 1] = '\0';
	line = buf;
	for (i = 0; buf[i]; i++) {
		if (buf[i] == '\n') {
			buf[i] = '\0';
			if (g_str_has_prefix(line, "cpu ")) {
				if (sscanf(line, "cpu %f %f %f %f", &u2, &n2, &s2, &i2) != 4) {
					g_warning("Failed to read total cpu line.");
					break;
				} else {
					u3 = (u2 - u1);
					n3 = (n2 - n1);
					s3 = (s2 - s1);
					i3 = (i2 - i1);
					total = (u3 + n3 + s3 + i3);
					if (initialized) {
						if (total != 0.) {
							cpu_info.cpuUsage = (100 * (u3 + n3 + s3)) / total;
						}
					}
					u1 = u2;
					i1 = i2;
					s1 = s2;
					n1 = n2;
				}
			} else if (strncmp(line, "cpu", 3) == 0) {
				line += 3;
				cpu = strtoll(line, &line, 10);
				if (sscanf(line, "%f %f %f %f", &u2, &n2, &s2, &i2) != 4) {
					g_warning("Failed to read cpu %d line.", cpu);
					break;
				} else {
					u3 = (u2 - _u1[cpu]);
					n3 = (n2 - _n1[cpu]);
					s3 = (s2 - _s1[cpu]);
					i3 = (i2 - _i1[cpu]);
					total = (u3 + n3 + s3 + i3);
					if (initialized) {
						if (total == 0.) {
							cpu_info.cpusUsage[cpu] = 0.;
						} else {
							cpu_info.cpusUsage[cpu] = (100 * (u3 + n3 + s3)) / total;
						}
					}
					_u1[cpu] = u2;
					_i1[cpu] = i2;
					_s1[cpu] = s2;
					_n1[cpu] = n2;
				}
			}
			line = &buf[++i];
		}
	}

	initialized = TRUE;

  	close(fd);
}

static void
next_net (void)
{
	static gboolean initialized = FALSE;
	static gdouble lastTotalIn = 0;
	static gdouble lastTotalOut = 0;
	char buf[4096];
	char iface[32];
	char *line;
	int fd;
	int i;
	int l = 0;
	gulong dummy;
	gulong bytesIn = 0;
	gulong bytesOut = 0;
	gdouble totalIn = 0;
	gdouble totalOut = 0;

	if ((fd = open("/proc/net/dev", O_RDONLY)) < 0) {
		g_warning("Failed to open /proc/net/dev");
		g_assert_not_reached();
	}

	memset(buf, 0, sizeof(buf));
	read(fd, buf, sizeof(buf));
	buf[sizeof(buf) - 1] = '\0';
	line = buf;
	for (i = 0; buf[i]; i++) {
		if (buf[i] == ':') {
			buf[i] = ' ';
		} else if (buf[i] == '\n') {
			buf[i] = '\0';
			if (++l > 2) { // ignore first two lines
				if (sscanf(line, "%s %lu %lu %lu %lu %lu %lu %lu %lu %lu",
				           iface, &bytesIn, &dummy, &dummy, &dummy, &dummy,
					   &dummy, &dummy, &dummy, &bytesOut) != 10) {
					g_warning("Skipping invalid line: %s", line);
				} else if (g_strcmp0(iface, "lo") != 0) {
					totalIn += bytesIn;
					totalOut += bytesOut;
				}
				line = NULL;
			}
			line = &buf[++i];
		}
	}

	if (!initialized) {
		initialized = TRUE;
		goto finish;
	}

	net_info.bytesIn = (totalIn - lastTotalIn);
	net_info.bytesOut = (totalOut - lastTotalOut);

  finish:
	close(fd);
	lastTotalOut = totalOut;
	lastTotalIn = totalIn;
}

static void
next_mem (void)
{
	static gboolean initialized = FALSE;

	gdouble memTotal = 0;
	gdouble memFree = 0;
	gdouble swapTotal = 0;
	gdouble swapFree = 0;
	gdouble cached = 0;
	int fd;
	char buf[4096];
	char *line;
	int i;


	if ((fd = open("/proc/meminfo", O_RDONLY)) < 0) {
		g_warning("Failed to open /proc/meminfo");
		return;
	}

	read(fd, buf, sizeof(buf));
	buf[sizeof(buf) - 1] = '\0';
	line = buf;

	for (i = 0; buf[i]; i++) {
		if (buf[i] == '\n') {
			buf[i] = '\0';
			if (g_str_has_prefix(line, "MemTotal:")) {
				if (sscanf(line, "MemTotal: %lf", &memTotal) != 1) {
					g_warning("Failed to read MemTotal");
					goto error;
				}
			} else if (g_str_has_prefix(line, "MemFree:")) {
				if (sscanf(line, "MemFree: %lf", &memFree) != 1) {
					g_warning("Failed to read MemFree");
					goto error;
				}
			} else if (g_str_has_prefix(line, "SwapTotal:")) {
				if (sscanf(line, "SwapTotal: %lf", &swapTotal) != 1) {
					g_warning("Failed to read SwapTotal");
					goto error;
				}
			} else if (g_str_has_prefix(line, "SwapFree:")) {
				if (sscanf(line, "SwapFree: %lf", &swapFree) != 1) {
					g_warning("Failed to read SwapFree");
					goto error;
				}
			} else if (g_str_has_prefix(line, "Cached:")) {
				if (sscanf(line, "Cached: %lf", &cached) != 1) {
					g_warning("Failed to read Cached");
					goto error;
				}
			}
			line = &buf[i + 1];
		}
	}

	if (!initialized) {
		initialized = TRUE;
		goto finish;
	}

	mem_info.memFree = (memTotal - cached - memFree) / memTotal;
	mem_info.swapFree = (swapTotal - swapFree) / swapTotal;

  finish:
  error:
  	close(fd);
}

static void
next_pmem (void)
{
	static char *path = NULL;
	int fd;
	char buf[1024];
	long size = 0;
	long resident = 0;

	if (G_UNLIKELY(!path)) {
		path = g_strdup_printf("/proc/%d/statm", pid);
	}

	fd = open(path, O_RDONLY);
	read(fd, buf, sizeof(buf));
	sscanf(buf, "%ld %ld", &size, &resident);
	pmem_info.size = size;
	pmem_info.resident = resident;
	close(fd);
}

static void
next_sched (void)
{
	static char *path = NULL;
	static gdouble last_vruntime = 0;
	gdouble vruntime = 0;
	int fd;
	char buf[4096];
	char name[128];
	char *line;
	gint i;

	if (G_UNLIKELY(!path)) {
		path = g_strdup_printf("/proc/%d/sched", pid);
	}

	fd = open(path, O_RDONLY);
	memset(buf, 0, sizeof(buf));
	read(fd, buf, sizeof(buf));
	buf[sizeof(buf) - 1] = '\0';
	line = buf;
	for (i = 0; buf[i]; i++) {
		if (buf[i] == '\n') {
			buf[i] = '\0';
			if (g_str_has_prefix(line, "se.vruntime")) {
				if (sscanf(line, "%s : %lf", name, &vruntime) != 2) {
					g_printerr("Failed to parse vruntime.\n");
					break;
				}
				sched_info.vruntime = (vruntime - last_vruntime);
				break;
			}
			line = &buf[++i];
		}
	}
	close(fd);
	last_vruntime = vruntime;
}

static void
next_threads (void)
{
	static gchar *path = NULL;
	gint n_threads = 0;
	GDir *dir;

	if (G_UNLIKELY(!path)) {
		path = g_strdup_printf("/proc/%d/task", pid);
	}

	if (!(dir = g_dir_open(path, 0, NULL))) {
		return;
	}
	while (g_dir_read_name(dir)) {
		n_threads++;
	}
	g_dir_close(dir);
	thread_info.n_threads = n_threads;
}

static int	blktrace_fd = -1;
static GPid	blktrace_pid;

static void
blktrace_exited (GPid     pid,
                 gint     status,
                 gpointer data)
{
	g_printerr("blktrace exited.\n");
	blktrace_fd = -1;
}
static void die(const char *fmt, ...) __attribute__((format(gnu_printf, 1, 2), noreturn));

static void
die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(1);
}

static void
setup_blktrace(void)
{
	const gchar *argv[] = { "sudo", "/usr/sbin/blktrace", "-o-", "/dev/sda", NULL };
	gchar **args;
	GError *error = NULL;
	int i, flags;

	args = g_new0(gchar*, G_N_ELEMENTS(argv));
	for (i = 0; i < G_N_ELEMENTS(argv); i++)
		args[i] = g_strdup(argv[i]);

	if (!g_spawn_async_with_pipes(NULL, args, NULL,
				      G_SPAWN_SEARCH_PATH,
				      NULL, NULL, &blktrace_pid,
				      NULL, &blktrace_fd, NULL,
				      &error)) {
		g_printerr("%s\n", error->message);
		g_clear_error(&error);
		return;
	}
	g_child_watch_add(pid, blktrace_exited, NULL);
	if ((flags = fcntl(blktrace_fd, F_GETFL, 0)) == -1)
		die("F_GETFL: %s\n", strerror(errno));
	flags |= O_NONBLOCK;
	if (fcntl(blktrace_fd, F_SETFL, flags) == -1)
		die("F_SETFL: %s\n", strerror(errno));
	g_print("blktrace set up on fd %d\n", blktrace_fd);
}

static void
setup_iolats(void)
{
	setup_blktrace();
	iolat_info.q = g_async_queue_new_full(0);
}

static inline int tvdiff(const struct timeval a, const struct timeval b)
{
	return (b.tv_sec - a.tv_sec) * 1000000 + (b.tv_usec - a.tv_usec);
}

int
buffered_read(int fd, char *dest, int sz)
{
#define BUFSZ 1024
	static int last_fd, head, tail;
	static char buf[BUFSZ];
	char *p = dest;
	int a, n = sz;
	int nbuf;
	int ret = -1;

	/* if we have stuff buffered for another caller, bail. */
	if (fd != last_fd && head != tail)
		return read(fd, dest, sz);

	nbuf = head - tail;

#define min(a,b) ((a)<(b)?(a):(b))

	/* if we have buffered data, copy as much as will fit */
	if (nbuf > 0) {
		a = min(n, nbuf);
		memcpy(p, buf + tail, a);
		p += a;
		tail += a;
		n -= a;
		ret = a;
	}

	/* if we didn't satisfy the reader, get another buffer */
	if (n > 0) {
		g_assert(head == tail);

		if (n >= BUFSZ)
			return read(fd, p, n);

		if ((a = read(fd, buf, BUFSZ)) == -1)
			goto out;
		if (a == 0) {
			/* EOF!  If we copied out buffered data, out: will
			 * return its len; otherwise we need to return 0.
			 */
			ret = 0;
			goto out;
		}
		last_fd = fd;
		head = a;
		tail = min(n, a);
		memcpy(p, buf, tail);
		p += tail;
	}
out:
	/*
	 * if we gave the user any data, then return that length; otherwise,
	 * return error (or EOF).
	 */
	return (p - dest) ?: ret;
}

void hexdump(FILE *f, void *p, int n)
{
	int i;
	unsigned char *x = p;

	for(i=0; i<n; i++)
		fprintf(f, "%02x%s", x[i],
			(i%16 == 15 || i==n-1) ? "\n":i%8==7?"  ":" ");
}

static int
read_blktrace(int fd, struct blk_io_trace *t)
{
	static struct blk_io_trace b;
	static unsigned int n;
	static void *buf;
	static int buflen;
	static int numblk;
	void *p;
	int c;

	p = (char *)&b + n;
	c = buffered_read(fd, p, sizeof(b) - n);

	if (c == -1) {
		if (errno != EAGAIN)
			fprintf(stderr, "read(%d): %s\n", fd, strerror(errno));
		return 0;
	}
	n += c;
	if (n < sizeof(b))
		return 0;

	numblk++;
	if (b.magic != (BLK_IO_TRACE_MAGIC | BLK_IO_TRACE_VERSION)) {
		fprintf(stderr, "wrong magic! record %d buffer =\n", numblk);
		hexdump(stderr, &b, sizeof(b));
		exit(1);
	}

	if (b.pdu_len > 0) {
		if (b.pdu_len > buflen) {
			buflen = b.pdu_len;
			p = realloc(buf, buflen);
			if (!p)
				die("unable to allocate %d bytes\n", buflen);
			buf = p;
		}
		// XXX this should escape back out to the poller
		for (p = buf, c = b.pdu_len; c > 0; ) {
			int a = buffered_read(fd, p, c);

			if (a < 0) continue;
			c -= a;
			p += a;
		}
	}
	*t = b;
	n = 0;
	return 1;
}

static int io_list_len(void)
{
	int i;
	struct io_list *p;

	for(i=0, p=iolist; p; i++, p=p->next)
		;
	return i;
}

static struct blk_io_trace *
find_io(struct blk_io_trace t)
{
	struct io_list *p, **prevp = &iolist;
	struct blk_io_trace *r;

	for (p = iolist; p; p = p->next) {
		if(p->t->sector == t.sector) {
			*prevp = p->next;
			r = p->t;
			g_free(p);
			return r;
		}
		prevp = &p->next;
	}
	return 0;
}

static void
stash_io(struct blk_io_trace t)
{
	struct blk_io_trace *p = g_new(struct blk_io_trace, 1);
	struct io_list *n = g_new(struct io_list, 1);

	*p = t;
	n->t = p;
	n->next = iolist;
	iolist = n;
}

static void
next_iolats (void)
{
	struct blk_io_trace t, *p;
	int i, n = 0, x, td;
	GArray *vals;
	struct timeval tv1, tv2;

	if (blktrace_fd == -1) return;

	gettimeofday(&tv1, 0);
	vals = g_array_new(FALSE, FALSE, sizeof(gint));
	g_array_set_size(vals, 0);

	while (read_blktrace(blktrace_fd, &t)) {
		n++;
		if (0)
		printf("%-4d 0x%08x %5d 0x%08x %lld %5d %d@%d\n",
				n, (unsigned int)t.magic, (int)t.sequence,
				(unsigned int)t.action,
				(long long)t.time, (int)t.pid,
				(int)t.bytes, (int)t.sector);
		switch (t.action & 0xffff) {
		case __BLK_TA_COMPLETE:
			p = find_io(t);
			if (!p) {
				fprintf(stderr, "seq %d not found!\n", t.sequence);
				break;
			}
			x = t.time - p->time;
			g_array_append_val(vals, x);
			g_free(p);
			break;
		case __BLK_TA_ISSUE:
			stash_io(t);
			break;
		case __BLK_TA_QUEUE:
		case __BLK_TA_BACKMERGE:
		case __BLK_TA_FRONTMERGE:
		case __BLK_TA_GETRQ:
		case __BLK_TA_SLEEPRQ:
		case __BLK_TA_REQUEUE:
		case __BLK_TA_PLUG:
		case __BLK_TA_UNPLUG_IO:
		case __BLK_TA_UNPLUG_TIMER:
		case __BLK_TA_INSERT:
		case __BLK_TA_SPLIT:
		case __BLK_TA_BOUNCE:
		case __BLK_TA_REMAP:
		case __BLK_TA_ABORT:
		case __BLK_TA_DRV_DATA:
		default:
			break;
		}
	}
	gettimeofday(&tv2, 0);
	td = tvdiff(tv1, tv2);
	g_print("next_iolats %d records %d us %.2f us/record, %d completions, %d outstanding ",
			n, td, td * 1. / (n?:1), (int)vals->len, io_list_len());
	for (i=0; i<vals->len; i++)
		printf("%d ", (int)g_array_index(vals, gint, i) / 1000);
	printf("\n");
	g_async_queue_push((GAsyncQueue*)iolat_info.q, vals);
}

static inline GtkWidget*
new_label_container (void)
{
	GtkWidget *align;
	GtkWidget *hbox;

	align = gtk_alignment_new(.5, .5, 1., 1.);
	gtk_alignment_set_padding(GTK_ALIGNMENT(align), 0, 0, 83, 0);
	hbox = gtk_hbox_new(TRUE, 0);
	gtk_container_add(GTK_CONTAINER(align), hbox);
	//gtk_box_pack_start(GTK_BOX(vbox), align, FALSE, TRUE, 0);
	gtk_widget_show(align);
	return hbox;
}

static GtkWidget*
create_main_window (void)
{
	GtkWidget *window;
	GtkWidget *load_label;
	GtkWidget *cpu_label;
	GtkWidget *net_label;
	GtkWidget *mem_label;
	GtkWidget *hbox;
	GtkWidget *label;
	GtkWidget *group;
#if 1
	GtkWidget *heat;
	GtkWidget *heat2;
#endif
	UberRange cpu_range = { 0., 100., 100. };
	gint i;

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_container_set_border_width(GTK_CONTAINER(window), 12);
	gtk_window_set_title(GTK_WINDOW(window), _("UberGraph"));
	gtk_window_set_default_size(GTK_WINDOW(window), 640, 480);
	gtk_widget_show(window);

	vbox = gtk_vbox_new(TRUE, 6);
	gtk_container_add(GTK_CONTAINER(window), vbox);
	gtk_widget_show(vbox);

	group = gtk_vbox_new(FALSE, 3);
	hbox = gtk_hbox_new(FALSE, 3);
	gtk_box_pack_start(GTK_BOX(vbox), group, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(group), hbox, TRUE, TRUE, 0);
	gtk_widget_show(hbox);
	gtk_widget_show(group);

	cpu_label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(cpu_label), "<b>CPU</b>");
	gtk_box_pack_start(GTK_BOX(hbox), cpu_label, FALSE, FALSE, 0);
	gtk_misc_set_alignment(GTK_MISC(cpu_label), .5, .5);
	g_object_set(cpu_label, "angle", 90., NULL);
	gtk_widget_show(cpu_label);

	#define SET_LINE_COLOR(g, n, c) \
		G_STMT_START { \
			GdkColor gc; \
			gdk_color_parse(c, &gc); \
			uber_graph_set_line_color(UBER_GRAPH(g), n, &gc); \
		} G_STMT_END

	cpu_graph = create_graph();
	gtk_box_pack_start(GTK_BOX(hbox), cpu_graph, TRUE, TRUE, 0);
	uber_graph_set_show_xlabel(UBER_GRAPH(cpu_graph), TRUE);
	uber_graph_set_format(UBER_GRAPH(cpu_graph), UBER_GRAPH_PERCENT);
	uber_graph_set_yautoscale(UBER_GRAPH(cpu_graph), FALSE);
	uber_graph_set_yrange(UBER_GRAPH(cpu_graph), &cpu_range);
	//uber_graph_add_line(UBER_GRAPH(cpu_graph));
	//SET_LINE_COLOR(cpu_graph, 1, "#2e3436");
	uber_graph_set_value_func(UBER_GRAPH(cpu_graph), get_cpu, NULL, NULL);

	hbox = new_label_container();
	gtk_box_pack_start(GTK_BOX(group), gtk_widget_get_parent(hbox), FALSE, TRUE, 0);
	//add_label(hbox, "Total CPU", "#2e3436");
	for (i = 1; i <= get_nprocs(); i++) {
		char *text = g_strdup_printf("CPU%d", i);

		uber_graph_add_line(UBER_GRAPH(cpu_graph));
		SET_LINE_COLOR(cpu_graph, i, (gchar *)cpu_colors[(i-1) % G_N_ELEMENTS(cpu_colors)]);
		label = add_label(hbox, text, (gchar *)cpu_colors[(i-1) % G_N_ELEMENTS(cpu_colors)]);
		uber_label_bind_graph(UBER_LABEL(label), UBER_GRAPH(cpu_graph), i);
		g_ptr_array_add(labels, label);
		g_free(text);
	}
	gtk_widget_show(hbox);
	cpu_label_hbox = hbox;

	group = gtk_vbox_new(FALSE, 3);
	hbox = gtk_hbox_new(FALSE, 3);
	gtk_box_pack_start(GTK_BOX(group), hbox, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), group, TRUE, TRUE, 0);
	gtk_widget_show(hbox);
	gtk_widget_show(group);

	load_label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(load_label), "<b>Load</b>");
	g_object_set(load_label, "angle", 90., NULL);
	gtk_box_pack_start(GTK_BOX(hbox), load_label, FALSE, TRUE, 0);
	gtk_misc_set_alignment(GTK_MISC(load_label), .0, .5);
	gtk_widget_show(load_label);

	load_graph = create_graph();
	gtk_box_pack_start(GTK_BOX(hbox), load_graph, TRUE, TRUE, 0);
	uber_graph_set_yautoscale(UBER_GRAPH(load_graph), TRUE);
	uber_graph_add_line(UBER_GRAPH(load_graph));
	uber_graph_add_line(UBER_GRAPH(load_graph));
	uber_graph_add_line(UBER_GRAPH(load_graph));
	SET_LINE_COLOR(load_graph, 1, "#4e9a06");
	SET_LINE_COLOR(load_graph, 2, "#f57900");
	SET_LINE_COLOR(load_graph, 3, "#cc0000");
	uber_graph_set_value_func(UBER_GRAPH(load_graph), get_load, NULL, NULL);

	hbox = new_label_container();
	gtk_box_pack_start(GTK_BOX(group), gtk_widget_get_parent(hbox), FALSE, TRUE, 0);
	label = add_label(hbox, "5 Minute Average", "#4e9a06");
	uber_label_bind_graph(UBER_LABEL(label), UBER_GRAPH(load_graph), 1);
	label = add_label(hbox, "10 Minute Average", "#f57900");
	uber_label_bind_graph(UBER_LABEL(label), UBER_GRAPH(load_graph), 2);
	label = add_label(hbox, "15 Minute Average", "#cc0000");
	uber_label_bind_graph(UBER_LABEL(label), UBER_GRAPH(load_graph), 3);
	load_label_hbox = hbox;

	group = gtk_vbox_new(FALSE, 3);
	hbox = gtk_hbox_new(FALSE, 3);
	gtk_box_pack_start(GTK_BOX(group), hbox, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), group, TRUE, TRUE, 0);
	gtk_widget_show(hbox);
	gtk_widget_show(group);

	net_label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(net_label), "<b>Network</b>");
	g_object_set(net_label, "angle", 90., NULL);
	gtk_box_pack_start(GTK_BOX(hbox), net_label, FALSE, TRUE, 0);
	gtk_misc_set_alignment(GTK_MISC(net_label), .0, .5);
	gtk_widget_show(net_label);

	net_graph = create_graph();
	gtk_box_pack_start(GTK_BOX(hbox), net_graph, TRUE, TRUE, 0);
	uber_graph_set_format(UBER_GRAPH(net_graph), UBER_GRAPH_DIRECT1024);
	uber_graph_set_yautoscale(UBER_GRAPH(net_graph), TRUE);
	uber_graph_add_line(UBER_GRAPH(net_graph));
	uber_graph_add_line(UBER_GRAPH(net_graph));
	SET_LINE_COLOR(net_graph, 1, "#a40000");
	SET_LINE_COLOR(net_graph, 2, "#4e9a06");
	uber_graph_set_value_func(UBER_GRAPH(net_graph), get_net, NULL, NULL);

	hbox = new_label_container();
	gtk_box_pack_start(GTK_BOX(group), gtk_widget_get_parent(hbox), FALSE, TRUE, 0);
	label = add_label(hbox, "Bytes In", "#a40000");
	uber_label_bind_graph(UBER_LABEL(label), UBER_GRAPH(net_graph), 1);
	label = add_label(hbox, "Bytes Out", "#4e9a06");
	uber_label_bind_graph(UBER_LABEL(label), UBER_GRAPH(net_graph), 2);
	net_label_hbox = hbox;

	group = gtk_vbox_new(FALSE, 3);
	hbox = gtk_hbox_new(FALSE, 3);
	gtk_box_pack_start(GTK_BOX(group), hbox, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), group, TRUE, TRUE, 0);
	gtk_widget_show(group);
	gtk_widget_show(hbox);

	mem_label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(mem_label), "<b>Memory</b>");
	g_object_set(mem_label, "angle", 90., NULL);
	gtk_box_pack_start(GTK_BOX(hbox), mem_label, FALSE, TRUE, 0);
	gtk_misc_set_alignment(GTK_MISC(mem_label), .0, .5);
	gtk_widget_show(mem_label);

	mem_graph = create_graph();
	uber_graph_set_show_xlabel(UBER_GRAPH(mem_graph), TRUE);
	gtk_box_pack_start(GTK_BOX(hbox), mem_graph, TRUE, TRUE, 0);
	uber_graph_set_format(UBER_GRAPH(mem_graph), UBER_GRAPH_PERCENT);
	uber_graph_set_yautoscale(UBER_GRAPH(mem_graph), FALSE);
	uber_graph_add_line(UBER_GRAPH(mem_graph));
	uber_graph_add_line(UBER_GRAPH(mem_graph));
	SET_LINE_COLOR(mem_graph, 1, "#3465a4");
	SET_LINE_COLOR(mem_graph, 2, "#8ae234");
	uber_graph_set_value_func(UBER_GRAPH(mem_graph), get_mem, NULL, NULL);

	hbox = new_label_container();
	gtk_box_pack_start(GTK_BOX(group), gtk_widget_get_parent(hbox), FALSE, TRUE, 0);
	label = add_label(hbox, "Memory Free", "#3465a4");
	uber_label_bind_graph(UBER_LABEL(label), UBER_GRAPH(mem_graph), 1);
	label = add_label(hbox, "Swap Free", "#8ae234");
	uber_label_bind_graph(UBER_LABEL(label), UBER_GRAPH(mem_graph), 2);
	mem_label_hbox = hbox;

#if 1
	heat = uber_heat_map_new();
	uber_heat_map_set_block_size(UBER_HEAT_MAP(heat),
	                             60, TRUE,
	                             5, FALSE);
	uber_heat_map_set_value_func(UBER_HEAT_MAP(heat), get_iolat, NULL, NULL);
	gtk_container_add(GTK_CONTAINER(vbox), heat);
	gtk_widget_show(heat);

	heat2 = uber_heat_map_new();
	uber_heat_map_set_block_size(UBER_HEAT_MAP(heat2),
	                             5, FALSE,
	                             5, TRUE);
	gtk_container_add(GTK_CONTAINER(vbox), heat2);
	gtk_widget_show(heat2);
#endif

	setup_iolats();
	next_iolats();

	next_load();
	next_cpu();
	next_mem();
	next_net();
	next_pmem();
	next_sched();
	next_threads();

	next_load();
	next_cpu();
	next_mem();
	next_net();
	next_pmem();
	next_sched();

	return window;
}

static gboolean
get_pmem (UberGraph *graph,
          gint       line,
          gdouble   *value,
          gpointer   user_data)
{
	switch (line) {
	case 1:
		*value = pmem_info.size;
		break;
	case 2:
		*value = pmem_info.resident;
		break;
	default:
		*value = 0;
		return FALSE;
	}
	return TRUE;
}

static gboolean
get_sched (UberGraph *graph,
           gint       line,
           gdouble   *value,
           gpointer   user_data)
{
	switch (line) {
	case 1:
		*value = sched_info.vruntime;
		break;
	default:
		*value = 0;
		return FALSE;
	}
	return TRUE;
}

static void
create_pid_graphs (GPid pid)
{
	GtkWidget *label;

	label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label), "<b>Process Memory</b>");
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, TRUE, 0);
	gtk_misc_set_alignment(GTK_MISC(label), .0, .5);
	gtk_widget_show(label);

	pmem_graph = create_graph();
	uber_graph_set_yautoscale(UBER_GRAPH(pmem_graph), TRUE);
	uber_graph_add_line(UBER_GRAPH(pmem_graph));
	uber_graph_add_line(UBER_GRAPH(pmem_graph));
	uber_graph_set_value_func(UBER_GRAPH(pmem_graph), get_pmem, NULL, NULL);

	label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label), "<b>Scheduler Time</b>");
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, TRUE, 0);
	gtk_misc_set_alignment(GTK_MISC(label), .0, .5);
	gtk_widget_show(label);

	sched_graph = create_graph();
	uber_graph_set_yautoscale(UBER_GRAPH(sched_graph), TRUE);
	uber_graph_add_line(UBER_GRAPH(sched_graph));
	uber_graph_set_value_func(UBER_GRAPH(sched_graph), get_sched, NULL, NULL);

	label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label), "<b>Thread Count</b>");
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, TRUE, 0);
	gtk_misc_set_alignment(GTK_MISC(label), .0, .5);
	gtk_widget_show(label);

	thread_graph = create_graph();
	uber_graph_set_format(UBER_GRAPH(thread_graph), UBER_GRAPH_INTEGRAL);
	uber_graph_set_yautoscale(UBER_GRAPH(thread_graph), TRUE);
	uber_graph_add_line(UBER_GRAPH(thread_graph));
	uber_graph_set_value_func(UBER_GRAPH(thread_graph), get_threads, NULL, NULL);
}

static volatile gboolean quit = FALSE;

static gpointer
sample_func (gpointer data)
{
	while (!quit) {
		DEBUG("Running samplers ...");
		next_load();
		next_cpu();
		next_net();
		next_mem();
		next_pmem();
		next_sched();
		next_threads();
		next_iolats();
		g_usleep(G_USEC_PER_SEC);
	}
	return NULL;
}

static gboolean
test_4_foreach (UberBuffer *buffer,
                gdouble     value,
                gpointer    user_data)
{
	static gint count = 0;
	gint v = value;

	g_assert_cmpint(v, ==, 4 - count);
	count++;
	return (value == 1.);
}

static gboolean
test_2_foreach (UberBuffer *buffer,
                gdouble     value,
                gpointer    user_data)
{
	static gint count = 0;
	gint v = value;

	g_assert_cmpint(v, ==, 4 - count);
	count++;
	return (value == 3.);
}

static gboolean
test_2e_foreach (UberBuffer *buffer,
                 gdouble     value,
                 gpointer    user_data)
{
	static gint count = 0;
	gint v = value;

	if (count == 0 || count == 1) {
		g_assert_cmpint(v, ==, 4. - count);
	} else {
		g_assert(value == -INFINITY);
	}
	count++;
	return FALSE;
}

static void
run_buffer_tests (void)
{
	UberBuffer *buf;

	buf = uber_buffer_new();
	g_assert(buf);

	uber_buffer_append(buf, 1.);
	uber_buffer_append(buf, 2.);
	uber_buffer_append(buf, 3.);
	uber_buffer_append(buf, 4.);
	uber_buffer_foreach(buf, test_4_foreach, NULL);

	uber_buffer_set_size(buf, 2);
	g_assert_cmpint(buf->len, ==, 2);
	g_assert_cmpint(buf->pos, ==, 0);
	uber_buffer_foreach(buf, test_2_foreach, NULL);

	uber_buffer_set_size(buf, 32);
	g_assert_cmpint(buf->len, ==, 32);
	g_assert_cmpint(buf->pos, ==, 0);
	uber_buffer_foreach(buf, test_2e_foreach, NULL);
}

static void
child_exited (GPid     pid,
              gint     status,
              gpointer data)
{
	g_printerr("Child exited.\n");
	reaped = TRUE;
	gtk_main_quit();
}

gint
main (gint   argc,
      gchar *argv[])
{
	GError *error = NULL;
	GtkWidget *window;
	gchar **args;
	gint i;

	g_set_application_name(_("uber-graph"));
	g_thread_init(NULL);
	gtk_init(&argc, &argv);

#if 1
	/* run the UberBuffer tests */
	run_buffer_tests();
#endif

	labels = g_ptr_array_new();

	/* initialize sources to -INFINITY */
	cpu_info.cpuUsage = -INFINITY;
	net_info.bytesIn = -INFINITY;
	net_info.bytesOut = -INFINITY;
	mem_info.memFree = -INFINITY;
	mem_info.swapFree = -INFINITY;
	load_info.load5 = -INFINITY;
	load_info.load10 = -INFINITY;
	load_info.load15 = -INFINITY;

	/* if we need to spawn a process, do so */
	if (argc > 1) {
		g_print("Spawning subprocess ...\n");
		args = g_new0(gchar*, argc);
		for (i = 0; i < argc - 1; i++) {
			args[i] = g_strdup(argv[i + 1]);
		}
		if (!g_spawn_async(".", args, NULL,
		                   G_SPAWN_SEARCH_PATH | G_SPAWN_LEAVE_DESCRIPTORS_OPEN | G_SPAWN_DO_NOT_REAP_CHILD,
		                   NULL, NULL,
		                   &pid, &error)) {
			g_printerr("%s\n", error->message);
			g_clear_error(&error);
			return EXIT_FAILURE;
		}
		g_child_watch_add(pid, child_exited, NULL);
		g_print("Process %d started.\n", (gint)pid);
	}

	/* run the test gui */
	window = create_main_window();

	/* add application specific graphs */
	if (pid) {
		create_pid_graphs(pid);
	}

	g_signal_connect(window, "delete-event", gtk_main_quit, NULL);
	g_thread_create(sample_func, NULL, FALSE, NULL);

	gtk_main();

	/* kill child process if needed */
	if (pid && !reaped) {
		g_print("Exiting, killing child prcess.\n");
		kill(pid, SIGINT);
	}

	return EXIT_SUCCESS;
}
