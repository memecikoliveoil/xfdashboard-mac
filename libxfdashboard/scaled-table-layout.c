/*
 * scaled-table-layout: Layouts children in a dynamic table grid
 *                      (rows and columns are inserted and deleted
 *                      automatically depending on the number of
 *                      child actors) and scaled to fit the allocation
 *                      of the actor holding all child actors.
 * 
 * Copyright 2012-2021 Stephan Haller <nomad@froevel.de>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 * 
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <libxfdashboard/scaled-table-layout.h>

#include <glib/gi18n-lib.h>
#include <clutter/clutter.h>
#include <math.h>

#include <libxfdashboard/actor.h>
#include <libxfdashboard/animation.h>
#include <libxfdashboard/live-window-simple.h>
#include <libxfdashboard/window-tracker-window.h>
#include <stdlib.h>


/* Define this class in GObject system */
struct _XfdashboardScaledTableLayoutPrivate
{
	/* Properties related */
	gfloat		rowSpacing;
	gfloat		columnSpacing;
	gboolean	relativeScale;
	gboolean	preventUpscaling;

	/* Instance related */
	gint		rows;
	gint		columns;
	gint		numberChildren;

	gboolean	reentrantDetermineWidth;
	gboolean	reentrantDetermineHeight;
};

G_DEFINE_TYPE_WITH_PRIVATE(XfdashboardScaledTableLayout,
							xfdashboard_scaled_table_layout,
							CLUTTER_TYPE_LAYOUT_MANAGER)

/* Properties */
enum
{
	PROP_0,

	PROP_ROW_SPACING,
	PROP_COLUMN_SPACING,
	PROP_RELATIVE_SCALE,
	PROP_PREVENT_UPSCALING,

	PROP_NUMBER_CHILDREN,
	PROP_ROWS,
	PROP_COLUMNS,

	PROP_LAST
};

static GParamSpec* XfdashboardScaledTableLayoutProperties[PROP_LAST]={ 0, };


/* IMPLEMENTATION: Private variables and methods */

/* Updates the minimum number of rows and columns needed for layout */
static void _xfdashboard_scaled_table_layout_update_rows_and_columns(XfdashboardScaledTableLayout *self,
																		ClutterContainer *inContainer)
{
	XfdashboardScaledTableLayoutPrivate		*priv;
	ClutterRequestMode						requestMode;
	ClutterActorIter						iter;
	ClutterActor							*child;
	gint									numberChildren;
	gint									rows;
	gint									columns;

	g_return_if_fail(XFDASHBOARD_IS_SCALED_TABLE_LAYOUT(self));
	g_return_if_fail(CLUTTER_IS_CONTAINER(inContainer));
	g_return_if_fail(CLUTTER_IS_ACTOR(inContainer));

	priv=self->priv;

	/* Freeze notification */
	g_object_freeze_notify(G_OBJECT(self));

	/* Get number of visible child actors */
	numberChildren=0;
	clutter_actor_iter_init(&iter, CLUTTER_ACTOR(inContainer));
	while(clutter_actor_iter_next(&iter, &child))
	{
		if(clutter_actor_is_visible(child)) numberChildren++;
	}

	if(numberChildren!=priv->numberChildren)
	{
		priv->numberChildren=numberChildren;
		g_object_notify_by_pspec(G_OBJECT(self), XfdashboardScaledTableLayoutProperties[PROP_NUMBER_CHILDREN]);
	}

	/* Get request mode to determine if more rows than columns are needed
	 * or the opposite
	 */
	requestMode=clutter_actor_get_request_mode(CLUTTER_ACTOR(inContainer));

	/* Calculate and update number of rows and columns */
	if(requestMode==CLUTTER_REQUEST_HEIGHT_FOR_WIDTH)
	{
		rows=ceil(sqrt((double)priv->numberChildren));
		columns=ceil((double)priv->numberChildren / (double)priv->rows);
	}
		else
		{
			columns=ceil(sqrt((double)priv->numberChildren));
			rows=ceil((double)priv->numberChildren / (double)priv->columns);
		}

	if(rows!=priv->rows)
	{
		priv->rows=rows;
		g_object_notify_by_pspec(G_OBJECT(self), XfdashboardScaledTableLayoutProperties[PROP_ROWS]);
	}

	if(columns!=priv->columns)
	{
		priv->columns=columns;
		g_object_notify_by_pspec(G_OBJECT(self), XfdashboardScaledTableLayoutProperties[PROP_COLUMNS]);
	}

	/* Thaw notification */
	g_object_thaw_notify(G_OBJECT(self));
}

