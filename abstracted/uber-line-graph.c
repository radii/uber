/* uber-line-graph.c
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>

#include "uber-line-graph.h"
#include "g-ring.h"

#define RECT_BOTTOM(r) ((r).y + (r).height)
#define RECT_RIGHT(r)  ((r).x + (r).width)

/**
 * SECTION:uber-line-graph.h
 * @title: UberLineGraph
 * @short_description: 
 *
 * Section overview.
 */

G_DEFINE_TYPE(UberLineGraph, uber_line_graph, UBER_TYPE_GRAPH)

typedef struct
{
	GdkColor  color;
	GRing    *raw_data;
	GRing    *scaled_data;
} LineInfo;

struct _UberLineGraphPrivate
{
	GArray            *lines;
	cairo_antialias_t  antialias;
	guint              stride;
	gboolean           autoscale;
	UberLineGraphFunc  func;
	gpointer           func_data;
	GDestroyNotify     func_notify;
};

/**
 * uber_line_graph_init_ring:
 * @ring: A #GRing.
 *
 * Initialize the #GRing to default values (-INFINITY).
 *
 * Returns: None.
 * Side effects: None.
 */
static inline void
uber_line_graph_init_ring (GRing *ring) /* IN */
{
	gdouble val = -INFINITY;
	gint i;

	g_return_if_fail(ring != NULL);

	for (i = 0; i < ring->len; i++) {
		g_ring_append_val(ring, val);
	}
}

/**
 * uber_line_graph_new:
 *
 * Creates a new instance of #UberLineGraph.
 *
 * Returns: the newly created instance of #UberLineGraph.
 * Side effects: None.
 */
GtkWidget*
uber_line_graph_new (void)
{
	UberLineGraph *graph;

	graph = g_object_new(UBER_TYPE_LINE_GRAPH, NULL);
	return GTK_WIDGET(graph);
}

/**
 * uber_line_graph_set_autoscale:
 * @graph: A #UberLineGraph.
 * @autoscale: Should we autoscale.
 *
 * Sets if we should autoscale the range of the graph when a new input
 * value is outside the visible range.
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_line_graph_set_autoscale (UberLineGraph *graph,     /* IN */
                               gboolean       autoscale) /* IN */
{
	UberLineGraphPrivate *priv;

	g_return_if_fail(UBER_IS_LINE_GRAPH(graph));

	priv = graph->priv;
	priv->autoscale = autoscale;
}

/**
 * uber_line_graph_get_autoscale:
 * @graph: A #UberLineGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
gboolean
uber_line_graph_get_autoscale (UberLineGraph *graph) /* IN */
{
	g_return_val_if_fail(UBER_IS_LINE_GRAPH(graph), FALSE);
	return graph->priv->autoscale;
}

/**
 * uber_line_graph_add_line:
 * @graph: A #UberLineGraph.
 * @color: A #GdkColor for the line or %NULL.
 *
 * Adds a new line to the graph.  If color is %NULL, the next value
 * in the default color list will be used.
 *
 * See uber_line_graph_remove_line().
 *
 * Returns: The line identifier.
 * Side effects: None.
 */
guint
uber_line_graph_add_line (UberLineGraph  *graph, /* IN */
                          const GdkColor *color) /* IN */
{
	UberLineGraphPrivate *priv;
	LineInfo info = { { 0 } };

	g_return_val_if_fail(UBER_IS_LINE_GRAPH(graph), 0);

	priv = graph->priv;
	/*
	 * Retrieve the lines color.
	 */
	if (color) {
		info.color = *color;
	} else {
		gdk_color_parse("#729fcf", &info.color);
	}
	/*
	 * Allocate buffers for data points.
	 */
	info.raw_data = g_ring_sized_new(sizeof(gdouble), priv->stride, NULL);
	info.scaled_data = g_ring_sized_new(sizeof(gdouble), priv->stride, NULL);
	uber_line_graph_init_ring(info.raw_data);
	uber_line_graph_init_ring(info.scaled_data);
	/*
	 * Store the newly crated line.
	 */
	g_array_append_val(priv->lines, info);
	/*
	 * Mark the graph for full redraw.
	 */
	uber_graph_redraw(UBER_GRAPH(graph));
	/*
	 * Line indexes start from 1.
	 */
	return priv->lines->len;
}

