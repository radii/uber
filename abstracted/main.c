#include "uber-line-graph.h"
#include "uber-heat-map.h"
#include "uber-scatter.h"

static guint gdk_event_count = 0;

static void
gdk_event_hook (GdkEvent *event, /* IN */
                gpointer  data)  /* IN */
{
	gdk_event_count++;
	gtk_main_do_event(event);
}

static gboolean
get_xevent_info (UberLineGraph *graph,     /* IN */
                 guint          line,      /* IN */
                 gdouble       *value,     /* OUT */
                 gpointer       user_data) /* IN */
{
	switch (line) {
	case 1:
		*value = gdk_event_count;
		gdk_event_count = 0;
		break;
	default:
		g_assert_not_reached();
	}
	return TRUE;
}

gint
main (gint   argc,   /* IN */
      gchar *argv[]) /* IN */
{
	GtkWidget *window;
	GtkWidget *vbox;
	GtkWidget *line;
	GtkWidget *map;
	GtkWidget *scatter;

	gtk_init(&argc, &argv);
	/*
	 * Install event hook to track how many X events we are doing.
	 */
	gdk_event_handler_set(gdk_event_hook, NULL, NULL);
	/*
	 * Create window.
	 */
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	vbox = gtk_vbox_new(TRUE, 3);
	line = g_object_new(UBER_TYPE_LINE_GRAPH, NULL);
	map = g_object_new(UBER_TYPE_HEAT_MAP, NULL);
	scatter = g_object_new(UBER_TYPE_SCATTER, NULL);
	uber_line_graph_add_line(UBER_LINE_GRAPH(line), NULL);
	uber_line_graph_set_data_func(UBER_LINE_GRAPH(line),
	                              get_xevent_info, NULL, NULL);
#if 0
	uber_graph_set_dps(UBER_GRAPH(map), 2.);
#endif
	gtk_container_add(GTK_CONTAINER(window), vbox);
	gtk_box_pack_start(GTK_BOX(vbox), line, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), map, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), scatter, TRUE, TRUE, 0);
	gtk_window_set_default_size(GTK_WINDOW(window), 300, 300);
	gtk_widget_show(scatter);
	gtk_widget_show(map);
	gtk_widget_show(line);
	gtk_widget_show(vbox);
	gtk_widget_show(window);
	g_signal_connect(window,
	                 "delete-event",
	                 G_CALLBACK(gtk_main_quit),
	                 NULL);
	gtk_main();
	return 0;
}