/* IMPLEMENTATION: ClutterLayoutManager */

/* Get preferred width/height */
static void _xfdashboard_scaled_table_layout_get_preferred_width(ClutterLayoutManager *self,
																	ClutterContainer *inContainer,
																	gfloat inForHeight,
																	gfloat *outMinWidth,
																	gfloat *outNaturalWidth)
{
	XfdashboardScaledTableLayoutPrivate		*priv;
	gfloat									maxMinWidth, maxNaturalWidth;
	ClutterActor							*parent;

	g_return_if_fail(XFDASHBOARD_IS_SCALED_TABLE_LAYOUT(self));
	g_return_if_fail(CLUTTER_IS_CONTAINER(inContainer));

	priv=XFDASHBOARD_SCALED_TABLE_LAYOUT(self)->priv;

	/* Set up default values */
	maxMinWidth=0.0f;
	maxNaturalWidth=0.0f;

	/* Update number of rows and columns needed for layout */
	_xfdashboard_scaled_table_layout_update_rows_and_columns(XFDASHBOARD_SCALED_TABLE_LAYOUT(self), inContainer);

	/* Get size of parent if this child is parented and if it is not reentrant */
	parent=clutter_actor_get_parent(CLUTTER_ACTOR(inContainer));
	if(parent && !priv->reentrantDetermineWidth)
	{
		/* Prevent getting size of parent (reentrance) */
		priv->reentrantDetermineWidth=TRUE;

		/* Get size of parent */
		clutter_actor_get_size(CLUTTER_ACTOR(parent), &maxNaturalWidth, NULL);

		/* Allow getting size of parent again */
		priv->reentrantDetermineWidth=FALSE;
	}

	/* Calculate width */
	if(priv->columns>0)
	{
		maxMinWidth=(priv->columns-1)*priv->columnSpacing;
		if(maxNaturalWidth==0.0f) maxNaturalWidth=(priv->columns-1)*priv->columnSpacing;
	}

	/* Set return values */
	if(outMinWidth) *outMinWidth=maxMinWidth;
	if(outNaturalWidth) *outNaturalWidth=maxNaturalWidth;
}

static void _xfdashboard_scaled_table_layout_get_preferred_height(ClutterLayoutManager *self,
																	ClutterContainer *inContainer,
																	gfloat inForWidth,
																	gfloat *outMinHeight,
																	gfloat *outNaturalHeight)
{
	XfdashboardScaledTableLayoutPrivate		*priv;
	gfloat									maxMinHeight, maxNaturalHeight;
	ClutterActor							*parent;

	g_return_if_fail(XFDASHBOARD_IS_SCALED_TABLE_LAYOUT(self));
	g_return_if_fail(CLUTTER_IS_CONTAINER(inContainer));

	priv=XFDASHBOARD_SCALED_TABLE_LAYOUT(self)->priv;

	/* Set up default values */
	maxMinHeight=0.0f;
	maxNaturalHeight=0.0f;

	/* Update number of rows and columns needed for layout */
	_xfdashboard_scaled_table_layout_update_rows_and_columns(XFDASHBOARD_SCALED_TABLE_LAYOUT(self), inContainer);

	/* Get size of parent if this child is parented and if it is not reentrant */
	parent=clutter_actor_get_parent(CLUTTER_ACTOR(inContainer));
	if(parent && !priv->reentrantDetermineHeight)
	{
		/* Prevent getting size of parent (reentrance) */
		priv->reentrantDetermineHeight=TRUE;

		/* Get size of parent */
		clutter_actor_get_size(CLUTTER_ACTOR(parent), NULL, &maxNaturalHeight);

		/* Allow getting size of parent again */
		priv->reentrantDetermineHeight=FALSE;
	}

	/* Calculate height */
	if(priv->rows>0)
	{
		maxMinHeight=(priv->rows-1)*priv->rowSpacing;
		if(maxNaturalHeight==0.0f) maxNaturalHeight=(priv->rows-1)*priv->rowSpacing;
	}

	/* Set return values */
	if(outMinHeight) *outMinHeight=maxMinHeight;
	if(outNaturalHeight) *outNaturalHeight=maxNaturalHeight;
}

