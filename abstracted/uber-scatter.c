/* uber-scatter.c
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

#include "uber-scatter.h"
#include "g-ring.h"

/**
 * SECTION:uber-scatter.h
 * @title: UberScatter
 * @short_description: 
 *
 * Section overview.
 */

G_DEFINE_TYPE(UberScatter, uber_scatter, UBER_TYPE_GRAPH)

struct _UberScatterPrivate
{
	GRing *raw_data;
	gint   stride;
};

/**
 * uber_scatter_new:
 *
 * Creates a new instance of #UberScatter.
 *
 * Returns: the newly created instance of #UberScatter.
 * Side effects: None.
 */
GtkWidget*
uber_scatter_new (void)
{
	UberScatter *scatter;

	scatter = g_object_new(UBER_TYPE_SCATTER, NULL);
	return GTK_WIDGET(scatter);
}

/**
 * uber_scatter_destroy_array:
 * @array: A #GArray.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_scatter_destroy_array (gpointer data) /* IN */
{
	GArray **ar = data;

	if (ar) {
		g_array_unref(*ar);
	}
}

/**
 * uber_scatter_set_stride:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_scatter_set_stride (UberGraph *graph,  /* IN */
                          guint      stride) /* IN */
{
	UberScatterPrivate *priv;

	g_return_if_fail(UBER_IS_SCATTER(graph));

	priv = UBER_SCATTER(graph)->priv;
	if (priv->stride == stride) {
		return;
	}
	priv->stride = stride;
	if (priv->raw_data) {
		g_ring_unref(priv->raw_data);
	}
	priv->raw_data = g_ring_sized_new(sizeof(GArray*), stride,
	                                  uber_scatter_destroy_array);
}

/**
 * uber_scatter_render:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_scatter_render (UberGraph    *graph, /* IN */
                      cairo_t      *cr,    /* IN */
                      GdkRectangle *area)  /* IN */
{
	UberGraphPrivate *priv;
	cairo_pattern_t *cp;

	g_return_if_fail(UBER_IS_SCATTER(graph));

	priv = graph->priv;
	/*
	 * XXX: Temporarily draw a nice little gradient to test sliding.
	 */
	return;
	cp = cairo_pattern_create_linear(0, 0, area->width, 0);
	cairo_pattern_add_color_stop_rgb(cp, 0, .1, .1, .1);
	cairo_pattern_add_color_stop_rgb(cp, .2, .3, .3, .5);
	cairo_pattern_add_color_stop_rgb(cp, .4, .2, .7, .4);
	cairo_pattern_add_color_stop_rgb(cp, .7, .6, .2, .1);
	cairo_pattern_add_color_stop_rgb(cp, .8, .6, .8, .1);
	cairo_pattern_add_color_stop_rgb(cp, 1., .3, .8, .5);
	gdk_cairo_rectangle(cr, area);
	cairo_set_source(cr, cp);
	cairo_fill(cr);
	cairo_pattern_destroy(cp);
}

/**
 * uber_scatter_render_fast:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_scatter_render_fast (UberGraph    *graph, /* IN */
                           cairo_t      *cr,    /* IN */
                           GdkRectangle *area,  /* IN */
                           guint         epoch, /* IN */
                           gfloat        each)  /* IN */
{
	UberGraphPrivate *priv;
	gfloat x;
	gfloat y;
	gint i;

	g_return_if_fail(UBER_IS_SCATTER(graph));

	#define COUNT  3
	#define RADIUS 3

	priv = graph->priv;
	/*
	 * XXX: Temporarily draw nice little cicles;
	 */
	for (i = 0; i < COUNT; i++) {
		x = g_random_double_range(epoch - each, epoch);
		y = g_random_double_range(area->y, area->y + area->height);
		/*
		 * Shadow.
		 */
		cairo_arc(cr, x + .5, y + .5, RADIUS, 0, 2 * M_PI);
		cairo_set_source_rgb(cr, .3, .3, .3);
		cairo_fill(cr);
		/*
		 * Foreground.
		 */
		cairo_arc(cr, x, y, RADIUS, 0, 2 * M_PI);
		cairo_set_source_rgb(cr, 1., 0, 0);
		cairo_fill(cr);
	}
}

/**
 * uber_scatter_get_next_data:
 * @graph: A #UberGraph.
 *
 * Retrieve the next data point for the graph.
 *
 * Returns: None.
 * Side effects: None.
 */
static gboolean
uber_scatter_get_next_data (UberGraph *graph) /* IN */
{
	UberScatterPrivate *priv;

	g_return_val_if_fail(UBER_IS_SCATTER(graph), FALSE);

	priv = UBER_SCATTER(graph)->priv;
	return TRUE;
}

/**
 * uber_scatter_finalize:
 * @object: A #UberScatter.
 *
 * Finalizer for a #UberScatter instance.  Frees any resources held by
 * the instance.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_scatter_finalize (GObject *object) /* IN */
{
	G_OBJECT_CLASS(uber_scatter_parent_class)->finalize(object);
}

/**
 * uber_scatter_class_init:
 * @klass: A #UberScatterClass.
 *
 * Initializes the #UberScatterClass and prepares the vtable.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_scatter_class_init (UberScatterClass *klass) /* IN */
{
	GObjectClass *object_class;
	UberGraphClass *graph_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = uber_scatter_finalize;
	g_type_class_add_private(object_class, sizeof(UberScatterPrivate));

	graph_class = UBER_GRAPH_CLASS(klass);
	graph_class->render = uber_scatter_render;
	graph_class->render_fast = uber_scatter_render_fast;
	graph_class->set_stride = uber_scatter_set_stride;
	graph_class->get_next_data = uber_scatter_get_next_data;
}

/**
 * uber_scatter_init:
 * @scatter: A #UberScatter.
 *
 * Initializes the newly created #UberScatter instance.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_scatter_init (UberScatter *scatter) /* IN */
{
	scatter->priv = G_TYPE_INSTANCE_GET_PRIVATE(scatter,
	                                            UBER_TYPE_SCATTER,
	                                            UberScatterPrivate);
}