/**
 * uber_line_graph_set_antialias:
 * @graph: A #UberLineGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_line_graph_set_antialias (UberLineGraph     *graph,     /* IN */
                               cairo_antialias_t  antialias) /* IN */
{
	UberLineGraphPrivate *priv;

	g_return_if_fail(UBER_IS_LINE_GRAPH(graph));

	priv = graph->priv;
	priv->antialias = antialias;
	uber_graph_redraw(UBER_GRAPH(graph));
}

/**
 * uber_line_graph_get_antialias:
 * @graph: A #UberLineGraph.
 *
 * Retrieves the antialias mode for the graph.
 *
 * Returns: A cairo_antialias_t.
 * Side effects: None.
 */
cairo_antialias_t
uber_line_graph_get_antialias (UberLineGraph *graph) /* IN */
{
	g_return_val_if_fail(UBER_IS_LINE_GRAPH(graph), 0);

	return graph->priv->antialias;
}

/**
 * uber_line_graph_get_next_data:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static gboolean
uber_line_graph_get_next_data (UberGraph *graph) /* IN */
{
	UberLineGraphPrivate *priv;
	LineInfo *line;
	gdouble val;
	gboolean ret = FALSE;
	gint i;

	g_return_val_if_fail(UBER_IS_LINE_GRAPH(graph), FALSE);

	priv = UBER_LINE_GRAPH(graph)->priv;
	/*
	 * Retrieve the next data point.
	 */
	if (priv->func) {
		for (i = 0; i < priv->lines->len; i++) {
			val = 0.;
			line = &g_array_index(priv->lines, LineInfo, i);
			if (!(ret = priv->func(UBER_LINE_GRAPH(graph),
			                       i + 1, &val,
			                       priv->func_data))) {
				val = -INFINITY;
			}
			g_ring_append_val(line->raw_data, val);
			/*
			 * TODO: Scale value.
			 */
			g_ring_append_val(line->scaled_data, val);
		}
	}
	return ret;
}

/**
 * uber_line_graph_set_data_func:
 * @graph: A #UberLineGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_line_graph_set_data_func (UberLineGraph     *graph,     /* IN */
                               UberLineGraphFunc  func,      /* IN */
                               gpointer           user_data, /* IN */
                               GDestroyNotify     notify)    /* IN */
{
	UberLineGraphPrivate *priv;

	g_return_if_fail(UBER_IS_LINE_GRAPH(graph));

	priv = graph->priv;
	/*
	 * Free existing data func if neccessary.
	 */
	if (priv->func_notify) {
		priv->func_notify(priv->func_data);
	}
	/*
	 * Store data func.
	 */
	priv->func = func;
	priv->func_data = user_data;
	priv->func_notify = notify;
}