/* Re-layout and allocate children of container we manage */
/* Window packing like macOS Mission Control: all windows keep their relative
 * real sizes (one common scale), are put into rows ordered by their place on
 * screen, and the number of rows giving the largest scale is used.
 */
typedef struct
{
	ClutterActor	*actor;
	gfloat			width, height;
	gfloat			screenX, screenY;
} XfdashboardScaledTableLayoutItem;

static gint _xfdashboard_scaled_table_layout_sort_by_y(gconstpointer inLeft, gconstpointer inRight)
{
	const XfdashboardScaledTableLayoutItem	*left=inLeft, *right=inRight;
	gfloat									l=left->screenY+left->height/2.0f;
	gfloat									r=right->screenY+right->height/2.0f;

	return(l<r ? -1 : (l>r ? 1 : 0));
}

static gint _xfdashboard_scaled_table_layout_sort_by_x(gconstpointer inLeft, gconstpointer inRight)
{
	const XfdashboardScaledTableLayoutItem	*left=inLeft, *right=inRight;
	gfloat									l=left->screenX+left->width/2.0f;
	gfloat									r=right->screenX+right->width/2.0f;

	return(l<r ? -1 : (l>r ? 1 : 0));
}

/* Split n items into rows of roughly equal total width; returns row starts */
static void _xfdashboard_scaled_table_layout_split_rows(XfdashboardScaledTableLayoutItem *inItems, gint inCount, gint inRows, gint *outStarts)
{
	gfloat		total=0.0f, target, acc;
	gint		i, row;

	for(i=0; i<inCount; i++) total+=inItems[i].width;
	target=total/inRows;

	outStarts[0]=0;
	row=1;
	acc=0.0f;
	for(i=0; i<inCount && row<inRows; i++)
	{
		acc+=inItems[i].width;
		/* close row when target width is reached, but leave at least one
		 * item for each remaining row
		 */
		if((acc>=target*row - inItems[i].width/2.0f && i+1<=inCount-(inRows-row)) ||
			inCount-(i+1)==inRows-row)
		{
			outStarts[row++]=i+1;
		}
	}
	while(row<inRows) outStarts[row++]=inCount;
	outStarts[inRows]=inCount;
}

