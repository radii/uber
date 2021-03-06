/* uber-graph.h
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

#ifndef __UBER_GRAPH_H__
#define __UBER_GRAPH_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define UBER_TYPE_GRAPH            (uber_graph_get_type())
#define UBER_GRAPH(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), UBER_TYPE_GRAPH, UberGraph))
#define UBER_GRAPH_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), UBER_TYPE_GRAPH, UberGraph const))
#define UBER_GRAPH_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  UBER_TYPE_GRAPH, UberGraphClass))
#define UBER_IS_GRAPH(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), UBER_TYPE_GRAPH))
#define UBER_IS_GRAPH_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  UBER_TYPE_GRAPH))
#define UBER_GRAPH_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  UBER_TYPE_GRAPH, UberGraphClass))

typedef struct _UberGraph        UberGraph;
typedef struct _UberGraphClass   UberGraphClass;
typedef struct _UberGraphPrivate UberGraphPrivate;

struct _UberGraph
{
	GtkDrawingArea parent;

	/*< private >*/
	UberGraphPrivate *priv;
};

struct _UberGraphClass
{
	GtkDrawingAreaClass parent_class;

	gboolean (*get_next_data) (UberGraph    *graph);
	void     (*render)        (UberGraph    *graph,
	                           cairo_t      *cairo,
	                           GdkRectangle *content_area);
	void     (*render_fast)   (UberGraph    *graph,
	                           cairo_t      *cairo,
	                           GdkRectangle *content_area,
	                           guint         epoch,
	                           gfloat        each);
	void     (*set_stride)    (UberGraph    *graph,
	                           guint         stride);
};

GType uber_graph_get_type (void) G_GNUC_CONST;
void  uber_graph_set_dps  (UberGraph *graph,
                           gfloat     dps);
void  uber_graph_set_fps  (UberGraph *graph,
                           guint      fps);
void  uber_graph_redraw   (UberGraph *graph);

G_END_DECLS

#endif /* __UBER_GRAPH_H__ */