/**
 * uber_line_graph_render:
 * @graph: A #UberGraph.
 * @cr: A #cairo_t context.
 * @area: Full area to render contents within.
 * @line: The line to render.
 *
 * Render a particular line to the graph.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_line_graph_render_line (UberLineGraph *graph, /* IN */
                             cairo_t       *cr,    /* IN */
                             GdkRectangle  *area,  /* IN */
                             LineInfo      *line)  /* IN */
{
	UberLineGraphPrivate *priv;
	guint x_epoch;
	guint x;
	guint y;
	guint last_x;
	guint last_y;
	gdouble val;
	gdouble each;
	gint i;

	g_return_if_fail(UBER_IS_LINE_GRAPH(graph));

	priv = graph->priv;
	/*
	 * Calculate number of pixels per data point.
	 */
	each = area->width / ((gfloat)priv->stride - 1.);
	/*
	 * Determine the end of our drawing area for relative coordinates.
	 */
	x_epoch = area->x + area->width;
	/*
	 * Prepare cairo settings.
	 */
	cairo_set_line_width(cr, 1.0);
	cairo_set_antialias(cr, priv->antialias);
	gdk_cairo_set_source_color(cr, &line->color);
	/*
	 * Force a new path.
	 */
	cairo_new_path(cr);
	/*
	 * Draw the line contents as bezier curves.
	 */
	for (i = 0; i < line->raw_data->len; i++) {
		/*
		 * Retrieve data point.
		 */
		val = g_ring_get_index(line->raw_data, gdouble, i);
		/*
		 * Once we get to -INFINITY, we must be at the end of the data
		 * sequence.  This may not always be true in the future.
		 */
		if (val == -INFINITY) {
			break;
		}
		/*
		 * Calculate X/Y coordinate.
		 */
		y = area->y + area->height - val;
		x = x_epoch - (each * i);
		if (i == 0) {
			/*
			 * Just move to the right position on first entry.
			 */
			cairo_move_to(cr, x, y);
			goto next;
		} else {
			/*
			 * Draw curve to data point using the last X/Y positions as
			 * control points.
			 */
			cairo_curve_to(cr,
			               last_x - (each / 2.),
			               last_y,
			               last_x - (each / 2.),
			               y, x, y);
		}
	  next:
		last_y = y;
		last_x = x;
	}
	/*
	 * Stroke the line content.
	 */
	cairo_stroke(cr);
}

/**
 * uber_line_graph_render:
 * @graph: A #UberGraph.
 *
 * Render the entire contents of the graph.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_line_graph_render (UberGraph    *graph, /* IN */
                        cairo_t      *cr,    /* IN */
                        GdkRectangle *rect)  /* IN */
{
	UberLineGraphPrivate *priv;
	LineInfo *line;
	gint i;

	g_return_if_fail(UBER_IS_LINE_GRAPH(graph));

	priv = UBER_LINE_GRAPH(graph)->priv;
	/*
	 * Render each line to the graph.
	 */
	for (i = 0; i < priv->lines->len; i++) {
		line = &g_array_index(priv->lines, LineInfo, i);
		uber_line_graph_render_line(UBER_LINE_GRAPH(graph), cr, rect, line);
	}
}

/**
 * uber_line_graph_render_fast:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_line_graph_render_fast (UberGraph    *graph, /* IN */
                             cairo_t      *cr,    /* IN */
                             GdkRectangle *rect,  /* IN */
                             guint         epoch, /* IN */
                             gfloat        each)  /* IN */
{
	UberLineGraphPrivate *priv;
	LineInfo *line;
	gdouble last_y;
	gdouble y;
	gint i;

	g_return_if_fail(UBER_IS_LINE_GRAPH(graph));
	g_return_if_fail(cr != NULL);
	g_return_if_fail(rect != NULL);

	priv = UBER_LINE_GRAPH(graph)->priv;
	/*
	 * Prepare cairo line styling.
	 */
	cairo_set_line_width(cr, 1.0);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
	/*
	 * Render most recent data point for each line.
	 */
	for (i = 0; i < priv->lines->len; i++) {
		line = &g_array_index(priv->lines, LineInfo, i);
		gdk_cairo_set_source_color(cr, &line->color);
		/*
		 * Calculate positions.
		 */
		y = g_ring_get_index(line->scaled_data, gdouble, 0);
		last_y = g_ring_get_index(line->scaled_data, gdouble, 1);
		/*
		 * Don't try to draw before we have real values.
		 */
		if ((isnan(y) || isinf(y)) || (isnan(last_y) || isinf(last_y))) {
			continue;
		}
		/*
		 * Translate position from bottom right corner.
		 */
		y = RECT_BOTTOM(*rect) - y;
		last_y = RECT_BOTTOM(*rect) - last_y;
		/*
		 * Convert relative position to fixed from bottom pixel.
		 */
		cairo_new_path(cr);
		cairo_move_to(cr, epoch, y);
		cairo_curve_to(cr,
		               epoch - (each / 2.),
		               y,
		               epoch - (each / 2.),
		               last_y,
		               epoch - each,
		               last_y);
		cairo_stroke(cr);
	}
}