static void _xfdashboard_scaled_table_layout_allocate(ClutterLayoutManager *self,
														ClutterContainer *inContainer,
														const ClutterActorBox *inAllocation,
														ClutterAllocationFlags inFlags)
{
	XfdashboardScaledTableLayoutPrivate		*priv;
	ClutterActor							*child;
	ClutterActorIter						iter;
	XfdashboardScaledTableLayoutItem		*items;
	gint									count, i, rows, bestRows, row;
	gint									*starts;
	gfloat									availWidth, availHeight, spacing;
	gfloat									bestScale, y;

	g_return_if_fail(XFDASHBOARD_IS_SCALED_TABLE_LAYOUT(self));
	g_return_if_fail(CLUTTER_IS_CONTAINER(inContainer));

	priv=XFDASHBOARD_SCALED_TABLE_LAYOUT(self)->priv;
	spacing=MAX(priv->columnSpacing, priv->rowSpacing);
	clutter_actor_get_size(CLUTTER_ACTOR(inContainer), &availWidth, &availHeight);

	/* Collect visible children with their natural size and screen position */
	items=g_new0(XfdashboardScaledTableLayoutItem, MAX(1, clutter_actor_get_n_children(CLUTTER_ACTOR(inContainer))));
	count=0;
	clutter_actor_iter_init(&iter, CLUTTER_ACTOR(inContainer));
	while(clutter_actor_iter_next(&iter, &child))
	{
		gfloat								w, h;

		if(!clutter_actor_is_visible(child)) continue;

		clutter_actor_get_preferred_size(child, NULL, NULL, &w, &h);
		if(w<=0.0f || h<=0.0f)
		{
			ClutterActorBox					empty={ 0, 0, 0, 0 };

			clutter_actor_allocate(child, &empty, inFlags);
			continue;
		}

		items[count].actor=child;
		items[count].width=w;
		items[count].height=h;
		items[count].screenX=count*10.0f;
		items[count].screenY=0.0f;
		if(XFDASHBOARD_IS_LIVE_WINDOW_SIMPLE(child))
		{
			XfdashboardWindowTrackerWindow	*window;
			gint							wx, wy, ww, wh;

			window=xfdashboard_live_window_simple_get_window(XFDASHBOARD_LIVE_WINDOW_SIMPLE(child));
			if(window)
			{
				xfdashboard_window_tracker_window_get_geometry(window, &wx, &wy, &ww, &wh);
				items[count].screenX=wx;
				items[count].screenY=wy;

				/* Use the real window size, not the actor's preferred size which
				 * changes with its decorations (e.g. title shown when selected)
				 * and would reshuffle the whole layout on every hover
				 */
				if(ww>0 && wh>0)
				{
					items[count].width=ww;
					items[count].height=wh;
				}
			}
		}
		count++;
	}

	if(count==0 || availWidth<=0.0f || availHeight<=0.0f)
	{
		g_free(items);
		return;
	}

	/* Order from top to bottom; rows are filled in this order */
	qsort(items, count, sizeof(XfdashboardScaledTableLayoutItem), (GCompareFunc)_xfdashboard_scaled_table_layout_sort_by_y);

	/* Find number of rows giving the largest common scale */
	starts=g_new0(gint, count+1);
	bestScale=0.0f;
	bestRows=1;
	for(rows=1; rows<=count; rows++)
	{
		gfloat							scale, sumHeights;

		_xfdashboard_scaled_table_layout_split_rows(items, count, rows, starts);

		scale=G_MAXFLOAT;
		sumHeights=0.0f;
		for(row=0; row<rows; row++)
		{
			gfloat						rowWidth=0.0f, rowHeight=0.0f;
			gint						k=starts[row+1]-starts[row];

			if(k<=0) continue;
			for(i=starts[row]; i<starts[row+1]; i++)
			{
				rowWidth+=items[i].width;
				rowHeight=MAX(rowHeight, items[i].height);
			}
			scale=MIN(scale, (availWidth-((k-1)*spacing))/rowWidth);
			sumHeights+=rowHeight;
		}
		scale=MIN(scale, (availHeight-((rows-1)*spacing))/sumHeights);
		if(priv->preventUpscaling) scale=MIN(scale, 1.0f);

		if(scale>bestScale*1.0001f)
		{
			bestScale=scale;
			bestRows=rows;
		}
	}

	/* Place rows centered. Each row may be scaled up on its own until it
	 * fills the width (so rows with few windows do not stay small), then all
	 * rows are scaled down together if they do not fit into the height.
	 */
	_xfdashboard_scaled_table_layout_split_rows(items, count, bestRows, starts);
	{
		gfloat							*rowScale;
		gfloat							totalHeight, fit;

		rowScale=g_new0(gfloat, bestRows);
		totalHeight=0.0f;
		for(row=0; row<bestRows; row++)
		{
			gfloat						rowWidth=0.0f, rowHeight=0.0f;
			gint						k=starts[row+1]-starts[row];

			if(k<=0) continue;
			for(i=starts[row]; i<starts[row+1]; i++)
			{
				rowWidth+=items[i].width;
				rowHeight=MAX(rowHeight, items[i].height);
			}

			rowScale[row]=(availWidth-((k-1)*spacing))/rowWidth;
			rowScale[row]=MIN(rowScale[row], bestScale*1.5f);
			rowScale[row]=MAX(rowScale[row], bestScale);
			if(priv->preventUpscaling) rowScale[row]=MIN(rowScale[row], 1.0f);
			totalHeight+=rowHeight*rowScale[row];
		}

		/* Shrink enlarged rows until everything fits into the height. A row
		 * never gets smaller than the common scale (which is known to fit), so
		 * rows hitting that limit are fixed and the remaining height is shared
		 * by the other rows. Repeat as fixing rows changes the remaining height
		 * (a single pass let enlarged rows overflow, e.g. behind the dock).
		 */
		{
			gboolean					*fixed;
			gint						pass;

			fixed=g_new0(gboolean, bestRows);
			for(pass=0; pass<=bestRows; pass++)
			{
				gfloat					fixedHeight=0.0f, freeHeight=0.0f;
				gboolean				changed=FALSE;

				for(row=0; row<bestRows; row++)
				{
					gfloat				rowHeight=0.0f;

					for(i=starts[row]; i<starts[row+1]; i++) rowHeight=MAX(rowHeight, items[i].height);
					if(fixed[row]) fixedHeight+=rowHeight*rowScale[row];
						else freeHeight+=rowHeight*rowScale[row];
				}

				if(freeHeight<=0.0f) break;
				fit=(availHeight-((bestRows-1)*spacing)-fixedHeight)/freeHeight;
				if(fit>=1.0f) break;

				for(row=0; row<bestRows; row++)
				{
					if(fixed[row]) continue;
					rowScale[row]*=fit;
					if(rowScale[row]<=bestScale)
					{
						rowScale[row]=bestScale;
						fixed[row]=TRUE;
						changed=TRUE;
					}
				}
				if(!changed) break;
			}
			g_free(fixed);

			totalHeight=0.0f;
			for(row=0; row<bestRows; row++)
			{
				gfloat					rowHeight=0.0f;

				for(i=starts[row]; i<starts[row+1]; i++) rowHeight=MAX(rowHeight, items[i].height);
				totalHeight+=rowHeight*rowScale[row];
			}
		}
		totalHeight+=(bestRows-1)*spacing;
		y=floor(MAX(0.0f, (availHeight-totalHeight)/2.0f));

		for(row=0; row<bestRows; row++)
		{
			gfloat						rowWidth=0.0f, rowHeight=0.0f, x;
			gint						k=starts[row+1]-starts[row];
			gfloat						scale=rowScale[row];

			if(k<=0) continue;

			/* Within a row order from left to right */
			qsort(items+starts[row], k, sizeof(XfdashboardScaledTableLayoutItem), (GCompareFunc)_xfdashboard_scaled_table_layout_sort_by_x);

			for(i=starts[row]; i<starts[row+1]; i++)
			{
				rowWidth+=items[i].width*scale;
				rowHeight=MAX(rowHeight, items[i].height*scale);
			}
			rowWidth+=(k-1)*spacing;
			x=floor((availWidth-rowWidth)/2.0f);

			for(i=starts[row]; i<starts[row+1]; i++)
			{
				ClutterActorBox			childAllocation;
				gboolean				fixedPosition;
				gfloat					fixedX, fixedY;
				gfloat					w=items[i].width*scale;
				gfloat					h=items[i].height*scale;

				g_object_get(items[i].actor,
								"fixed-position-set", &fixedPosition,
								"fixed-x", &fixedX,
								"fixed-y", &fixedY,
								NULL);

				if(fixedPosition)
				{
					childAllocation.x1=ceil(fixedX);
					childAllocation.y1=ceil(fixedY);
				}
					else
					{
						childAllocation.x1=floor(x);
						childAllocation.y1=floor(y+((rowHeight-h)/2.0f));
					}
				childAllocation.x2=childAllocation.x1+floor(w);
				childAllocation.y2=childAllocation.y1+floor(h);
				clutter_actor_allocate(items[i].actor, &childAllocation, inFlags);

				x+=w+spacing;
			}

			y+=rowHeight+spacing;
		}

		g_free(rowScale);
	}

	g_free(starts);
	g_free(items);
}

/* IMPLEMENTATION: GObject */

/* Set/get properties */
static void _xfdashboard_scaled_table_layout_set_property(GObject *inObject,
															guint inPropID,
															const GValue *inValue,
															GParamSpec *inSpec)
{
	XfdashboardScaledTableLayout			*self=XFDASHBOARD_SCALED_TABLE_LAYOUT(inObject);
	
	switch(inPropID)
	{
		case PROP_ROW_SPACING:
			xfdashboard_scaled_table_layout_set_row_spacing(self, g_value_get_float(inValue));
			break;

		case PROP_COLUMN_SPACING:
			xfdashboard_scaled_table_layout_set_column_spacing(self, g_value_get_float(inValue));
			break;

		case PROP_RELATIVE_SCALE:
			xfdashboard_scaled_table_layout_set_relative_scale(self, g_value_get_boolean(inValue));
			break;

		case PROP_PREVENT_UPSCALING:
			xfdashboard_scaled_table_layout_set_prevent_upscaling(self, g_value_get_boolean(inValue));
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(inObject, inPropID, inSpec);
			break;
	}
}

static void _xfdashboard_scaled_table_layout_get_property(GObject *inObject,
															guint inPropID,
															GValue *outValue,
															GParamSpec *inSpec)
{
	XfdashboardScaledTableLayout	*self=XFDASHBOARD_SCALED_TABLE_LAYOUT(inObject);

	switch(inPropID)
	{
		case PROP_ROW_SPACING:
			g_value_set_float(outValue, self->priv->rowSpacing);
			break;

		case PROP_COLUMN_SPACING:
			g_value_set_float(outValue, self->priv->columnSpacing);
			break;

		case PROP_RELATIVE_SCALE:
			g_value_set_boolean(outValue, self->priv->relativeScale);
			break;

		case PROP_PREVENT_UPSCALING:
			g_value_set_boolean(outValue, self->priv->preventUpscaling);
			break;

		case PROP_NUMBER_CHILDREN:
			g_value_set_int(outValue, self->priv->numberChildren);
			break;

		case PROP_ROWS:
			g_value_set_int(outValue, self->priv->rows);
			break;

		case PROP_COLUMNS:
			g_value_set_int(outValue, self->priv->columns);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(inObject, inPropID, inSpec);
			break;
	}
}