/**
 * uber_line_graph_set_stride:
 * @graph: A #UberGraph.
 * @stride: The number of data points within the graph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_line_graph_set_stride (UberGraph *graph,  /* IN */
                            guint      stride) /* IN */
{
	UberLineGraphPrivate *priv;
	LineInfo *line;
	gint i;

	g_return_if_fail(UBER_IS_LINE_GRAPH(graph));

	priv = UBER_LINE_GRAPH(graph)->priv;
	priv->stride = stride;
	/*
	 * TODO: Support changing stride after lines have been added.
	 */
	if (priv->lines->len) {
		for (i = 0; i < priv->lines->len; i++) {
			line = &g_array_index(priv->lines, LineInfo, i);
			g_ring_unref(line->raw_data);
			g_ring_unref(line->scaled_data);
			line->raw_data = g_ring_sized_new(sizeof(gdouble),
			                                  priv->stride, NULL);
			line->scaled_data = g_ring_sized_new(sizeof(gdouble),
			                                     priv->stride, NULL);
			uber_line_graph_init_ring(line->raw_data);
			uber_line_graph_init_ring(line->scaled_data);
		}
		return;
	}
}

/**
 * uber_line_graph_finalize:
 * @object: A #UberLineGraph.
 *
 * Finalizer for a #UberLineGraph instance.  Frees any resources held by
 * the instance.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_line_graph_finalize (GObject *object) /* IN */
{
	UberLineGraphPrivate *priv;
	LineInfo *line;
	gint i;

	priv = UBER_LINE_GRAPH(object)->priv;
	/*
	 * Clean up after cached values.
	 */
	for (i = 0; i < priv->lines->len; i++) {
		line = &g_array_index(priv->lines, LineInfo, i);
		g_ring_unref(line->raw_data);
		g_ring_unref(line->scaled_data);
	}
	G_OBJECT_CLASS(uber_line_graph_parent_class)->finalize(object);
}

/**
 * uber_line_graph_class_init:
 * @klass: A #UberLineGraphClass.
 *
 * Initializes the #UberLineGraphClass and prepares the vtable.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_line_graph_class_init (UberLineGraphClass *klass) /* IN */
{
	GObjectClass *object_class;
	UberGraphClass *graph_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = uber_line_graph_finalize;
	g_type_class_add_private(object_class, sizeof(UberLineGraphPrivate));

	graph_class = UBER_GRAPH_CLASS(klass);
	graph_class->get_next_data = uber_line_graph_get_next_data;
	graph_class->render = uber_line_graph_render;
	graph_class->render_fast = uber_line_graph_render_fast;
	graph_class->set_stride = uber_line_graph_set_stride;
}

/**
 * uber_line_graph_init:
 * @graph: A #UberLineGraph.
 *
 * Initializes the newly created #UberLineGraph instance.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_line_graph_init (UberLineGraph *graph) /* IN */
{
	UberLineGraphPrivate *priv;

	/*
	 * Keep pointer to private data.
	 */
	graph->priv = G_TYPE_INSTANCE_GET_PRIVATE(graph,
	                                          UBER_TYPE_LINE_GRAPH,
	                                          UberLineGraphPrivate);
	priv = graph->priv;
	/*
	 * Initialize defaults.
	 */
	priv->stride = 60;
	priv->antialias = CAIRO_ANTIALIAS_DEFAULT;
	priv->lines = g_array_sized_new(FALSE, FALSE, sizeof(LineInfo), 2);
}