/* Class initialization
 * Override functions in parent classes and define properties
 * and signals
 */
static void xfdashboard_scaled_table_layout_class_init(XfdashboardScaledTableLayoutClass *klass)
{
	ClutterLayoutManagerClass	*layoutClass=CLUTTER_LAYOUT_MANAGER_CLASS(klass);
	GObjectClass				*gobjectClass=G_OBJECT_CLASS(klass);

	/* Override functions */
	layoutClass->get_preferred_width=_xfdashboard_scaled_table_layout_get_preferred_width;
	layoutClass->get_preferred_height=_xfdashboard_scaled_table_layout_get_preferred_height;
	layoutClass->allocate=_xfdashboard_scaled_table_layout_allocate;

	gobjectClass->set_property=_xfdashboard_scaled_table_layout_set_property;
	gobjectClass->get_property=_xfdashboard_scaled_table_layout_get_property;

	/* Define properties */
	XfdashboardScaledTableLayoutProperties[PROP_ROW_SPACING]=
		g_param_spec_float("row-spacing",
								"Row spacing",
								"The spacing between rows in table",
								0.0f,
								G_MAXFLOAT,
								0.0f,
								G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	XfdashboardScaledTableLayoutProperties[PROP_COLUMN_SPACING]=
		g_param_spec_float("column-spacing",
								"Column spacing",
								"The spacing between columns in table",
								0.0f,
								G_MAXFLOAT,
								0.0f,
								G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	XfdashboardScaledTableLayoutProperties[PROP_RELATIVE_SCALE]=
		g_param_spec_boolean("relative-scale",
								"Relative scale",
								"Whether all children should be scaled relatively to largest child",
								FALSE,
								G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	XfdashboardScaledTableLayoutProperties[PROP_PREVENT_UPSCALING]=
		g_param_spec_boolean("prevent-upscaling",
								"Prevent upscaling",
								"Whether this layout manager should prevent upsclaing any child beyond its real size",
								FALSE,
								G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	XfdashboardScaledTableLayoutProperties[PROP_NUMBER_CHILDREN]=
		g_param_spec_int("number-children",
								"Number children",
								"Current number of child actors in this layout",
								0,
								G_MAXINT,
								0,
								G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	XfdashboardScaledTableLayoutProperties[PROP_ROWS]=
		g_param_spec_int("rows",
								"Rows",
								"Current number of rows in this layout",
								0,
								G_MAXINT,
								0,
								G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	XfdashboardScaledTableLayoutProperties[PROP_COLUMNS]=
		g_param_spec_int("columns",
								"Columns",
								"Current number of columns in this layout",
								0,
								G_MAXINT,
								0,
								G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	
	g_object_class_install_properties(gobjectClass, PROP_LAST, XfdashboardScaledTableLayoutProperties);
}

/* Object initialization
 * Create private structure and set up default values
 */
static void xfdashboard_scaled_table_layout_init(XfdashboardScaledTableLayout *self)
{
	XfdashboardScaledTableLayoutPrivate	*priv;

	priv=self->priv=xfdashboard_scaled_table_layout_get_instance_private(self);

	/* Set default values */
	priv->rowSpacing=0.0f;
	priv->columnSpacing=0.0f;
	priv->relativeScale=FALSE;
	priv->preventUpscaling=FALSE;

	priv->rows=0;
	priv->columns=0;
	priv->numberChildren=0;

	priv->reentrantDetermineWidth=FALSE;
	priv->reentrantDetermineHeight=FALSE;
}

/* IMPLEMENTATION: Public API */

/* Create new instance */
ClutterLayoutManager* xfdashboard_scaled_table_layout_new(void)
{
	return(CLUTTER_LAYOUT_MANAGER(g_object_new(XFDASHBOARD_TYPE_SCALED_TABLE_LAYOUT, NULL)));
}

/* Get number of (visible) children which will be layouted */
gint xfdashboard_scaled_table_layout_get_number_children(XfdashboardScaledTableLayout *self)
{
	g_return_val_if_fail(XFDASHBOARD_IS_SCALED_TABLE_LAYOUT(self), 0);

	return(self->priv->numberChildren);
}

/* Get number of rows */
gint xfdashboard_scaled_table_layout_get_rows(XfdashboardScaledTableLayout *self)
{
	g_return_val_if_fail(XFDASHBOARD_IS_SCALED_TABLE_LAYOUT(self), 0);

	return(self->priv->rows);
}

/* Get number of columns */
gint xfdashboard_scaled_table_layout_get_columns(XfdashboardScaledTableLayout *self)
{
	g_return_val_if_fail(XFDASHBOARD_IS_SCALED_TABLE_LAYOUT(self), 0);

	return(self->priv->columns);
}

/* Get/set relative scaling of all children to largest one */
gboolean xfdashboard_scaled_table_layout_get_relative_scale(XfdashboardScaledTableLayout *self)
{
	g_return_val_if_fail(XFDASHBOARD_IS_SCALED_TABLE_LAYOUT(self), FALSE);

	return(self->priv->relativeScale);
}

void xfdashboard_scaled_table_layout_set_relative_scale(XfdashboardScaledTableLayout *self, gboolean inScaling)
{
	XfdashboardScaledTableLayoutPrivate		*priv;

	g_return_if_fail(XFDASHBOARD_IS_SCALED_TABLE_LAYOUT(self));

	priv=self->priv;

	/* Set new value if changed */
	if(priv->relativeScale!=inScaling)
	{
		/* Set new value and notify about property change */
		priv->relativeScale=inScaling;
		g_object_notify_by_pspec(G_OBJECT(self), XfdashboardScaledTableLayoutProperties[PROP_RELATIVE_SCALE]);

		/* Notify for upcoming layout changes */
		clutter_layout_manager_layout_changed(CLUTTER_LAYOUT_MANAGER(self));
	}
}

/* Get/set if layout manager should prevent to size any child larger than its real size */
gboolean xfdashboard_scaled_table_layout_get_prevent_upscaling(XfdashboardScaledTableLayout *self)
{
	g_return_val_if_fail(XFDASHBOARD_IS_SCALED_TABLE_LAYOUT(self), FALSE);

	return(self->priv->preventUpscaling);
}

void xfdashboard_scaled_table_layout_set_prevent_upscaling(XfdashboardScaledTableLayout *self, gboolean inPreventUpscaling)
{
	XfdashboardScaledTableLayoutPrivate		*priv;

	g_return_if_fail(XFDASHBOARD_IS_SCALED_TABLE_LAYOUT(self));

	priv=self->priv;

	/* Set new value if changed */
	if(priv->preventUpscaling!=inPreventUpscaling)
	{
		/* Set new value and notify about property change */
		priv->preventUpscaling=inPreventUpscaling;
		g_object_notify_by_pspec(G_OBJECT(self), XfdashboardScaledTableLayoutProperties[PROP_PREVENT_UPSCALING]);

		/* Notify for upcoming layout changes */
		clutter_layout_manager_layout_changed(CLUTTER_LAYOUT_MANAGER(self));
	}
}

/* Set relative row and column spacing to same value at once */
void xfdashboard_scaled_table_layout_set_spacing(XfdashboardScaledTableLayout *self, gfloat inSpacing)
{
	XfdashboardScaledTableLayoutPrivate		*priv;

	g_return_if_fail(XFDASHBOARD_IS_SCALED_TABLE_LAYOUT(self));
	g_return_if_fail(inSpacing>=0.0f);

	priv=self->priv;

	/* Set new values if changed */
	if(priv->rowSpacing!=inSpacing || priv->columnSpacing!=inSpacing)
	{
		/* Set new values and notify about properties changes */
		priv->rowSpacing=inSpacing;
		g_object_notify_by_pspec(G_OBJECT(self), XfdashboardScaledTableLayoutProperties[PROP_ROW_SPACING]);

		priv->columnSpacing=inSpacing;
		g_object_notify_by_pspec(G_OBJECT(self), XfdashboardScaledTableLayoutProperties[PROP_COLUMN_SPACING]);

		/* Notify for upcoming layout changes */
		clutter_layout_manager_layout_changed(CLUTTER_LAYOUT_MANAGER(self));
	}
}

/* Get/set row spacing */
gfloat xfdashboard_scaled_table_layout_get_row_spacing(XfdashboardScaledTableLayout *self)
{
	g_return_val_if_fail(XFDASHBOARD_IS_SCALED_TABLE_LAYOUT(self), 0.0f);

	return(self->priv->rowSpacing);
}

void xfdashboard_scaled_table_layout_set_row_spacing(XfdashboardScaledTableLayout *self, gfloat inSpacing)
{
	XfdashboardScaledTableLayoutPrivate		*priv;

	g_return_if_fail(XFDASHBOARD_IS_SCALED_TABLE_LAYOUT(self));
	g_return_if_fail(inSpacing>=0.0f);

	priv=self->priv;

	/* Set new value if changed */
	if(priv->rowSpacing!=inSpacing)
	{
		/* Set new value and notify about property change */
		priv->rowSpacing=inSpacing;
		g_object_notify_by_pspec(G_OBJECT(self), XfdashboardScaledTableLayoutProperties[PROP_ROW_SPACING]);

		/* Notify for upcoming layout changes */
		clutter_layout_manager_layout_changed(CLUTTER_LAYOUT_MANAGER(self));
	}
}

/* Get/set columns spacing */
gfloat xfdashboard_scaled_table_layout_get_column_spacing(XfdashboardScaledTableLayout *self)
{
	g_return_val_if_fail(XFDASHBOARD_IS_SCALED_TABLE_LAYOUT(self), 0.0f);

	return(self->priv->columnSpacing);
}

void xfdashboard_scaled_table_layout_set_column_spacing(XfdashboardScaledTableLayout *self, gfloat inSpacing)
{
	XfdashboardScaledTableLayoutPrivate		*priv;

	g_return_if_fail(XFDASHBOARD_IS_SCALED_TABLE_LAYOUT(self));
	g_return_if_fail(inSpacing>=0.0f);

	priv=self->priv;

	/* Set new value if changed */
	if(priv->columnSpacing!=inSpacing)
	{
		/* Set new value and notify about property change */
		priv->columnSpacing=inSpacing;
		g_object_notify_by_pspec(G_OBJECT(self), XfdashboardScaledTableLayoutProperties[PROP_COLUMN_SPACING]);

		/* Notify for upcoming layout changes */
		clutter_layout_manager_layout_changed(CLUTTER_LAYOUT_MANAGER(self));
	}
}
