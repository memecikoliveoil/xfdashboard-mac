/*
 * quicklaunch: Quicklaunch box
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


#define COGL_ENABLE_EXPERIMENTAL_API
#define CLUTTER_ENABLE_EXPERIMENTAL_API

#include <libxfdashboard/quicklaunch.h>

#include <glib/gi18n-lib.h>
#include <math.h>
#include <string.h>
#include <cairo.h>
#include <gtk/gtk.h>

#include <libxfdashboard/enums.h>
#include <libxfdashboard/core.h>
#include <libxfdashboard/application-button.h>
#include <libxfdashboard/toggle-button.h>
#include <libxfdashboard/drag-action.h>
#include <libxfdashboard/drop-action.h>
#include <libxfdashboard/applications-view.h>
#include <libxfdashboard/tooltip-action.h>
#include <libxfdashboard/focusable.h>
#include <libxfdashboard/marshal.h>
#include <libxfdashboard/desktop-app-info.h>
#include <libxfdashboard/desktop-app-info-action.h>
#include <libxfdashboard/application-database.h>
#include <libxfdashboard/application-tracker.h>
#include <libxfdashboard/window-tracker.h>
#include <libxfdashboard/window-tracker-window.h>
#include <libxfdashboard/click-action.h>
#include <libxfdashboard/popup-menu.h>
#include <libxfdashboard/popup-menu-item-button.h>
#include <libxfdashboard/popup-menu-item-separator.h>
#include <libxfdashboard/settings.h>
#include <libxfdashboard/utils.h>
#include <libxfdashboard/debug.h>


/* Define this class in GObject system */
static void _xfdashboard_quicklaunch_focusable_iface_init(XfdashboardFocusableInterface *iface);

struct _XfdashboardQuicklaunchPrivate
{
	/* Properties related */
	gchar							**favourites;

	gfloat							normalIconSize;
	gfloat							scaleMin;
	gfloat							scaleMax;
	gfloat							scaleStep;

	gfloat							spacing;
	gfloat							hoverZoom;
	gboolean						plankStyle;
	gfloat							zoomPointer;
	gfloat							zoomStrength;
	gfloat							zoomStrengthFrom;
	gfloat							zoomStrengthTo;
	ClutterTimeline					*zoomTimeline;
	gfloat							growLeft;
	gfloat							separatorX[8];	/* positions of divider lines drawn */
	gint							separatorCount;
	CoglTexture						*dotTexture;
	CoglTexture						*activeTexture;
	guint							windowsChangedIdleID;
	gfloat							growRight;
	gfloat							growTop;
	CoglPipeline					*dockPipeline;

	ClutterOrientation				orientation;

	/* Instance related */
	GBinding						*settingsFavouritesBinding;

	gfloat							scaleCurrent;

	ClutterActor					*appsButton;
	ClutterActor					*trashButton;

	guint							dragMode;
	ClutterActor					*dragPreviewIcon;

	ClutterActor					*selectedItem;

	ClutterActor					*separatorFavouritesToDynamic;

	XfdashboardApplicationDatabase	*appDB;
	XfdashboardApplicationTracker	*appTracker;

	XfdashboardSettings				*settings;
};

G_DEFINE_TYPE_WITH_CODE(XfdashboardQuicklaunch,
						xfdashboard_quicklaunch,
						XFDASHBOARD_TYPE_BACKGROUND,
						G_ADD_PRIVATE(XfdashboardQuicklaunch)
						G_IMPLEMENT_INTERFACE(XFDASHBOARD_TYPE_FOCUSABLE, _xfdashboard_quicklaunch_focusable_iface_init))

/* Properties */
enum
{
	PROP_0,

	PROP_FAVOURITES,
	PROP_NORMAL_ICON_SIZE,
	PROP_SPACING,
	PROP_ORIENTATION,
	PROP_HOVER_ZOOM,
	PROP_PLANK_STYLE,

	PROP_LAST
};

static GParamSpec* XfdashboardQuicklaunchProperties[PROP_LAST]={ 0, };

/* Signals */
enum
{
	SIGNAL_FAVOURITE_ADDED,
	SIGNAL_FAVOURITE_REMOVED,

	ACTION_SELECTION_ADD_FAVOURITE,
	ACTION_SELECTION_REMOVE_FAVOURITE,
	ACTION_FAVOURITE_REORDER_LEFT,
	ACTION_FAVOURITE_REORDER_RIGHT,
	ACTION_FAVOURITE_REORDER_UP,
	ACTION_FAVOURITE_REORDER_DOWN,

	SIGNAL_LAST
};

static guint XfdashboardQuicklaunchSignals[SIGNAL_LAST]={ 0, };


/* IMPLEMENTATION: Private variables and methods */
#define DEFAULT_SCALE_MIN			0.1
#define DEFAULT_SCALE_MAX			1.0
#define DEFAULT_SCALE_STEP			0.1

enum
{
	DRAG_MODE_NONE,
	DRAG_MODE_CREATE,
	DRAG_MODE_MOVE_EXISTING
};

/* Forward declarations */
#define XFDASHBOARD_QUICKLAUNCH_DIVIDER_KEY		"xfdashboard-dock-divider"
#define XFDASHBOARD_QUICKLAUNCH_DIVIDER_ENTRY	"separator"
/* Position of the applications (Launchpad) button within favourites */
#define XFDASHBOARD_QUICKLAUNCH_APPS_ENTRY		"launchpad"

static void _xfdashboard_quicklaunch_update_property_from_icons(XfdashboardQuicklaunch *self);
static ClutterActor* _xfdashboard_quicklaunch_create_dynamic_actor(XfdashboardQuicklaunch *self, GAppInfo *inAppInfo);
static ClutterActor* _xfdashboard_quicklaunch_create_favourite_actor(XfdashboardQuicklaunch *self, GAppInfo *inAppInfo);

/* Get actor for desktop application information */
static ClutterActor* _xfdashboard_quicklaunch_get_actor_for_appinfo(XfdashboardQuicklaunch *self,
																	GAppInfo *inAppInfo)
{
	XfdashboardQuicklaunchPrivate	*priv;
	ClutterActorIter				iter;
	ClutterActor					*child;
	GAppInfo						*desktopAppInfo;
	GFile							*desktopFile;

	g_return_val_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self), NULL);
	g_return_val_if_fail(G_IS_APP_INFO(inAppInfo), NULL);

	priv=self->priv;

	/* If requested application information does not contain a desktop file
	 * (means it must derive from XfdashboardDesktopAppInfo) then assume
	 * no actor exists for it.
	 */
	if(!XFDASHBOARD_IS_DESKTOP_APP_INFO(inAppInfo))
	{
		XFDASHBOARD_DEBUG(self, ACTOR,
							"%s is derived from %s but not derived %s",
							G_OBJECT_TYPE_NAME(inAppInfo),
							g_type_name(G_TYPE_APP_INFO),
							g_type_name(XFDASHBOARD_TYPE_DESKTOP_APP_INFO));
		return(NULL);
	}

	/* Check if application information is valid and provides a desktop file */
	desktopFile=xfdashboard_desktop_app_info_get_file(XFDASHBOARD_DESKTOP_APP_INFO(inAppInfo));
	if(!desktopFile)
	{
		g_critical("Could not check for duplicates for invalid %s object so assume no actor exists",
					G_OBJECT_TYPE_NAME(inAppInfo));
		return(NULL);
	}

	/* Iterate through actors and check each application button if it
	 * provides the requested desktop file of application information.
	 */
	clutter_actor_iter_init(&iter, CLUTTER_ACTOR(self));
	while(clutter_actor_iter_next(&iter, &child))
	{
		/* Only check application buttons */
 		if(!XFDASHBOARD_IS_APPLICATION_BUTTON(child)) continue;

		/* Do not check preview icon for drag if available */
		if(priv->dragPreviewIcon && child==priv->dragPreviewIcon) continue;

		/* Check if application button provides requested desktop file */
		desktopAppInfo=xfdashboard_application_button_get_app_info(XFDASHBOARD_APPLICATION_BUTTON(child));
		if(desktopAppInfo &&
			g_app_info_equal(desktopAppInfo, inAppInfo))
		{
			/* Found actor so return it */
			return(child);
		}
	}

	/* If we get here then this quicklaunch does not have any item
	 * which matches the requested application information so return NULL.
	 */
	return(NULL);
}

/* Check for duplicate application buttons */
static gboolean _xfdashboard_quicklaunch_has_favourite_appinfo(XfdashboardQuicklaunch *self, GAppInfo *inAppInfo)
{
	XfdashboardQuicklaunchPrivate	*priv;
	guint							i;
	GFile							*desktopFile;

	g_return_val_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self), TRUE);
	g_return_val_if_fail(G_IS_APP_INFO(inAppInfo), TRUE);

	priv=self->priv;

	/* If requested application information does not contain a desktop file
	 * (means it must derive from XfdashboardDesktopAppInfo) then assume
	 * it exists already.
	 */
	if(!XFDASHBOARD_IS_DESKTOP_APP_INFO(inAppInfo))
	{
		XFDASHBOARD_DEBUG(self, ACTOR,
							"%s is derived from %s but not derived %s",
							G_OBJECT_TYPE_NAME(inAppInfo),
							g_type_name(G_TYPE_APP_INFO),
							g_type_name(XFDASHBOARD_TYPE_DESKTOP_APP_INFO));
		return(TRUE);
	}

	/* Check if application information is valid and provides a desktop file */
	desktopFile=xfdashboard_desktop_app_info_get_file(XFDASHBOARD_DESKTOP_APP_INFO(inAppInfo));
	if(!desktopFile)
	{
		g_critical("Could not check for duplicates for invalid %s object so assume it exists",
					G_OBJECT_TYPE_NAME(inAppInfo));
		return(TRUE);
	}

	/* Iterate through favourites and check if already a favourite for
	 * requested desktop file exists.
	 */
	for(i=0; i<g_strv_length(priv->favourites); i++)
	{
		gchar						*value;
		GAppInfo					*valueAppInfo;

		valueAppInfo=NULL;

		/* Get favourite value and the string it contains */
		value=priv->favourites[i];
		if(value)
		{
			/* Get application information for string */
			if(g_path_is_absolute(value))
			{
				valueAppInfo=xfdashboard_desktop_app_info_new_from_path(value);
			}
				else
				{
					valueAppInfo=xfdashboard_application_database_lookup_desktop_id(priv->appDB, value);
				}
		}

		/* Check if favourite value matches application information */
		if(valueAppInfo &&
			g_app_info_equal(valueAppInfo, inAppInfo))
		{

			/* Release allocated resources */
			if(valueAppInfo) g_object_unref(valueAppInfo);

			return(TRUE);
		}

		/* Release allocated resources */
		if(valueAppInfo) g_object_unref(valueAppInfo);
	}

	/* If we get here then this quicklaunch does not have any item
	 * which matches the requested application information so return FALSE.
	 */
	return(FALSE);
}

/* An application icon (favourite) in quicklaunch was clicked */
static void _xfdashboard_quicklaunch_on_favourite_clicked(XfdashboardQuicklaunch *self, gpointer inUserData)
{
	XfdashboardQuicklaunchPrivate			*priv;
	XfdashboardApplicationButton			*button;
	gboolean								launchNewInstance;

	g_return_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self));
	g_return_if_fail(XFDASHBOARD_IS_APPLICATION_BUTTON(inUserData));

	priv=self->priv;
	button=XFDASHBOARD_APPLICATION_BUTTON(inUserData);

	/* If user wants to activate the last active windows for a running instance
	 * of application whose button was clicked, then check if a window exists
	 * and activate it. Otherwise launch a new instance.
	 */
	launchNewInstance=xfdashboard_settings_get_always_launch_new_instance(priv->settings);
	if(!launchNewInstance)
	{
		GAppInfo							*appInfo;
		const GList							*windows;
		XfdashboardWindowTrackerWindow		*lastActiveWindow;
		XfdashboardWindowTrackerWorkspace	*lastActiveWorkspace;

		/* Get application information of application button */
		appInfo=xfdashboard_application_button_get_app_info(button);
		if(!appInfo)
		{
			xfdashboard_notify(CLUTTER_ACTOR(self),
								"dialog-error",
								_("Launching application '%s' failed: %s"),
								xfdashboard_application_button_get_display_name(button),
								_("No information available for application"));

			g_warning("Launching application '%s' failed: %s",
						xfdashboard_application_button_get_display_name(button),
						"No information available for application");

			return;
		}

		/* Get list of windows for application */
		windows=xfdashboard_application_tracker_get_window_list_by_app_info(priv->appTracker, appInfo);
		if(windows)
		{
			/* Get last active window for application which is the first one in list */
			lastActiveWindow=XFDASHBOARD_WINDOW_TRACKER_WINDOW(windows->data);

			/* If last active window for application if available, wwitch to workspace
			 * where it is placed at and activate it.
			 */
			if(lastActiveWindow)
			{
				/* Switch to workspace where window is placed at */
				lastActiveWorkspace=xfdashboard_window_tracker_window_get_workspace(lastActiveWindow);
				xfdashboard_window_tracker_workspace_activate(lastActiveWorkspace);

				/* Like macOS: bring all (not minimized) windows of the application
				 * on that workspace to front, oldest first so the last active one
				 * ends up on top
				 */
				{
					const GList						*iter;

					for(iter=g_list_last((GList*)windows); iter; iter=g_list_previous(iter))
					{
						XfdashboardWindowTrackerWindow	*window=XFDASHBOARD_WINDOW_TRACKER_WINDOW(iter->data);

						if(window==lastActiveWindow) continue;
						if(xfdashboard_window_tracker_window_get_workspace(window)!=lastActiveWorkspace) continue;
						if(xfdashboard_window_tracker_window_get_state(window) &
							(XFDASHBOARD_WINDOW_TRACKER_WINDOW_STATE_MINIMIZED | XFDASHBOARD_WINDOW_TRACKER_WINDOW_STATE_SKIP_TASKLIST))
						{
							continue;
						}
						xfdashboard_window_tracker_window_activate(window);
					}
				}

				/* Activate window */
				xfdashboard_window_tracker_window_activate(lastActiveWindow);

				/* Activating last active window of application seems to be successfully
				 * so request core to quit.
				 */
				xfdashboard_core_quit(NULL);

				return;
			}
		}

		/* If we get here we found the application but no active window,
		 * so check if application is running. If it is display a warning
		 * message as notification and return from this function. If it
		 * is not running, continue to start a new instance.
		 */
		if(xfdashboard_application_tracker_is_running_by_app_info(priv->appTracker, appInfo))
		{
			xfdashboard_notify(CLUTTER_ACTOR(self),
								"dialog-error",
								_("Launching application '%s' failed: %s"),
								xfdashboard_application_button_get_display_name(button),
								_("No windows to activate for application"));

			g_warning("Launching application '%s' failed: %s",
						xfdashboard_application_button_get_display_name(button),
						"No windows to activate for application");

			return;
		}
	}

	/* Launch a new instance of application whose button was clicked */
	if(xfdashboard_application_button_execute(button, NULL))
	{
		/* Launching application seems to be successfully so request core to quit */
		xfdashboard_core_quit(NULL);

		return;
	}
}

/* User selected to open a new window or to launch that application at pop-up menu */
static void _xfdashboard_quicklaunch_on_favourite_popup_menu_item_launch(XfdashboardPopupMenuItem *inMenuItem,
																			gpointer inUserData)
{
	GAppInfo							*appInfo;
	XfdashboardApplicationTracker		*appTracker;
	GIcon								*gicon;
	const gchar							*iconName;

	g_return_if_fail(XFDASHBOARD_IS_POPUP_MENU_ITEM(inMenuItem));
	g_return_if_fail(G_IS_APP_INFO(inUserData));

	appInfo=G_APP_INFO(inUserData);
	iconName=NULL;

	/* Get icon of application */
	gicon=g_app_info_get_icon(appInfo);
	if(gicon) iconName=g_icon_to_string(gicon);

	/* Check if we should launch that application or to open a new window */
	appTracker=xfdashboard_core_get_application_tracker(NULL);
	if(!xfdashboard_application_tracker_is_running_by_app_info(appTracker, appInfo))
	{
		GAppLaunchContext			*context;
		GError						*error;

		/* Create context to start application at */
		context=xfdashboard_create_app_context(NULL);

		/* Try to launch application */
		error=NULL;
		if(!g_app_info_launch(appInfo, NULL, context, &error))
		{
			/* Show notification about failed application launch */
			xfdashboard_notify(CLUTTER_ACTOR(inMenuItem),
								iconName,
								_("Launching application '%s' failed: %s"),
								g_app_info_get_display_name(appInfo),
								(error && error->message) ? error->message : _("unknown error"));
			g_warning("Launching application '%s' failed: %s",
						g_app_info_get_display_name(appInfo),
						(error && error->message) ? error->message : "unknown error");
			if(error) g_error_free(error);
		}
			else
			{
				/* Show notification about successful application launch */
				xfdashboard_notify(CLUTTER_ACTOR(inMenuItem),
									iconName,
									_("Application '%s' launched"),
									g_app_info_get_display_name(appInfo));

				/* Emit signal for successful application launch */
				g_signal_emit_by_name(xfdashboard_core_get_default(), "application-launched", appInfo);

				/* Request core to quit */
				xfdashboard_core_quit(NULL);
			}

		/* Release allocated resources */
		g_object_unref(context);
	}

	/* Release allocated resources */
	g_object_unref(appTracker);
	g_object_unref(gicon);
}

/* User selected to remove application from favourites via pop-up menu */
static void _xfdashboard_quicklaunch_on_favourite_popup_menu_item_remove_from_favourite(XfdashboardPopupMenuItem *inMenuItem,
																						gpointer inUserData)
{
	XfdashboardApplicationButton		*appButton;
	GAppInfo							*appInfo;
	ClutterActor						*actor;
	XfdashboardQuicklaunch				*self;
	XfdashboardQuicklaunchPrivate		*priv;

	g_return_if_fail(XFDASHBOARD_IS_POPUP_MENU_ITEM(inMenuItem));
	g_return_if_fail(XFDASHBOARD_IS_APPLICATION_BUTTON(inUserData));

	appButton=XFDASHBOARD_APPLICATION_BUTTON(inUserData);

	/* Find quicklaunch for application button */
	actor=CLUTTER_ACTOR(appButton);
	do
	{
		actor=clutter_actor_get_parent(actor);
	}
	while(actor && !XFDASHBOARD_IS_QUICKLAUNCH(actor));

	if(!actor)
	{
		g_critical("Cannot find quicklaunch for application button.");
		return;
	}

	self=XFDASHBOARD_QUICKLAUNCH(actor);
	priv=self->priv;

	/* Notify about removal of favourite icon */
	xfdashboard_notify(CLUTTER_ACTOR(self),
						xfdashboard_application_button_get_icon_name(appButton),
						_("Favourite '%s' removed"),
						xfdashboard_application_button_get_display_name(appButton));

	/* Emit signal and re-add removed favourite as dynamically added
	 * application button for non-favourites apps when it is still running.
	 */
	appInfo=xfdashboard_application_button_get_app_info(appButton);
	if(appInfo)
	{
		/* Emit signal */
		g_signal_emit(self, XfdashboardQuicklaunchSignals[SIGNAL_FAVOURITE_REMOVED], 0, appInfo);

		/* Re-add removed favourite as dynamically added application button
		 * for non-favourites apps when it is still running.
		 */
		if(xfdashboard_application_tracker_is_running_by_app_info(priv->appTracker, appInfo))
		{
			ClutterActor				*newAppButton;

			newAppButton=_xfdashboard_quicklaunch_create_dynamic_actor(self, appInfo);
			clutter_actor_show(newAppButton);
			clutter_actor_add_child(CLUTTER_ACTOR(self), newAppButton);
		}
	}

	/* Destroy favourite icon before updating property */
	xfdashboard_actor_destroy(CLUTTER_ACTOR(appButton));

	/* Update favourites from icon order */
	_xfdashboard_quicklaunch_update_property_from_icons(self);
}

/* User selected to add application to favourites via pop-up menu */
static void _xfdashboard_quicklaunch_on_favourite_popup_menu_item_add_to_favourite(XfdashboardPopupMenuItem *inMenuItem,
																					gpointer inUserData)
{
	XfdashboardApplicationButton		*appButton;
	GAppInfo							*appInfo;
	ClutterActor						*actor;
	XfdashboardQuicklaunch				*self;
	XfdashboardQuicklaunchPrivate		*priv;

	g_return_if_fail(XFDASHBOARD_IS_POPUP_MENU_ITEM(inMenuItem));
	g_return_if_fail(XFDASHBOARD_IS_APPLICATION_BUTTON(inUserData));

	appButton=XFDASHBOARD_APPLICATION_BUTTON(inUserData);

	/* Find quicklaunch for application button */
	actor=CLUTTER_ACTOR(appButton);
	do
	{
		actor=clutter_actor_get_parent(actor);
	}
	while(actor && !XFDASHBOARD_IS_QUICKLAUNCH(actor));

	if(!actor)
	{
		g_critical("Cannot find quicklaunch for application button.");
		return;
	}

	self=XFDASHBOARD_QUICKLAUNCH(actor);
	priv=self->priv;

	/* Check if application button provides all information needed to add favourite
	 * and also check for duplicates.
	 */
	appInfo=xfdashboard_application_button_get_app_info(XFDASHBOARD_APPLICATION_BUTTON(appButton));
	if(appInfo &&
		!_xfdashboard_quicklaunch_has_favourite_appinfo(self, appInfo))
	{
		ClutterActor					*favouriteActor;

		/* If an actor for current selection to add to favourites already exists,
		 * destroy and remove it regardless if it an actor or a favourite app or
		 * dynamic non-favourite app. It will be re-added later.
		 */
		favouriteActor=_xfdashboard_quicklaunch_get_actor_for_appinfo(self, appInfo);
		if(favouriteActor)
		{
			/* Remove existing actor from this quicklaunch */
			xfdashboard_actor_destroy(favouriteActor);
		}

		/* Now (re-)add current selection to favourites but hidden as
		 * it will become visible and properly set up when function
		 * _xfdashboard_quicklaunch_update_property_from_icons is called.
		 */
		favouriteActor=xfdashboard_application_button_new_from_app_info(appInfo);
		clutter_actor_hide(favouriteActor);
		xfdashboard_stylable_add_class(XFDASHBOARD_STYLABLE(favouriteActor), "favourite-app");
		clutter_actor_insert_child_below(CLUTTER_ACTOR(self), favouriteActor, priv->separatorFavouritesToDynamic);

		/* Update favourites from icon order */
		_xfdashboard_quicklaunch_update_property_from_icons(self);

		/* Notify about new favourite */
		xfdashboard_notify(CLUTTER_ACTOR(self),
							xfdashboard_application_button_get_icon_name(XFDASHBOARD_APPLICATION_BUTTON(favouriteActor)),
							_("Favourite '%s' added"),
							xfdashboard_application_button_get_display_name(XFDASHBOARD_APPLICATION_BUTTON(favouriteActor)));

		g_signal_emit(self, XfdashboardQuicklaunchSignals[SIGNAL_FAVOURITE_ADDED], 0, appInfo);
	}
}

/* A right-click might have happened on an application icon (favourite) in quicklaunch */
static void _xfdashboard_quicklaunch_on_favourite_popup_menu(XfdashboardQuicklaunch *self,
																ClutterActor *inActor,
																gpointer inUserData)
{
	XfdashboardQuicklaunchPrivate				*priv;
	XfdashboardApplicationButton				*appButton;
	XfdashboardClickAction						*action;

	g_return_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self));
	g_return_if_fail(XFDASHBOARD_IS_APPLICATION_BUTTON(inActor));
	g_return_if_fail(XFDASHBOARD_IS_CLICK_ACTION(inUserData));

	priv=self->priv;
	appButton=XFDASHBOARD_APPLICATION_BUTTON(inActor);
	action=XFDASHBOARD_CLICK_ACTION(inUserData);

	/* Check if right button was used when the application button was clicked */
	if(xfdashboard_click_action_get_button(action)==XFDASHBOARD_CLICK_ACTION_RIGHT_BUTTON)
	{
		ClutterActor							*popup;
		ClutterActor							*menuItem;
		GAppInfo								*appInfo;

		/* Get app info for application button as it is needed most the time */
		appInfo=xfdashboard_application_button_get_app_info(appButton);
		if(!appInfo)
		{
			g_critical("No application information available for clicked application button.");
			return;
		}

		/* Create pop-up menu */
		popup=xfdashboard_popup_menu_new_for_source(CLUTTER_ACTOR(self));
		xfdashboard_popup_menu_set_destroy_on_cancel(XFDASHBOARD_POPUP_MENU(popup), TRUE);
		xfdashboard_popup_menu_set_title(XFDASHBOARD_POPUP_MENU(popup), g_app_info_get_display_name(appInfo));
		xfdashboard_popup_menu_set_title_gicon(XFDASHBOARD_POPUP_MENU(popup), g_app_info_get_icon(appInfo));

		/* Add each open window to pop-up of application */
		if(xfdashboard_application_button_add_popup_menu_items_for_windows(appButton, XFDASHBOARD_POPUP_MENU(popup))>0)
		{
			/* Add a separator to split windows from other actions in pop-up menu */
			menuItem=xfdashboard_popup_menu_item_separator_new();
			clutter_actor_set_x_expand(menuItem, TRUE);
			xfdashboard_popup_menu_add_item(XFDASHBOARD_POPUP_MENU(popup), XFDASHBOARD_POPUP_MENU_ITEM(menuItem));
		}

		/* Add menu item to launch application if it is not running */
		if(!xfdashboard_application_tracker_is_running_by_app_info(priv->appTracker, appInfo))
		{
			menuItem=xfdashboard_popup_menu_item_button_new();
			xfdashboard_label_set_text(XFDASHBOARD_LABEL(menuItem), _("Launch"));
			clutter_actor_set_x_expand(menuItem, TRUE);
			xfdashboard_popup_menu_add_item(XFDASHBOARD_POPUP_MENU(popup), XFDASHBOARD_POPUP_MENU_ITEM(menuItem));

			g_signal_connect(menuItem,
								"activated",
								G_CALLBACK(_xfdashboard_quicklaunch_on_favourite_popup_menu_item_launch),
								appInfo);
		}

		/* Add application actions */
		if(xfdashboard_application_button_add_popup_menu_items_for_actions(appButton, XFDASHBOARD_POPUP_MENU(popup))>0)
		{
			/* Add a separator to split windows from other actions in pop-up menu */
			menuItem=xfdashboard_popup_menu_item_separator_new();
			clutter_actor_set_x_expand(menuItem, TRUE);
			xfdashboard_popup_menu_add_item(XFDASHBOARD_POPUP_MENU(popup), XFDASHBOARD_POPUP_MENU_ITEM(menuItem));
		}

		/* Add "Remove from favourites" if application button is for a favourite application */
		if(xfdashboard_stylable_has_class(XFDASHBOARD_STYLABLE(appButton), "favourite-app"))
		{
			menuItem=xfdashboard_popup_menu_item_button_new();
			xfdashboard_label_set_text(XFDASHBOARD_LABEL(menuItem), _("Remove from favourites"));
			clutter_actor_set_x_expand(menuItem, TRUE);
			xfdashboard_popup_menu_add_item(XFDASHBOARD_POPUP_MENU(popup), XFDASHBOARD_POPUP_MENU_ITEM(menuItem));

			g_signal_connect(menuItem,
								"activated",
								G_CALLBACK(_xfdashboard_quicklaunch_on_favourite_popup_menu_item_remove_from_favourite),
								appButton);
		}

		/* Add "Add to favourites" if application button is for a non-favourite application */
		if(xfdashboard_stylable_has_class(XFDASHBOARD_STYLABLE(appButton), "dynamic-app"))
		{
			menuItem=xfdashboard_popup_menu_item_button_new();
			xfdashboard_label_set_text(XFDASHBOARD_LABEL(menuItem), _("Add to favourites"));
			clutter_actor_set_x_expand(menuItem, TRUE);
			xfdashboard_popup_menu_add_item(XFDASHBOARD_POPUP_MENU(popup), XFDASHBOARD_POPUP_MENU_ITEM(menuItem));

			g_signal_connect(menuItem,
								"activated",
								G_CALLBACK(_xfdashboard_quicklaunch_on_favourite_popup_menu_item_add_to_favourite),
								appButton);
		}

		/* Activate pop-up menu */
		xfdashboard_popup_menu_activate(XFDASHBOARD_POPUP_MENU(popup));
	}
}

/* Drag of a quicklaunch icon begins */
static void _xfdashboard_quicklaunch_on_favourite_drag_begin(ClutterDragAction *inAction,
																ClutterActor *inActor,
																gfloat inStageX,
																gfloat inStageY,
																ClutterModifierType inModifiers,
																gpointer inUserData)
{
	XfdashboardQuicklaunch			*self;
	XfdashboardQuicklaunchPrivate	*priv;
	ClutterActor					*dragHandle;
	ClutterStage					*stage;
	GAppInfo						*appInfo;

	g_return_if_fail(CLUTTER_IS_DRAG_ACTION(inAction));
	g_return_if_fail(XFDASHBOARD_IS_APPLICATION_BUTTON(inActor));
	g_return_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(inUserData));

	self=XFDASHBOARD_QUICKLAUNCH(inUserData);
	priv=self->priv;

	/* Prevent signal "clicked" from being emitted on dragged icon */
	g_signal_handlers_block_by_func(inActor, _xfdashboard_quicklaunch_on_favourite_clicked, inUserData);

	/* Get stage */
	stage=CLUTTER_STAGE(clutter_actor_get_stage(inActor));

	/* Create a clone of application icon for drag handle and hide it
	 * initially. It is only shown if pointer is outside of quicklaunch.
	 */
	appInfo=xfdashboard_application_button_get_app_info(XFDASHBOARD_APPLICATION_BUTTON(inActor));

	dragHandle=xfdashboard_application_button_new_from_app_info(appInfo);
	clutter_actor_set_position(dragHandle, inStageX, inStageY);
	xfdashboard_label_set_icon_size(XFDASHBOARD_LABEL(dragHandle), priv->normalIconSize);
	xfdashboard_label_set_sync_icon_size(XFDASHBOARD_LABEL(dragHandle), FALSE);
	xfdashboard_label_set_style(XFDASHBOARD_LABEL(dragHandle), XFDASHBOARD_LABEL_STYLE_ICON);
	clutter_actor_add_child(CLUTTER_ACTOR(stage), dragHandle);

	clutter_drag_action_set_drag_handle(inAction, dragHandle);
}

/* Drag of a quicklaunch icon ends */
static void _xfdashboard_quicklaunch_on_favourite_drag_end(ClutterDragAction *inAction,
															ClutterActor *inActor,
															gfloat inStageX,
															gfloat inStageY,
															ClutterModifierType inModifiers,
															gpointer inUserData)
{
	ClutterActor					*dragHandle;

	g_return_if_fail(CLUTTER_IS_DRAG_ACTION(inAction));
	g_return_if_fail(XFDASHBOARD_IS_APPLICATION_BUTTON(inActor));
	g_return_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(inUserData));

	/* Destroy clone of application icon used as drag handle */
	dragHandle=clutter_drag_action_get_drag_handle(inAction);
	if(dragHandle)
	{
		clutter_drag_action_set_drag_handle(inAction, NULL);
		xfdashboard_actor_destroy(dragHandle);
	}

	/* Allow signal "clicked" from being emitted again */
	g_signal_handlers_unblock_by_func(inActor, _xfdashboard_quicklaunch_on_favourite_clicked, inUserData);
}

/* Drag of an actor to quicklaunch as drop target begins */
static gboolean _xfdashboard_quicklaunch_on_drop_begin(XfdashboardQuicklaunch *self,
														XfdashboardDragAction *inDragAction,
														gpointer inUserData)
{
	XfdashboardQuicklaunchPrivate	*priv;
	ClutterActor					*dragSource;
	ClutterActor					*draggedActor;
	GAppInfo						*appInfo;

	g_return_val_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self), FALSE);
	g_return_val_if_fail(XFDASHBOARD_IS_DRAG_ACTION(inDragAction), FALSE);
	g_return_val_if_fail(XFDASHBOARD_IS_DROP_ACTION(inUserData), FALSE);

	priv=self->priv;

	/* Get source where dragging started and actor being dragged */
	dragSource=xfdashboard_drag_action_get_source(inDragAction);
	draggedActor=xfdashboard_drag_action_get_actor(inDragAction);

	/* Check if we can handle dragged actor from given source */
	priv->dragMode=DRAG_MODE_NONE;

	if(XFDASHBOARD_IS_QUICKLAUNCH(dragSource) &&
		XFDASHBOARD_IS_APPLICATION_BUTTON(draggedActor) &&
		xfdashboard_application_button_get_app_info(XFDASHBOARD_APPLICATION_BUTTON(draggedActor)))
	{
		priv->dragMode=DRAG_MODE_MOVE_EXISTING;
	}

	if(!XFDASHBOARD_IS_QUICKLAUNCH(dragSource) &&
		XFDASHBOARD_IS_APPLICATION_BUTTON(draggedActor) &&
		xfdashboard_application_button_get_app_info(XFDASHBOARD_APPLICATION_BUTTON(draggedActor)))
	{
		gboolean					isExistingItem;

		isExistingItem=FALSE;

		/* Get application information of item which should be added */
		appInfo=xfdashboard_application_button_get_app_info(XFDASHBOARD_APPLICATION_BUTTON(draggedActor));
		if(appInfo)
		{
			isExistingItem=_xfdashboard_quicklaunch_has_favourite_appinfo(self, appInfo);
			if(!isExistingItem) priv->dragMode=DRAG_MODE_CREATE;
		}
	}

	/* Create a visible copy of dragged application button and insert it
	 * after dragged icon in quicklaunch. This one is the one which is
	 * moved within quicklaunch. It is used as preview how quicklaunch
	 * will look like if drop will be successful. It is also needed to
	 * restore original order of all favourite icons if drag was
	 * cancelled by just destroying this preview icon.
	 * If dragging an favourite icon we will hide the it and leave it
	 * untouched when drag is cancelled.
	 */
	if(priv->dragMode!=DRAG_MODE_NONE)
	{
		appInfo=xfdashboard_application_button_get_app_info(XFDASHBOARD_APPLICATION_BUTTON(draggedActor));

		priv->dragPreviewIcon=xfdashboard_application_button_new_from_app_info(appInfo);
		xfdashboard_label_set_icon_size(XFDASHBOARD_LABEL(priv->dragPreviewIcon), priv->normalIconSize);
		xfdashboard_label_set_sync_icon_size(XFDASHBOARD_LABEL(priv->dragPreviewIcon), FALSE);
		xfdashboard_label_set_style(XFDASHBOARD_LABEL(priv->dragPreviewIcon), XFDASHBOARD_LABEL_STYLE_ICON);
		if(priv->dragMode==DRAG_MODE_CREATE) clutter_actor_hide(priv->dragPreviewIcon);
		clutter_actor_add_child(CLUTTER_ACTOR(self), priv->dragPreviewIcon);

		if(priv->dragMode==DRAG_MODE_MOVE_EXISTING)
		{
			clutter_actor_set_child_below_sibling(CLUTTER_ACTOR(self),
													priv->dragPreviewIcon,
													draggedActor);
			clutter_actor_hide(draggedActor);
		}
	}

	/* Hide all dynamically added application button for non-favourite apps */
	if(priv->dragMode!=DRAG_MODE_NONE)
	{
		ClutterActorIter			iter;
		ClutterActor				*child;

		clutter_actor_iter_init(&iter, CLUTTER_ACTOR(self));
		while(clutter_actor_iter_next(&iter, &child))
		{
			/* Only check application buttons */
			if(!XFDASHBOARD_IS_APPLICATION_BUTTON(child)) continue;

			/* If actor is an application button for non-favourite apps,
			 * hide it now.
			 */
			if(xfdashboard_stylable_has_class(XFDASHBOARD_STYLABLE(child), "dynamic-app"))
			{
				clutter_actor_hide(child);
			}
		}
	}

	/* If drag mode is set return TRUE because we can handle dragged actor
	 * otherwise return FALSE
	 */
	return(priv->dragMode!=DRAG_MODE_NONE ? TRUE : FALSE);
}

/* Dragged actor was dropped on this drop target */
static void _xfdashboard_quicklaunch_on_drop_drop(XfdashboardQuicklaunch *self,
													XfdashboardDragAction *inDragAction,
													gfloat inX,
													gfloat inY,
													gpointer inUserData)
{
	XfdashboardQuicklaunchPrivate		*priv;
	ClutterActor						*draggedActor;
	ClutterActorIter					iter;
	ClutterActor						*child;

	g_return_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self));
	g_return_if_fail(XFDASHBOARD_IS_DRAG_ACTION(inDragAction));
	g_return_if_fail(XFDASHBOARD_IS_DROP_ACTION(inUserData));

	priv=self->priv;

	/* Get dragged actor */
	draggedActor=xfdashboard_drag_action_get_actor(inDragAction);

	/* Remove dynamically added non-favourite application buttons and
	 * emit signal when a favourite icon was added.
	 */
	if(priv->dragMode==DRAG_MODE_CREATE)
	{
		GAppInfo						*appInfo;
		ClutterActor					*actor;

		xfdashboard_notify(CLUTTER_ACTOR(self),
							xfdashboard_application_button_get_icon_name(XFDASHBOARD_APPLICATION_BUTTON(draggedActor)),
							_("Favourite '%s' added"),
							xfdashboard_application_button_get_display_name(XFDASHBOARD_APPLICATION_BUTTON(draggedActor)));

		appInfo=xfdashboard_application_button_get_app_info(XFDASHBOARD_APPLICATION_BUTTON(draggedActor));
		if(appInfo)
		{
			/* Remove any application button marked as dynamically added for non-favourite
			 * apps for the newly added favourite if available.
			 */
			actor=_xfdashboard_quicklaunch_get_actor_for_appinfo(self, appInfo);
			if(actor) xfdashboard_actor_destroy(actor);

			/* Emit signal for newly added favourite */
			g_signal_emit(self, XfdashboardQuicklaunchSignals[SIGNAL_FAVOURITE_ADDED], 0, appInfo);
		}

		/* Set CSS class for favourite to get it included when property is updated
		 * in function _xfdashboard_quicklaunch_update_property_from_icons.
		 */
		xfdashboard_stylable_add_class(XFDASHBOARD_STYLABLE(priv->dragPreviewIcon), "favourite-app");
	}

	/* If drag mode is reorder move originally dragged application icon
	 * to its final position and destroy preview application icon.
	 */
	if(priv->dragMode==DRAG_MODE_MOVE_EXISTING)
	{
		clutter_actor_set_child_below_sibling(CLUTTER_ACTOR(self),
												draggedActor,
												priv->dragPreviewIcon);
		clutter_actor_show(draggedActor);

		if(priv->dragPreviewIcon)
		{
			xfdashboard_actor_destroy(priv->dragPreviewIcon);
			priv->dragPreviewIcon=NULL;
		}
	}

	/* Show (remaining) hidden application buttons for non-favourite apps again */
	clutter_actor_iter_init(&iter, CLUTTER_ACTOR(self));
	while(clutter_actor_iter_next(&iter, &child))
	{
		/* Only check application buttons */
		if(!XFDASHBOARD_IS_APPLICATION_BUTTON(child)) continue;

		/* If actor is an application button for non-favourite apps,
		 * show it now.
		 */
		if(xfdashboard_stylable_has_class(XFDASHBOARD_STYLABLE(child), "dynamic-app"))
		{
			clutter_actor_show(child);
		}
	}

	/* Update favourites from icon order */
	_xfdashboard_quicklaunch_update_property_from_icons(self);

	/* Reset drag mode */
	priv->dragMode=DRAG_MODE_NONE;
}

/* Drag of an actor to this drop target ended without actor being dropped here */
static void _xfdashboard_quicklaunch_on_drop_end(XfdashboardQuicklaunch *self,
													XfdashboardDragAction *inDragAction,
													gpointer inUserData)
{
	XfdashboardQuicklaunchPrivate		*priv;
	ClutterActor						*draggedActor;
	ClutterActorIter					iter;
	ClutterActor						*child;

	g_return_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self));
	g_return_if_fail(XFDASHBOARD_IS_DRAG_ACTION(inDragAction));
	g_return_if_fail(XFDASHBOARD_IS_DROP_ACTION(inUserData));

	priv=self->priv;

	/* Get dragged actor */
	draggedActor=xfdashboard_drag_action_get_actor(inDragAction);

	/* Show hidden application buttons for non-favourite apps again */
	clutter_actor_iter_init(&iter, CLUTTER_ACTOR(self));
	while(clutter_actor_iter_next(&iter, &child))
	{
		/* Only check application buttons */
		if(!XFDASHBOARD_IS_APPLICATION_BUTTON(child)) continue;

		/* If actor is an application button for non-favourite apps,
		 * show it now.
		 */
		if(xfdashboard_stylable_has_class(XFDASHBOARD_STYLABLE(child), "dynamic-app"))
		{
			clutter_actor_show(child);
		}
	}

	/* Destroy preview icon and show originally dragged favourite icon.
	 * Doing it this way will restore the order of favourite icons.
	 */
	if(priv->dragPreviewIcon)
	{
		xfdashboard_actor_destroy(priv->dragPreviewIcon);
		priv->dragPreviewIcon=NULL;
	}

	if(priv->dragMode==DRAG_MODE_MOVE_EXISTING) clutter_actor_show(draggedActor);
	priv->dragMode=DRAG_MODE_NONE;
}

/* Drag of an actor entered this drop target */
static void _xfdashboard_quicklaunch_on_drop_enter(XfdashboardQuicklaunch *self,
													XfdashboardDragAction *inDragAction,
													gpointer inUserData)
{
	ClutterActor					*dragHandle;

	g_return_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self));
	g_return_if_fail(XFDASHBOARD_IS_DRAG_ACTION(inDragAction));
	g_return_if_fail(XFDASHBOARD_IS_DROP_ACTION(inUserData));

	/* Get source where dragging started and actor being dragged */
	dragHandle=clutter_drag_action_get_drag_handle(CLUTTER_DRAG_ACTION(inDragAction));

	/* Hide drag handle in this quicklaunch */
	clutter_actor_hide(dragHandle);
}

/* Drag of an actor moves over this drop target */
static void _xfdashboard_quicklaunch_on_drop_motion(XfdashboardQuicklaunch *self,
													XfdashboardDragAction *inDragAction,
													gfloat inX,
													gfloat inY,
													gpointer inUserData)
{
	XfdashboardQuicklaunchPrivate	*priv;
	ClutterActor					*draggedActor;
	ClutterActor					*dragHandle;

	g_return_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self));
	g_return_if_fail(XFDASHBOARD_IS_DRAG_ACTION(inDragAction));
	g_return_if_fail(XFDASHBOARD_IS_DROP_ACTION(inUserData));

	priv=self->priv;

	/* Get actor being dragged and the drag handle */
	draggedActor=xfdashboard_drag_action_get_actor(inDragAction);
	dragHandle=clutter_drag_action_get_drag_handle(CLUTTER_DRAG_ACTION(inDragAction));

	/* Get new position of preview application icon in quicklaunch
	 * if dragged actor is an application icon
	 */
	if(XFDASHBOARD_IS_APPLICATION_BUTTON(draggedActor))
	{
		ClutterStage				*stage;
		gboolean					oldPreviewReactive;
		gboolean					oldHandleReactive;
		gfloat						stageX, stageY;
		gfloat						deltaX, deltaY;
		ClutterActor				*actorUnderMouse;
		ClutterActorIter			iter;
		ClutterActor				*iterChild;

		/* Preview icon and drag handle should not be reactive to prevent
		 * clutter_stage_get_actor_at_pos() choosing one of both as the
		 * actor under mouse. But remember their state to reset it later.
		 */
		oldPreviewReactive=clutter_actor_get_reactive(priv->dragPreviewIcon);
		clutter_actor_set_reactive(priv->dragPreviewIcon, FALSE);

		oldHandleReactive=clutter_actor_get_reactive(dragHandle);
		clutter_actor_set_reactive(dragHandle, FALSE);

		/* Get new position and move preview icon */
		clutter_drag_action_get_motion_coords(CLUTTER_DRAG_ACTION(inDragAction), &stageX, &stageY);
		xfdashboard_drag_action_get_motion_delta(inDragAction, &deltaX, &deltaY);

		stage=CLUTTER_STAGE(clutter_actor_get_stage(dragHandle));
		actorUnderMouse=clutter_stage_get_actor_at_pos(stage, CLUTTER_PICK_REACTIVE, stageX, stageY);
		if(XFDASHBOARD_IS_APPLICATION_BUTTON(actorUnderMouse) &&
			actorUnderMouse!=priv->dragPreviewIcon)
		{
			if((priv->orientation==CLUTTER_ORIENTATION_HORIZONTAL && deltaX<0) ||
				(priv->orientation==CLUTTER_ORIENTATION_VERTICAL && deltaY<0))
			{
				clutter_actor_set_child_below_sibling(CLUTTER_ACTOR(self),
														priv->dragPreviewIcon,
														actorUnderMouse);
			}
				else
				{
					clutter_actor_set_child_above_sibling(CLUTTER_ACTOR(self),
															priv->dragPreviewIcon,
															actorUnderMouse);
				}

			/* Show preview icon now if drag mode is "new". Doing it earlier will
			 * show preview icon at wrong position when entering quicklaunch
			 */
			if(priv->dragMode==DRAG_MODE_CREATE) clutter_actor_show(priv->dragPreviewIcon);

			/* Iterate through list of current actors and enable allocation animation */
			clutter_actor_iter_init(&iter, CLUTTER_ACTOR(self));
			if((priv->orientation==CLUTTER_ORIENTATION_HORIZONTAL && deltaX<0) ||
				(priv->orientation==CLUTTER_ORIENTATION_VERTICAL && deltaY<0))
			{
				while(clutter_actor_iter_next(&iter, &iterChild) && iterChild!=priv->dragPreviewIcon)
				{
					if(XFDASHBOARD_IS_ACTOR(iterChild))
					{
						xfdashboard_actor_enable_allocation_animation_once(XFDASHBOARD_ACTOR(iterChild));
					}
				}
			}
				else
				{
					while(clutter_actor_iter_next(&iter, &iterChild) && iterChild!=priv->dragPreviewIcon);
					while(clutter_actor_iter_next(&iter, &iterChild))
					{
						if(XFDASHBOARD_IS_ACTOR(iterChild))
						{
							xfdashboard_actor_enable_allocation_animation_once(XFDASHBOARD_ACTOR(iterChild));
						}
					}
				}
		}

		/* Reset reactive state of preview icon and drag handle */
		clutter_actor_set_reactive(priv->dragPreviewIcon, oldPreviewReactive);
		clutter_actor_set_reactive(dragHandle, oldHandleReactive);
	}
}

/* Drag of an actor entered this drop target */
static void _xfdashboard_quicklaunch_on_drop_leave(XfdashboardQuicklaunch *self,
													XfdashboardDragAction *inDragAction,
													gpointer inUserData)
{
	XfdashboardQuicklaunchPrivate	*priv;
	ClutterActor					*dragHandle;

	g_return_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self));
	g_return_if_fail(XFDASHBOARD_IS_DRAG_ACTION(inDragAction));
	g_return_if_fail(XFDASHBOARD_IS_DROP_ACTION(inUserData));

	priv=self->priv;

	/* Get source where dragging started and drag handle */
	dragHandle=clutter_drag_action_get_drag_handle(CLUTTER_DRAG_ACTION(inDragAction));

	/* Show drag handle outside this quicklaunch */
	clutter_actor_show(dragHandle);

	/* Hide preview icon if drag mode is "create new" favourite */
	if(priv->dragMode==DRAG_MODE_CREATE) clutter_actor_hide(priv->dragPreviewIcon);
}

/* Drag of an actor to trash drop target begins */
static gboolean _xfdashboard_quicklaunch_on_trash_drop_begin(XfdashboardQuicklaunch *self,
																XfdashboardDragAction *inDragAction,
																gpointer inUserData)
{
	XfdashboardQuicklaunchPrivate	*priv;
	ClutterActor					*dragSource;
	ClutterActor					*draggedActor;

	g_return_val_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self), FALSE);
	g_return_val_if_fail(XFDASHBOARD_IS_DRAG_ACTION(inDragAction), FALSE);
	g_return_val_if_fail(XFDASHBOARD_IS_DROP_ACTION(inUserData), FALSE);

	priv=self->priv;

	/* Get source where dragging started and actor being dragged */
	dragSource=xfdashboard_drag_action_get_source(inDragAction);
	draggedActor=xfdashboard_drag_action_get_actor(inDragAction);

	/* Check if we can handle dragged actor from given source */
	if(XFDASHBOARD_IS_QUICKLAUNCH(dragSource) &&
		XFDASHBOARD_IS_APPLICATION_BUTTON(draggedActor))
	{
		/* Dragged actor is a favourite icon from quicklaunch. So hide
		 * "applications" button and show an unhighlited trash button instead.
		 * Trash button will be hidden and "applications" button shown again
		 * when drag ends.
		 */
		clutter_actor_hide(priv->appsButton);
		clutter_actor_show(priv->trashButton);

		return(TRUE);
	}

	/* If we get here we cannot handle dragged actor at trash drop target */
	return(FALSE);
}

/* Dragged actor was dropped on trash drop target */
static void _xfdashboard_quicklaunch_on_trash_drop_drop(XfdashboardQuicklaunch *self,
															XfdashboardDragAction *inDragAction,
															gfloat inX,
															gfloat inY,
															gpointer inUserData)
{
	XfdashboardQuicklaunchPrivate	*priv;
	ClutterActor					*draggedActor;
	GAppInfo						*appInfo;

	g_return_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self));
	g_return_if_fail(XFDASHBOARD_IS_DRAG_ACTION(inDragAction));
	g_return_if_fail(XFDASHBOARD_IS_DROP_ACTION(inUserData));

	priv=self->priv;

	/* Get dragged favourite icon */
	draggedActor=xfdashboard_drag_action_get_actor(inDragAction);

	/* Notify about removal of favourite icon */
	xfdashboard_notify(CLUTTER_ACTOR(self),
						xfdashboard_application_button_get_icon_name(XFDASHBOARD_APPLICATION_BUTTON(draggedActor)),
						_("Favourite '%s' removed"),
						xfdashboard_application_button_get_display_name(XFDASHBOARD_APPLICATION_BUTTON(draggedActor)));

	/* Emit signal and re-add removed favourite as dynamically added
	 * application button for non-favourites apps when it is still running.
	 */
	appInfo=xfdashboard_application_button_get_app_info(XFDASHBOARD_APPLICATION_BUTTON(draggedActor));
	if(appInfo)
	{
		/* Emit signal */
		g_signal_emit(self, XfdashboardQuicklaunchSignals[SIGNAL_FAVOURITE_REMOVED], 0, appInfo);

		/* Re-add removed favourite as dynamically added application button
		 * for non-favourites apps when it is still running.
		 */
		if(xfdashboard_application_tracker_is_running_by_app_info(priv->appTracker, appInfo))
		{
			ClutterActor			*actor;

			actor=_xfdashboard_quicklaunch_create_dynamic_actor(self, appInfo);
			clutter_actor_show(actor);
			clutter_actor_add_child(CLUTTER_ACTOR(self), actor);
		}
	}

	/* Destroy dragged favourite icon before updating property */
	xfdashboard_actor_destroy(draggedActor);

	/* Destroy preview icon before updating property */
	if(priv->dragPreviewIcon)
	{
		xfdashboard_actor_destroy(priv->dragPreviewIcon);
		priv->dragPreviewIcon=NULL;
	}

	/* Show "applications" button again and hide trash button instead */
	clutter_actor_hide(priv->trashButton);
	clutter_actor_show(priv->appsButton);

	/* Update favourites from icon order */
	_xfdashboard_quicklaunch_update_property_from_icons(self);

	/* Reset drag mode */
	priv->dragMode=DRAG_MODE_NONE;
}

/* Drag of an actor to trash drop target ended without actor being dropped here */
static void _xfdashboard_quicklaunch_on_trash_drop_end(XfdashboardQuicklaunch *self,
														XfdashboardDragAction *inDragAction,
														gpointer inUserData)
{
	XfdashboardQuicklaunchPrivate	*priv;

	g_return_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self));
	g_return_if_fail(XFDASHBOARD_IS_DRAG_ACTION(inDragAction));
	g_return_if_fail(XFDASHBOARD_IS_DROP_ACTION(inUserData));

	priv=self->priv;

	/* Show "applications" button again and hide trash button instead */
	clutter_actor_hide(priv->trashButton);
	clutter_actor_show(priv->appsButton);
}

/* Drag of an actor entered trash drop target */
static void _xfdashboard_quicklaunch_on_trash_drop_enter(XfdashboardQuicklaunch *self,
															XfdashboardDragAction *inDragAction,
															gpointer inUserData)
{
	XfdashboardQuicklaunchPrivate	*priv;

	g_return_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self));
	g_return_if_fail(XFDASHBOARD_IS_DRAG_ACTION(inDragAction));
	g_return_if_fail(XFDASHBOARD_IS_DROP_ACTION(inUserData));

	priv=self->priv;

	/* Set toggle state on trash drop target as dragged actor is over it */
	xfdashboard_toggle_button_set_toggle_state(XFDASHBOARD_TOGGLE_BUTTON(priv->trashButton), TRUE);
}

/* Drag of an actor leaves trash drop target */
static void _xfdashboard_quicklaunch_on_trash_drop_leave(XfdashboardQuicklaunch *self,
															XfdashboardDragAction *inDragAction,
															gpointer inUserData)
{
	XfdashboardQuicklaunchPrivate	*priv;

	g_return_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self));
	g_return_if_fail(XFDASHBOARD_IS_DRAG_ACTION(inDragAction));
	g_return_if_fail(XFDASHBOARD_IS_DROP_ACTION(inUserData));

	priv=self->priv;

	/* Set toggle state on trash drop target as dragged actor is over it */
	xfdashboard_toggle_button_set_toggle_state(XFDASHBOARD_TOGGLE_BUTTON(priv->trashButton), FALSE);
}

/* A tooltip for a favourite will be activated */
static void _xfdashboard_quicklaunch_on_tooltip_activating(ClutterAction *inAction, gpointer inUserData)
{
	XfdashboardTooltipAction		*action;
	XfdashboardApplicationButton	*button;

	g_return_if_fail(XFDASHBOARD_IS_TOOLTIP_ACTION(inAction));
	g_return_if_fail(XFDASHBOARD_IS_APPLICATION_BUTTON(inUserData));

	action=XFDASHBOARD_TOOLTIP_ACTION(inAction);
	button=XFDASHBOARD_APPLICATION_BUTTON(inUserData);

	/* Update tooltip text to reflect favourites current display name */
	xfdashboard_tooltip_action_set_text(action, xfdashboard_application_button_get_display_name(button));
}

/* Create actor for a dynamically added non-favourite application */
static ClutterActor* _xfdashboard_quicklaunch_create_dynamic_actor(XfdashboardQuicklaunch *self,
																	GAppInfo *inAppInfo)
{
	XfdashboardQuicklaunchPrivate	*priv;
	ClutterActor					*actor;
	ClutterAction					*action;

	g_return_val_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self), NULL);
	g_return_val_if_fail(G_IS_APP_INFO(inAppInfo), NULL);

	priv=self->priv;

	/* Create and set up actor */
	actor=xfdashboard_application_button_new_from_app_info(inAppInfo);
	xfdashboard_label_set_icon_size(XFDASHBOARD_LABEL(actor), priv->normalIconSize);
	xfdashboard_label_set_sync_icon_size(XFDASHBOARD_LABEL(actor), FALSE);
	xfdashboard_label_set_style(XFDASHBOARD_LABEL(actor), XFDASHBOARD_LABEL_STYLE_ICON);
	xfdashboard_stylable_add_class(XFDASHBOARD_STYLABLE(actor), "dynamic-app");

	/* Set up and add click action */
	g_signal_connect_swapped(actor, "clicked", G_CALLBACK(_xfdashboard_quicklaunch_on_favourite_clicked), self);

	/* Set up and add pop-up menu click action */
	action=xfdashboard_click_action_new();
	g_signal_connect_swapped(action, "clicked", G_CALLBACK(_xfdashboard_quicklaunch_on_favourite_popup_menu), self);
	clutter_actor_add_action(actor, action);

	/* Set up and add tooltip action */
	action=xfdashboard_tooltip_action_new();
	g_signal_connect(action, "activating", G_CALLBACK(_xfdashboard_quicklaunch_on_tooltip_activating), actor);
	clutter_actor_add_action(actor, action);

	/* Return newly created actor */
	return(actor);
}

/* Create actor for a favourite application */
static ClutterActor* _xfdashboard_quicklaunch_create_favourite_actor(XfdashboardQuicklaunch *self,
																		GAppInfo *inAppInfo)
{
	XfdashboardQuicklaunchPrivate	*priv;
	ClutterActor					*actor;
	ClutterAction					*action;

	g_return_val_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self), NULL);
	g_return_val_if_fail(G_IS_APP_INFO(inAppInfo), NULL);

	priv=self->priv;

	/* Create and set up actor */
	actor=xfdashboard_application_button_new_from_app_info(inAppInfo);
	xfdashboard_label_set_icon_size(XFDASHBOARD_LABEL(actor), priv->normalIconSize);
	xfdashboard_label_set_sync_icon_size(XFDASHBOARD_LABEL(actor), FALSE);
	xfdashboard_label_set_style(XFDASHBOARD_LABEL(actor), XFDASHBOARD_LABEL_STYLE_ICON);
	xfdashboard_stylable_add_class(XFDASHBOARD_STYLABLE(actor), "favourite-app");

	/* Set up and add click action */
	g_signal_connect_swapped(actor, "clicked", G_CALLBACK(_xfdashboard_quicklaunch_on_favourite_clicked), self);

	/* Set up and add pop-up menu click action */
	action=xfdashboard_click_action_new();
	g_signal_connect_swapped(action, "clicked", G_CALLBACK(_xfdashboard_quicklaunch_on_favourite_popup_menu), self);
	clutter_actor_add_action(actor, action);

	/* Set up and add drag'n'drop action */
	action=xfdashboard_drag_action_new_with_source(CLUTTER_ACTOR(self));
	clutter_drag_action_set_drag_threshold(CLUTTER_DRAG_ACTION(action), -1, -1);
	clutter_actor_add_action(actor, action);
	g_signal_connect(action, "drag-begin", G_CALLBACK(_xfdashboard_quicklaunch_on_favourite_drag_begin), self);
	g_signal_connect(action, "drag-end", G_CALLBACK(_xfdashboard_quicklaunch_on_favourite_drag_end), self);

	/* Set up and add tooltip action */
	action=xfdashboard_tooltip_action_new();
	g_signal_connect(action, "activating", G_CALLBACK(_xfdashboard_quicklaunch_on_tooltip_activating), actor);
	clutter_actor_add_action(actor, action);

	/* Return newly created actor */
	return(actor);
}

/* Update property from icons in quicklaunch */
static void _xfdashboard_quicklaunch_update_property_from_icons(XfdashboardQuicklaunch *self)
{
	XfdashboardQuicklaunchPrivate	*priv;
	ClutterActor					*child;
	ClutterActorIter				iter;
	GAppInfo						*desktopAppInfo;
	gchar							*desktopFile;
	GArray							*newFavourites;

	g_return_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self));

	priv=self->priv;

	/* Free current list of desktop files */
	if(priv->favourites)
	{
		g_strfreev(priv->favourites);
		priv->favourites=NULL;
	}

	/* Create array of strings pointing to desktop files for new order,
	 * update quicklaunch and store settings
	 */
	newFavourites=g_array_new(TRUE, TRUE, sizeof(gchar*));

	clutter_actor_iter_init(&iter, CLUTTER_ACTOR(self));
	while(clutter_actor_iter_next(&iter, &child))
	{
		desktopFile=NULL;

		/* Keep position of applications button (only needed if not first) */
		if(child==priv->appsButton)
		{
			if(clutter_actor_get_previous_sibling(child))
			{
				desktopFile=g_strdup(XFDASHBOARD_QUICKLAUNCH_APPS_ENTRY);
				g_array_append_val(newFavourites, desktopFile);
			}
			continue;
		}

		/* Keep dividers at their place in the list */
		if(g_object_get_data(G_OBJECT(child), XFDASHBOARD_QUICKLAUNCH_DIVIDER_KEY))
		{
			desktopFile=g_strdup(XFDASHBOARD_QUICKLAUNCH_DIVIDER_ENTRY);
			g_array_append_val(newFavourites, desktopFile);
			continue;
		}

		/* Only add desktop file if it is an application button for
		 * a favourite and provides a desktop ID or desktop file name
		 * and is not going to be destroyed
		 */
 		if(!XFDASHBOARD_IS_APPLICATION_BUTTON(child)) continue;
		if(xfdashboard_stylable_has_class(XFDASHBOARD_STYLABLE(child), "destroying")) continue;
		if(!xfdashboard_stylable_has_class(XFDASHBOARD_STYLABLE(child), "favourite-app")) continue;

		desktopAppInfo=xfdashboard_application_button_get_app_info(XFDASHBOARD_APPLICATION_BUTTON(child));
		if(desktopAppInfo &&
			XFDASHBOARD_IS_DESKTOP_APP_INFO(desktopAppInfo))
		{
			desktopFile=g_strdup(g_app_info_get_id(desktopAppInfo));
			if(!desktopFile)
			{
				GFile				*file;

				file=xfdashboard_desktop_app_info_get_file(XFDASHBOARD_DESKTOP_APP_INFO(desktopAppInfo));
				if(file) desktopFile=g_file_get_path(file);
			}
		}
		if(!desktopFile) continue;

		/* Add desktop file name to array but do not free memory allocated
		 * by desktop file name as it will stored in array and will not
		 * be duplicated.
		 */
		g_array_append_val(newFavourites, desktopFile);
	}

	/* Store new favourites list */
	priv->favourites=(gchar**)(gpointer)g_array_free(newFavourites, FALSE);

	/* Notify about property change */
	g_object_notify_by_pspec(G_OBJECT(self), XfdashboardQuicklaunchProperties[PROP_FAVOURITES]);
}

/* Folders and trash copied from Plank are pinned items which stay at the end
 * of the dock, after running applications (like "anchor files/docklets" in
 * Plank and like the macOS dock). They are desktop files generated in
 * ~/.local/share/xfdashboard/dock/ by plank-to-xfdashboard.py.
 */
static gboolean _xfdashboard_quicklaunch_is_anchored(ClutterActor *inActor)
{
	GAppInfo		*appInfo;
	GFile			*file;
	gchar			*path;
	gboolean		result=FALSE;

	if(!XFDASHBOARD_IS_APPLICATION_BUTTON(inActor)) return(FALSE);
	if(!xfdashboard_stylable_has_class(XFDASHBOARD_STYLABLE(inActor), "favourite-app")) return(FALSE);

	appInfo=xfdashboard_application_button_get_app_info(XFDASHBOARD_APPLICATION_BUTTON(inActor));
	if(!appInfo || !XFDASHBOARD_IS_DESKTOP_APP_INFO(appInfo)) return(FALSE);

	file=xfdashboard_desktop_app_info_get_file(XFDASHBOARD_DESKTOP_APP_INFO(appInfo));
	path=(file ? g_file_get_path(file) : NULL);
	if(path && strstr(path, "/xfdashboard/dock/")) result=TRUE;
	g_free(path);
	return(result);
}

static void _xfdashboard_quicklaunch_move_anchored_to_end(XfdashboardQuicklaunch *self)
{
	XfdashboardQuicklaunchPrivate	*priv=self->priv;
	ClutterActor					*child;
	GList							*pending=NULL, *move=NULL, *iter;

	/* Only for themes that opt in with "plank-style: true;" */
	if(!self->priv->plankStyle) return;

	for(child=clutter_actor_get_first_child(CLUTTER_ACTOR(self)); child; child=clutter_actor_get_next_sibling(child))
	{
		if(child==priv->separatorFavouritesToDynamic) continue;

		if(g_object_get_data(G_OBJECT(child), XFDASHBOARD_QUICKLAUNCH_DIVIDER_KEY))
		{
			pending=g_list_append(pending, child);
			continue;
		}

		if(_xfdashboard_quicklaunch_is_anchored(child))
		{
			move=g_list_concat(move, pending);
			pending=NULL;
			move=g_list_append(move, child);
			continue;
		}

		/* Ignore helpers which are not dock items */
		if(!clutter_actor_is_visible(child) && !XFDASHBOARD_IS_APPLICATION_BUTTON(child)) continue;

		g_list_free(pending);
		pending=NULL;
	}
	g_list_free(pending);

	for(iter=move; iter; iter=g_list_next(iter))
	{
		clutter_actor_set_child_above_sibling(CLUTTER_ACTOR(self), CLUTTER_ACTOR(iter->data), NULL);
	}
	if(move) clutter_actor_queue_relayout(CLUTTER_ACTOR(self));
	g_list_free(move);
}

static gboolean _xfdashboard_quicklaunch_on_windows_changed_idle(gpointer inUserData)
{
	XfdashboardQuicklaunch			*self=XFDASHBOARD_QUICKLAUNCH(inUserData);

	self->priv->windowsChangedIdleID=0;
	clutter_actor_queue_redraw(CLUTTER_ACTOR(self));
	return(G_SOURCE_REMOVE);
}

static void _xfdashboard_quicklaunch_on_windows_changed(XfdashboardQuicklaunch *self, gpointer inWindow)
{
	if(!self->priv->windowsChangedIdleID)
	{
		self->priv->windowsChangedIdleID=g_idle_add(_xfdashboard_quicklaunch_on_windows_changed_idle, self);
	}
}

static void _xfdashboard_quicklaunch_on_actor_added(ClutterActor *inActor, ClutterActor *inChild, gpointer inUserData)
{
	XfdashboardQuicklaunch			*self=XFDASHBOARD_QUICKLAUNCH(inActor);

	if(XFDASHBOARD_IS_STYLABLE(inChild) &&
		xfdashboard_stylable_has_class(XFDASHBOARD_STYLABLE(inChild), "dynamic-app"))
	{
		_xfdashboard_quicklaunch_move_anchored_to_end(self);
	}
}

/* Update icons in quicklaunch from property */
static void _xfdashboard_quicklaunch_update_icons_from_property(XfdashboardQuicklaunch *self)
{
	XfdashboardQuicklaunchPrivate	*priv;
	ClutterActor					*child;
	ClutterActorIter				iter;
	guint							i;
	ClutterActor					*actor;
	GAppInfo						*currentSelectionAppInfo;
	const gchar						*desktopFilename;
	GAppInfo						*appInfo;

	g_return_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self));

	priv=self->priv;
	currentSelectionAppInfo=NULL;

	/* If current selection is an application button then remember it
	 * to reselect it after favourites are re-setup.
	 */
	if(priv->selectedItem &&
		XFDASHBOARD_IS_APPLICATION_BUTTON(priv->selectedItem))
	{
		currentSelectionAppInfo=xfdashboard_application_button_get_app_info(XFDASHBOARD_APPLICATION_BUTTON(priv->selectedItem));

		XFDASHBOARD_DEBUG(self, ACTOR,
							"Going to destroy current selection %p (%s) for desktop ID '%s'",
							priv->selectedItem, G_OBJECT_TYPE_NAME(priv->selectedItem),
							g_app_info_get_id(xfdashboard_application_button_get_app_info(XFDASHBOARD_APPLICATION_BUTTON(priv->selectedItem))));
	}

	/* Remove all application buttons */
	clutter_actor_iter_init(&iter, CLUTTER_ACTOR(self));
	while(clutter_actor_iter_next(&iter, &child))
	{
		if((XFDASHBOARD_IS_APPLICATION_BUTTON(child) &&
				xfdashboard_stylable_has_class(XFDASHBOARD_STYLABLE(child), "favourite-app")) ||
			g_object_get_data(G_OBJECT(child), XFDASHBOARD_QUICKLAUNCH_DIVIDER_KEY))
		{
			clutter_actor_iter_destroy(&iter);
		}
	}

	/* Applications button starts at the beginning unless favourites say otherwise */
	if(priv->appsButton)
	{
		clutter_actor_set_child_below_sibling(CLUTTER_ACTOR(self), priv->appsButton, NULL);
		if(priv->trashButton) clutter_actor_set_child_above_sibling(CLUTTER_ACTOR(self), priv->trashButton, priv->appsButton);
	}

	/* Now re-add all application icons for current favourites */
	for(i=0; i<g_strv_length(priv->favourites); i++)
	{
		/* Get desktop file to create application button for in quicklaunch */
		desktopFilename=priv->favourites[i];

		/* Applications button entry: place it here (e.g. right of Finder like
		 * on macOS) instead of at the start of the dock
		 */
		if(g_strcmp0(desktopFilename, XFDASHBOARD_QUICKLAUNCH_APPS_ENTRY)==0)
		{
			if(priv->appsButton && priv->plankStyle)
			{
				clutter_actor_set_child_below_sibling(CLUTTER_ACTOR(self), priv->appsButton, priv->separatorFavouritesToDynamic);
				if(priv->trashButton) clutter_actor_set_child_above_sibling(CLUTTER_ACTOR(self), priv->trashButton, priv->appsButton);
			}
			continue;
		}

		/* Divider entry */
		if(g_strcmp0(desktopFilename, XFDASHBOARD_QUICKLAUNCH_DIVIDER_ENTRY)==0)
		{
			actor=clutter_actor_new();
			g_object_set_data(G_OBJECT(actor), XFDASHBOARD_QUICKLAUNCH_DIVIDER_KEY, GINT_TO_POINTER(1));
			clutter_actor_hide(actor);
			clutter_actor_insert_child_below(CLUTTER_ACTOR(self), actor, priv->separatorFavouritesToDynamic);
			continue;
		}

		if(g_path_is_absolute(desktopFilename)) appInfo=xfdashboard_desktop_app_info_new_from_path(desktopFilename);
			else
			{
				appInfo=xfdashboard_application_database_lookup_desktop_id(priv->appDB, desktopFilename);
				if(!appInfo) appInfo=xfdashboard_desktop_app_info_new_from_desktop_id(desktopFilename);
			}

		/* If we could not get application information for desktop file, do not
		 * create the application button.
		 */
		if(!appInfo) continue;

		/* Create application button from desktop file */
		actor=_xfdashboard_quicklaunch_create_favourite_actor(self, appInfo);
		clutter_actor_show(actor);
		clutter_actor_insert_child_below(CLUTTER_ACTOR(self), actor, priv->separatorFavouritesToDynamic);

		/* Select this item if it matches the previously selected item
		 * which was destroyed in the meantime.
		 */
		if(currentSelectionAppInfo)
		{
			/* Check if newly created application button matches current selection
			 * then reselect newly create actor as current selection.
			 */
			if(g_app_info_equal(G_APP_INFO(appInfo), currentSelectionAppInfo))
			{
				xfdashboard_focusable_set_selection(XFDASHBOARD_FOCUSABLE(self), actor);

				XFDASHBOARD_DEBUG(self, ACTOR,
									"Select newly created actor %p (%s) because it matches desktop ID '%s'",
									actor, G_OBJECT_TYPE_NAME(actor),
									g_app_info_get_id(xfdashboard_application_button_get_app_info(XFDASHBOARD_APPLICATION_BUTTON(actor))));
			}
		}

		/* Release allocated resources */
		g_object_unref(appInfo);
	}

	/* Keep folders/trash after running applications */
	_xfdashboard_quicklaunch_move_anchored_to_end(self);
}

/* Set up favourites array from string array value */
static void _xfdashboard_quicklaunch_set_favourites(XfdashboardQuicklaunch *self, const GValue *inValue)
{
	XfdashboardQuicklaunchPrivate	*priv;
	guint							i;
	gchar							**desktopFiles;
	guint							numberElements;

	g_return_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self));
	g_return_if_fail(G_IS_VALUE(inValue));

	priv=self->priv;

	/* Free current list of favourites */
	if(priv->favourites)
	{
		g_strfreev(priv->favourites);
		priv->favourites=NULL;
	}

	/* Copy new array of strings pointing to desktop files */
	desktopFiles=g_value_get_boxed(inValue);
	if(desktopFiles)
	{
		numberElements=g_strv_length(desktopFiles);
		priv->favourites=g_new0(gchar*, numberElements+1);
		for(i=0; i<numberElements; ++i)
		{
			priv->favourites[i]=g_strdup(desktopFiles[i]);
		}
	}
		else priv->favourites=g_new0(gchar*, 1);

	/* Update list of icons for desktop files */
	_xfdashboard_quicklaunch_update_icons_from_property(self);
}

/* Get scale factor to fit all children into given width */
/* Dividers like in the macOS dock (thin line with some extra room):
 * - where the favourites list contains a "separator" entry (Plank's separator
 *   docklet is copied there), and
 * - between pinned (favourite) and running but not pinned applications.
 * Dividers are hidden marker actors between the items. A divider is only
 * active if there are visible items on both sides and no other divider
 * directly before it.
 */

static gboolean _xfdashboard_quicklaunch_is_divider(XfdashboardQuicklaunch *self, ClutterActor *inActor)
{
	/* Dividers only for themes that opt in with "plank-style: true;" */
	if(!self->priv->plankStyle) return(FALSE);

	return(inActor==self->priv->separatorFavouritesToDynamic ||
			g_object_get_data(G_OBJECT(inActor), XFDASHBOARD_QUICKLAUNCH_DIVIDER_KEY)!=NULL);
}

static gfloat _xfdashboard_quicklaunch_get_divider_width(XfdashboardQuicklaunch *self)
{
	return(MAX(self->priv->spacing*2.0f, 16.0f));
}

/* Is divider active? If inDivider is NULL, return number of active dividers */
static gint _xfdashboard_quicklaunch_divider_active(XfdashboardQuicklaunch *self, ClutterActor *inDivider)
{
	XfdashboardQuicklaunchPrivate	*priv=self->priv;
	ClutterActor					*child, *next;
	gboolean						sawItem=FALSE, qualifies;
	gint							count=0;

	for(child=clutter_actor_get_first_child(CLUTTER_ACTOR(self)); child; child=clutter_actor_get_next_sibling(child))
	{
		if(!_xfdashboard_quicklaunch_is_divider(self, child))
		{
			if(clutter_actor_is_visible(child)) sawItem=TRUE;
			continue;
		}

		/* Check items following this divider */
		qualifies=FALSE;
		if(sawItem)
		{
			for(next=clutter_actor_get_next_sibling(child); next; next=clutter_actor_get_next_sibling(next))
			{
				if(_xfdashboard_quicklaunch_is_divider(self, next) || !clutter_actor_is_visible(next)) continue;
				if(child==priv->separatorFavouritesToDynamic)
				{
					ClutterActor	*other;

					qualifies=(XFDASHBOARD_IS_STYLABLE(next) &&
								xfdashboard_stylable_has_class(XFDASHBOARD_STYLABLE(next), "dynamic-app"));

					/* User placed own dividers (e.g. copied from Plank): running
					 * applications simply join the section before the last one
					 */
					for(other=clutter_actor_get_first_child(CLUTTER_ACTOR(self)); other && qualifies; other=clutter_actor_get_next_sibling(other))
					{
						if(g_object_get_data(G_OBJECT(other), XFDASHBOARD_QUICKLAUNCH_DIVIDER_KEY)) qualifies=FALSE;
					}
				}
					else qualifies=TRUE;
				break;
			}
		}

		if(qualifies)
		{
			count++;
			sawItem=FALSE;
		}
		if(child==inDivider) return(qualifies ? 1 : 0);
	}

	return(inDivider ? 0 : count);
}

/* Total extra room taken by active dividers */
static gfloat _xfdashboard_quicklaunch_get_separator_gap(XfdashboardQuicklaunch *self)
{
	return(_xfdashboard_quicklaunch_divider_active(self, NULL)*_xfdashboard_quicklaunch_get_divider_width(self));
}

static gfloat _xfdashboard_quicklaunch_get_scale_for_width(XfdashboardQuicklaunch *self,
															gfloat inForWidth,
															gboolean inDoMinimumSize)
{
	XfdashboardQuicklaunchPrivate	*priv;
	ClutterActor					*child;
	ClutterActorIter				iter;
	gint							numberChildren;
	gfloat							totalWidth, scalableWidth;
	gfloat							childWidth;
	gfloat							childMinWidth, childNaturalWidth;
	gfloat							scale;
	gboolean						recheckWidth;

	g_return_val_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self), 0.0f);
	g_return_val_if_fail(inForWidth>=0.0f, 0.0f);

	priv=self->priv;

	/* Count visible children and determine their total width */
	numberChildren=0;
	totalWidth=0.0f;
	clutter_actor_iter_init(&iter, CLUTTER_ACTOR(self));
	while(clutter_actor_iter_next(&iter, &child))
	{
		/* Only check visible children */
		if(!clutter_actor_is_visible(child)) continue;

		/* Get width of child */
		clutter_actor_get_preferred_width(child, -1, &childMinWidth, &childNaturalWidth);
		if(inDoMinimumSize==TRUE) childWidth=childMinWidth;
			else childWidth=childNaturalWidth;

		/* Determine total size so far */
		totalWidth+=ceil(childWidth);

		/* Count visible children */
		numberChildren++;
	}
	if(numberChildren==0) return(priv->scaleMax);

	/* Determine scalable width. That is the width without spacing
	 * between children and the spacing used as padding.
	 */
	scalableWidth=inForWidth-((numberChildren+1)*priv->spacing)-_xfdashboard_quicklaunch_get_separator_gap(self);

	/* Get scale factor */
	scale=priv->scaleMax;
	if(totalWidth>0.0f)
	{
		scale=floor((scalableWidth/totalWidth)/priv->scaleStep)*priv->scaleStep;
		scale=MIN(scale, priv->scaleMax);
		scale=MAX(scale, priv->scaleMin);
	}

	/* Check if all visible children would really fit into width
	 * otherwise we need to decrease scale factor one step down
	 */
	if(scale>priv->scaleMin)
	{
		do
		{
			recheckWidth=FALSE;
			totalWidth=priv->spacing+_xfdashboard_quicklaunch_get_separator_gap(self);

			/* Iterate through visible children and sum their scaled
			 * widths. The total width will be initialized with unscaled
			 * spacing and all visible children's scaled width will also
			 * be added with unscaled spacing to have the padding added.
			 */
			clutter_actor_iter_init(&iter, CLUTTER_ACTOR(self));
			while(clutter_actor_iter_next(&iter, &child))
			{
				/* Only check visible children */
				if(!clutter_actor_is_visible(child)) continue;

				/* Get scaled size of child and add to total width */
				clutter_actor_get_preferred_width(child, -1, &childMinWidth, &childNaturalWidth);
				if(inDoMinimumSize==TRUE) childWidth=childMinWidth;
					else childWidth=childNaturalWidth;

				childWidth*=scale;
				totalWidth+=ceil(childWidth)+priv->spacing;
			}

			/* If total width is greater than given width
			 * decrease scale factor by one step and recheck
			 */
			if(totalWidth>inForWidth && scale>priv->scaleMin)
			{
				scale-=priv->scaleStep;
				recheckWidth=TRUE;
			}
		}
		while(recheckWidth==TRUE);
	}

	/* Return found scale factor */
	return(scale);
}

/* Get scale factor to fit all children into given height */
static gfloat _xfdashboard_quicklaunch_get_scale_for_height(XfdashboardQuicklaunch *self,
															gfloat inForHeight,
															gboolean inDoMinimumSize)
{
	XfdashboardQuicklaunchPrivate	*priv;
	ClutterActor					*child;
	ClutterActorIter				iter;
	gint							numberChildren;
	gfloat							totalHeight, scalableHeight;
	gfloat							childHeight;
	gfloat							childMinHeight, childNaturalHeight;
	gfloat							scale;
	gboolean						recheckHeight;

	g_return_val_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self), 0.0f);
	g_return_val_if_fail(inForHeight>=0.0f, 0.0f);

	priv=self->priv;

	/* Count visible children and determine their total height */
	numberChildren=0;
	totalHeight=0.0f;
	clutter_actor_iter_init(&iter, CLUTTER_ACTOR(self));
	while(clutter_actor_iter_next(&iter, &child))
	{
		/* Only check visible children */
		if(!clutter_actor_is_visible(child)) continue;

		/* Get height of child */
		clutter_actor_get_preferred_height(child, -1, &childMinHeight, &childNaturalHeight);
		if(inDoMinimumSize==TRUE) childHeight=childMinHeight;
			else childHeight=childNaturalHeight;

		/* Determine total size so far */
		totalHeight+=ceil(childHeight);

		/* Count visible children */
		numberChildren++;
	}
	if(numberChildren==0) return(priv->scaleMax);

	/* Determine scalable height. That is the height without spacing
	 * between children and the spacing used as padding.
	 */
	scalableHeight=inForHeight-((numberChildren+1)*priv->spacing);

	/* Get scale factor */
	scale=priv->scaleMax;
	if(totalHeight>0.0f)
	{
		scale=floor((scalableHeight/totalHeight)/priv->scaleStep)*priv->scaleStep;
		scale=MIN(scale, priv->scaleMax);
		scale=MAX(scale, priv->scaleMin);
	}

	/* Check if all visible children would really fit into height
	 * otherwise we need to decrease scale factor one step down
	 */
	if(scale>priv->scaleMin)
	{
		do
		{
			recheckHeight=FALSE;
			totalHeight=priv->spacing;

			/* Iterate through visible children and sum their scaled
			 * heights. The total height will be initialized with unscaled
			 * spacing and all visible children's scaled height will also
			 * be added with unscaled spacing to have the padding added.
			 */
			clutter_actor_iter_init(&iter, CLUTTER_ACTOR(self));
			while(clutter_actor_iter_next(&iter, &child))
			{
				/* Only check visible children */
				if(!clutter_actor_is_visible(child)) continue;

				/* Get scaled size of child and add to total height */
				clutter_actor_get_preferred_height(child, -1, &childMinHeight, &childNaturalHeight);
				if(inDoMinimumSize==TRUE) childHeight=childMinHeight;
					else childHeight=childNaturalHeight;

				childHeight*=scale;
				totalHeight+=ceil(childHeight)+priv->spacing;
			}

			/* If total height is greater than given height
			 * decrease scale factor by one step and recheck
			 */
			if(totalHeight>inForHeight && scale>priv->scaleMin)
			{
				scale-=priv->scaleStep;
				recheckHeight=TRUE;
			}
		}
		while(recheckHeight==TRUE);
	}

	/* Return found scale factor */
	return(scale);
}

/* Get previous selectable actor in quicklaunch */
static ClutterActor* xfdashboard_quicklaunch_get_previous_selectable(XfdashboardQuicklaunch *self,
																		ClutterActor *inSelected)
{
	ClutterActorIter	iter;
	ClutterActor		*child;
	ClutterActor		*prevItem;

	g_return_val_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self), NULL);

	prevItem=NULL;

	/* Iterate through children and return previous selectable item after given one */
	clutter_actor_iter_init(&iter, CLUTTER_ACTOR(self));
	while(clutter_actor_iter_next(&iter, &child))
	{
		/* If this child is the lookup one return previous visible item seen */
		if(child==inSelected && prevItem) return(prevItem);

		/* If this child is visible but not the one we lookup remember it as previous one */
		if(clutter_actor_is_visible(child)) prevItem=child;
	}

	/* If we get here there is no selectable item after given one, so return last
	 * selectable item we have seen.
	 */
	return(prevItem);
}

/* Get next selectable actor in quicklaunch */
static ClutterActor* xfdashboard_quicklaunch_get_next_selectable(XfdashboardQuicklaunch *self,
																	ClutterActor *inSelected)
{
	ClutterActorIter	iter;
	ClutterActor		*child;
	gboolean			doLookup;

	g_return_val_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self), NULL);

	/* Iterate through children beginnig at current selected one and return next
	 * selectable item after that one.
	 */
	doLookup=FALSE;
	clutter_actor_iter_init(&iter, CLUTTER_ACTOR(self));
	while(clutter_actor_iter_next(&iter, &child))
	{
		/* If this child is the lookup one, lookup next visible item */
		if(child!=inSelected && doLookup==FALSE) continue;

		/* Return child if visible */
		if(doLookup && clutter_actor_is_visible(child)) return(child);

		/* If we get here we either found current selected one and we should
		 * look for next selectable item or we looked for next selectable item
		 * but it was not selectable. In both cases set flag to lookup for
		 * selectable items.
		 */
		doLookup=TRUE;
	}

	/* If we get here we are at the end of list of children and no one was selectable.
	 * Start over at the beginning of list of children up to the current selected one.
	 * Return the first visible item.
	 */
	clutter_actor_iter_init(&iter, CLUTTER_ACTOR(self));
	while(clutter_actor_iter_next(&iter, &child))
	{
		/* Stop at current selected one */
		if(child==inSelected) break;

		/* Return this child if visible */
		if(clutter_actor_is_visible(child)) return(child);
	}

	/* If we get here there is no selectable item was found, so return NULL */
	return(NULL);
}

/* Action signal to add current selected item as favourite was emitted */
static gboolean _xfdashboard_quicklaunch_selection_add_favourite(XfdashboardQuicklaunch *self,
																	XfdashboardFocusable *inSource,
																	const gchar *inAction,
																	ClutterEvent *inEvent)
{
	XfdashboardQuicklaunchPrivate	*priv;
	ClutterActor					*currentSelection;
	GAppInfo						*appInfo;

	g_return_val_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self), CLUTTER_EVENT_PROPAGATE);
	g_return_val_if_fail(XFDASHBOARD_IS_FOCUSABLE(inSource), CLUTTER_EVENT_PROPAGATE);
	g_return_val_if_fail(inEvent, CLUTTER_EVENT_PROPAGATE);

	priv=self->priv;

	/* Get current selection of focusable actor and check if it is an actor
	 * derived from XfdashboardApplicationButton.
	 */
	currentSelection=xfdashboard_focusable_get_selection(inSource);
	if(!currentSelection)
	{
		XFDASHBOARD_DEBUG(self, ACTOR,
							"Source actor %s has no selection and no favourite can be added.",
							G_OBJECT_TYPE_NAME(inSource));
		return(CLUTTER_EVENT_STOP);
	}

	if(!XFDASHBOARD_IS_APPLICATION_BUTTON(currentSelection))
	{
		XFDASHBOARD_DEBUG(self, ACTOR,
							"Current selection at source actor %s has type %s but only selections of type %s can be added.",
							G_OBJECT_TYPE_NAME(inSource),
							G_OBJECT_TYPE_NAME(currentSelection),
							g_type_name(XFDASHBOARD_TYPE_APPLICATION_BUTTON));
		return(CLUTTER_EVENT_STOP);
	}

	/* Check if application button provides all information needed to add favourite
	 * and also check for duplicates.
	 */
	appInfo=xfdashboard_application_button_get_app_info(XFDASHBOARD_APPLICATION_BUTTON(currentSelection));
	if(appInfo &&
		!_xfdashboard_quicklaunch_has_favourite_appinfo(self, appInfo))
	{
		ClutterActor		*favouriteActor;

		/* If an actor for current selection to add to favourites already exists,
		 * destroy and remove it regardless if it an actor or a favourite app or
		 * dynamic non-favourite app. It will be re-added later.
		 */
		favouriteActor=_xfdashboard_quicklaunch_get_actor_for_appinfo(self, appInfo);
		if(favouriteActor)
		{
			/* Remove existing actor from this quicklaunch */
			xfdashboard_actor_destroy(favouriteActor);
		}

		/* Now (re-)add current selection to favourites but hidden as
		 * it will become visible and properly set up when function
		 * _xfdashboard_quicklaunch_update_property_from_icons is called.
		 */
		favouriteActor=xfdashboard_application_button_new_from_app_info(appInfo);
		clutter_actor_hide(favouriteActor);
		xfdashboard_stylable_add_class(XFDASHBOARD_STYLABLE(favouriteActor), "favourite-app");
		clutter_actor_insert_child_below(CLUTTER_ACTOR(self), favouriteActor, priv->separatorFavouritesToDynamic);

		/* Update favourites from icon order */
		_xfdashboard_quicklaunch_update_property_from_icons(self);

		/* Notify about new favourite */
		xfdashboard_notify(CLUTTER_ACTOR(self),
							xfdashboard_application_button_get_icon_name(XFDASHBOARD_APPLICATION_BUTTON(favouriteActor)),
							_("Favourite '%s' added"),
							xfdashboard_application_button_get_display_name(XFDASHBOARD_APPLICATION_BUTTON(favouriteActor)));

		g_signal_emit(self, XfdashboardQuicklaunchSignals[SIGNAL_FAVOURITE_ADDED], 0, appInfo);
	}

	return(CLUTTER_EVENT_STOP);
}

/* Action signal to remove current selected item as favourite was emitted */
static gboolean _xfdashboard_quicklaunch_selection_remove_favourite(XfdashboardQuicklaunch *self,
																	XfdashboardFocusable *inSource,
																	const gchar *inAction,
																	ClutterEvent *inEvent)
{
	XfdashboardQuicklaunchPrivate	*priv;
	ClutterActor					*currentSelection;
	ClutterActor					*newSelection;
	GAppInfo						*appInfo;

	g_return_val_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self), CLUTTER_EVENT_PROPAGATE);
	g_return_val_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(inSource), CLUTTER_EVENT_PROPAGATE);
	g_return_val_if_fail(inEvent, CLUTTER_EVENT_PROPAGATE);

	priv=self->priv;

	/* If this binding action was not emitted on this quicklaunch
	 * then propagate event because there might be another quicklaunch
	 * which will handle it.
	 */
	if(XFDASHBOARD_QUICKLAUNCH(inSource)!=self) return(CLUTTER_EVENT_PROPAGATE);

	/* Check if current selection in this quicklaunch is a favourite */
	currentSelection=xfdashboard_focusable_get_selection(inSource);
	if(!currentSelection)
	{
		XFDASHBOARD_DEBUG(self, ACTOR,
							"Source actor %s has no selection so no favourite can be removed.",
							G_OBJECT_TYPE_NAME(inSource));
		return(CLUTTER_EVENT_STOP);
	}

	if(!XFDASHBOARD_IS_APPLICATION_BUTTON(currentSelection))
	{
		XFDASHBOARD_DEBUG(self, ACTOR,
							"Current selection at source actor %s has type %s but only selections of type %s can be removed.",
							G_OBJECT_TYPE_NAME(inSource),
							G_OBJECT_TYPE_NAME(currentSelection),
							g_type_name(XFDASHBOARD_TYPE_APPLICATION_BUTTON));
		return(CLUTTER_EVENT_STOP);
	}

	if(priv->dragPreviewIcon && currentSelection==priv->dragPreviewIcon)
	{
		XFDASHBOARD_DEBUG(self, ACTOR,
							"Current selection at source actor %s is %s which is the drag preview icon which cannot be removed.",
							G_OBJECT_TYPE_NAME(inSource),
							G_OBJECT_TYPE_NAME(currentSelection));
		return(CLUTTER_EVENT_STOP);
	}

	/* Get application information */
	appInfo=xfdashboard_application_button_get_app_info(XFDASHBOARD_APPLICATION_BUTTON(currentSelection));

	/* Notify about removed favourite */
	xfdashboard_notify(CLUTTER_ACTOR(self),
						xfdashboard_application_button_get_icon_name(XFDASHBOARD_APPLICATION_BUTTON(currentSelection)),
						_("Favourite '%s' removed"),
						xfdashboard_application_button_get_display_name(XFDASHBOARD_APPLICATION_BUTTON(currentSelection)));

	if(appInfo) g_signal_emit(self, XfdashboardQuicklaunchSignals[SIGNAL_FAVOURITE_REMOVED], 0, appInfo);

	/* Select previous or next actor in quicklaunch if the favourite
	 * going to be removed is the currently selected one.
	 */
	newSelection=clutter_actor_get_next_sibling(currentSelection);
	if(!newSelection) newSelection=clutter_actor_get_previous_sibling(currentSelection);
	if(!newSelection) newSelection=clutter_actor_get_last_child(CLUTTER_ACTOR(self));

	if(newSelection)
	{
		xfdashboard_focusable_set_selection(XFDASHBOARD_FOCUSABLE(self), newSelection);
	}

	/* Remove actor from this quicklaunch */
	xfdashboard_actor_destroy(currentSelection);

	/* Re-add removed favourite as dynamically added application button
	 * for non-favourites apps when it is still running.
	 */
	if(appInfo &&
		xfdashboard_application_tracker_is_running_by_app_info(priv->appTracker, appInfo))
	{
		ClutterActor			*actor;

		actor=_xfdashboard_quicklaunch_create_dynamic_actor(self, appInfo);
		clutter_actor_show(actor);
		clutter_actor_add_child(CLUTTER_ACTOR(self), actor);
	}

	/* Update favourites from icon order */
	_xfdashboard_quicklaunch_update_property_from_icons(self);

	/* Action handled */
	return(CLUTTER_EVENT_STOP);
}

/* Action signal to move current selected item to reorder items was emitted */
static gboolean _xfdashboard_quicklaunch_favourite_reorder_selection(XfdashboardQuicklaunch *self,
																		XfdashboardOrientation inDirection)
{
	XfdashboardQuicklaunchPrivate	*priv;
	ClutterActor					*currentSelection;
	ClutterActor					*newSelection;
	ClutterOrientation				orientation;

	g_return_val_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self), CLUTTER_EVENT_PROPAGATE);
	g_return_val_if_fail(inDirection<=XFDASHBOARD_ORIENTATION_BOTTOM, CLUTTER_EVENT_PROPAGATE);

	priv=self->priv;

	/* Determine expected orientation of quicklaunch */
	if(inDirection==XFDASHBOARD_ORIENTATION_LEFT ||
		inDirection==XFDASHBOARD_ORIENTATION_RIGHT)
	{
		orientation=CLUTTER_ORIENTATION_HORIZONTAL;
	}
		else orientation=CLUTTER_ORIENTATION_VERTICAL;

	/* Orientation of quicklaunch must match orientation of direction
	 * to which we should reorder item.
	 */
	if(priv->orientation!=orientation)
	{
		XFDASHBOARD_DEBUG(self, ACTOR,
							"Source actor %s does not have expected orientation.",
							G_OBJECT_TYPE_NAME(self));
		return(CLUTTER_EVENT_STOP);
	}

	/* Check if current selection in this quicklaunch is a favourite */
	currentSelection=xfdashboard_focusable_get_selection(XFDASHBOARD_FOCUSABLE(self));
	if(!currentSelection)
	{
		XFDASHBOARD_DEBUG(self, ACTOR,
							"Source actor %s has no selection so no favourite can be reordered.",
							G_OBJECT_TYPE_NAME(self));
		return(CLUTTER_EVENT_STOP);
	}

	if(!XFDASHBOARD_IS_APPLICATION_BUTTON(currentSelection))
	{
		XFDASHBOARD_DEBUG(self, ACTOR,
							"Current selection at source actor %s has type %s but only selections of type %s can be reordered.",
							G_OBJECT_TYPE_NAME(self),
							G_OBJECT_TYPE_NAME(currentSelection),
							g_type_name(XFDASHBOARD_TYPE_APPLICATION_BUTTON));
		return(CLUTTER_EVENT_STOP);
	}

	if(!xfdashboard_stylable_has_class(XFDASHBOARD_STYLABLE(currentSelection), "favourite-app"))
	{
		XFDASHBOARD_DEBUG(self, ACTOR,
							"Current selection at source actor %s is not a favourite and cannot be reordered.",
							G_OBJECT_TYPE_NAME(self));
		return(CLUTTER_EVENT_STOP);
	}

	if(priv->dragPreviewIcon && currentSelection==priv->dragPreviewIcon)
	{
		XFDASHBOARD_DEBUG(self, ACTOR,
							"Current selection at source actor %s is %s which is the drag preview icon which cannot be reordered.",
							G_OBJECT_TYPE_NAME(self),
							G_OBJECT_TYPE_NAME(currentSelection));
		return(CLUTTER_EVENT_STOP);
	}

	/* Find new position and check if current selection can be moved to this new position.
	 * The current selection cannot be moved if it is already at the beginning or the end
	 * and it cannot bypass an actor of type XfdashboardButton which is used by any
	 * non favourite actor like "switch" button, trash button etc. or ClutterActor used
	 * by separators. Favourites or dynamic non-favourites use actors of type
	 * XfdashboardApplicationButton. So each non-favourite button cannot get into favourites
	 * area and vice versa.
	 */
	if(inDirection==XFDASHBOARD_ORIENTATION_LEFT ||
		inDirection==XFDASHBOARD_ORIENTATION_TOP)
	{
		newSelection=clutter_actor_get_previous_sibling(currentSelection);
	}
		else newSelection=clutter_actor_get_next_sibling(currentSelection);

	if(!newSelection)
	{
		XFDASHBOARD_DEBUG(self, ACTOR,
							"Current selection %s at source actor %s is already at end of list",
							G_OBJECT_TYPE_NAME(currentSelection),
							G_OBJECT_TYPE_NAME(self));
		return(CLUTTER_EVENT_STOP);
	}

	if(!XFDASHBOARD_IS_APPLICATION_BUTTON(newSelection))
	{
		XFDASHBOARD_DEBUG(self, ACTOR,
							"Current selection %s at source actor %s cannot be moved because it is blocked by %s.",
							G_OBJECT_TYPE_NAME(currentSelection),
							G_OBJECT_TYPE_NAME(self),
							G_OBJECT_TYPE_NAME(newSelection));
		return(CLUTTER_EVENT_STOP);
	}

	/* Move current selection to new position */
	if(inDirection==XFDASHBOARD_ORIENTATION_LEFT ||
		inDirection==XFDASHBOARD_ORIENTATION_TOP)
	{
		clutter_actor_set_child_below_sibling(CLUTTER_ACTOR(self),
												currentSelection,
												newSelection);
	}
		else
		{
			clutter_actor_set_child_above_sibling(CLUTTER_ACTOR(self),
													currentSelection,
													newSelection);
		}

	/* Update favourites from icon order */
	_xfdashboard_quicklaunch_update_property_from_icons(self);

	/* Action handled */
	return(CLUTTER_EVENT_STOP);
}

static gboolean _xfdashboard_quicklaunch_favourite_reorder_left(XfdashboardQuicklaunch *self,
																XfdashboardFocusable *inSource,
																const gchar *inAction,
																ClutterEvent *inEvent)
{
	return(_xfdashboard_quicklaunch_favourite_reorder_selection(self, XFDASHBOARD_ORIENTATION_LEFT));
}

static gboolean _xfdashboard_quicklaunch_favourite_reorder_right(XfdashboardQuicklaunch *self,
																	XfdashboardFocusable *inSource,
																	const gchar *inAction,
																	ClutterEvent *inEvent)
{
	return(_xfdashboard_quicklaunch_favourite_reorder_selection(self, XFDASHBOARD_ORIENTATION_RIGHT));
}

/* Action signal to move current selected item to up (to reorder items) was emitted */
static gboolean _xfdashboard_quicklaunch_favourite_reorder_up(XfdashboardQuicklaunch *self,
																XfdashboardFocusable *inSource,
																const gchar *inAction,
																ClutterEvent *inEvent)
{
	return(_xfdashboard_quicklaunch_favourite_reorder_selection(self, XFDASHBOARD_ORIENTATION_TOP));
}

/* Action signal to move current selected item to down (to reorder items) was emitted */
static gboolean _xfdashboard_quicklaunch_favourite_reorder_down(XfdashboardQuicklaunch *self,
																XfdashboardFocusable *inSource,
																const gchar *inAction,
																ClutterEvent *inEvent)
{
	return(_xfdashboard_quicklaunch_favourite_reorder_selection(self, XFDASHBOARD_ORIENTATION_BOTTOM));
}

/* An application was started or quitted */
static void _xfdashboard_quicklaunch_on_app_tracker_state_changed(XfdashboardQuicklaunch *self,
																	const gchar *inDesktopID,
																	gboolean inIsRunning,
																	gpointer inUserData)
{
	XfdashboardQuicklaunchPrivate	*priv;
	GAppInfo						*appInfo;
	ClutterActor					*actor;

	g_return_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self));
	g_return_if_fail(XFDASHBOARD_IS_APPLICATION_TRACKER(inUserData));

	priv=self->priv;

	/* Get application information for desktop id */
	appInfo=xfdashboard_application_database_lookup_desktop_id(priv->appDB, inDesktopID);
	if(!appInfo)
	{
		XFDASHBOARD_DEBUG(self, APPLICATIONS,
							"Unknown desktop ID '%s' changed state to '%s'",
							inDesktopID,
							inIsRunning ? "running" : "stopped");
		return;
	}

	/* If application is now running and no actor exists for it,
	 * then create an application button but mark it as dynamically added.
	 */
	if(inIsRunning)
	{
		/* Find actor for application which changed running state */
		actor=_xfdashboard_quicklaunch_get_actor_for_appinfo(self, appInfo);

		/* Create application button, mark it as dynamically added and
		 * add it to quicklaunch.
		 */
		if(!actor)
		{
			actor=_xfdashboard_quicklaunch_create_dynamic_actor(self, appInfo);
			clutter_actor_show(actor);
			clutter_actor_add_child(CLUTTER_ACTOR(self), actor);

			XFDASHBOARD_DEBUG(self, ACTOR,
								"Created dynamic actor %p for newly running desktop ID '%s'",
								actor,
								inDesktopID);
		}
	}

	/* If application is now stopped and an actor exists for it and it is
	 * marked as dynamically added, then destroy it.
	 */
	if(!inIsRunning)
	{
		/* Find actor for application which changed running state */
		actor=_xfdashboard_quicklaunch_get_actor_for_appinfo(self, appInfo);

		/* If actor exists and is marked as dynamically added, destroy it */
		if(actor &&
			xfdashboard_stylable_has_class(XFDASHBOARD_STYLABLE(actor), "dynamic-app"))
		{
			XFDASHBOARD_DEBUG(self, ACTOR,
								"Destroying dynamic actor %p for stopped desktop ID '%s'",
								actor,
								inDesktopID);

			xfdashboard_actor_destroy(actor);
		}
	}
}

/* IMPLEMENTATION: ClutterActor */

/* Get preferred width/height */
static void _xfdashboard_quicklaunch_get_preferred_height(ClutterActor *inActor,
															gfloat inForWidth,
															gfloat *outMinHeight,
															gfloat *outNaturalHeight)
{
	XfdashboardQuicklaunch			*self=XFDASHBOARD_QUICKLAUNCH(inActor);
	XfdashboardQuicklaunchPrivate	*priv=self->priv;
	gfloat							minHeight, naturalHeight;
	ClutterActor					*child;
	ClutterActorIter				iter;
	gfloat							childMinHeight, childNaturalHeight;
	gint							numberChildren;
	gfloat							scale;

	/* Set up default values */
	minHeight=naturalHeight=0.0f;

	/* Determine height for horizontal orientation ... */
	if(priv->orientation==CLUTTER_ORIENTATION_HORIZONTAL)
	{
		/* Iterate through visible children and determine heights */
		numberChildren=0;
		clutter_actor_iter_init(&iter, CLUTTER_ACTOR(inActor));
		while(clutter_actor_iter_next(&iter, &child))
		{
			/* Only check visible children */
			if(!clutter_actor_is_visible(child)) continue;

			/* Get sizes of child */
			clutter_actor_get_preferred_height(child,
												-1,
												&childMinHeight,
												&childNaturalHeight);

			/* Determine heights */
			minHeight=MAX(minHeight, childMinHeight);
			naturalHeight=MAX(naturalHeight, childNaturalHeight);

			/* Count visible children */
			numberChildren++;
		}

		/* Check if we need to scale width because of the need to fit
		 * all visible children into given limiting width
		 */
		if(inForWidth>=0.0f)
		{
			scale=_xfdashboard_quicklaunch_get_scale_for_width(self, inForWidth, TRUE);
			minHeight*=scale;

			scale=_xfdashboard_quicklaunch_get_scale_for_width(self, inForWidth, FALSE);
			naturalHeight*=scale;
		}

		/* Add spacing as padding */
		if(numberChildren>0)
		{
			minHeight+=2*priv->spacing;
			naturalHeight+=2*priv->spacing;
		}
	}
		/* ... otherwise determine height for vertical orientation */
		else
		{
			/* Iterate through visible children and determine height */
			numberChildren=0;
			clutter_actor_iter_init(&iter, CLUTTER_ACTOR(inActor));
			while(clutter_actor_iter_next(&iter, &child))
			{
				/* Only check visible children */
				if(!clutter_actor_is_visible(child)) continue;

				/* Get child's size */
				clutter_actor_get_preferred_height(child,
													inForWidth,
													&childMinHeight,
													&childNaturalHeight);

				/* Determine heights */
				minHeight+=childMinHeight;
				naturalHeight+=childNaturalHeight;

				/* Count visible children */
				numberChildren++;
			}

			/* Add spacing between children and spacing as padding */
			if(numberChildren>0)
			{
				minHeight+=(numberChildren+1)*priv->spacing;
				naturalHeight+=(numberChildren+1)*priv->spacing;
			}
		}

	/* Store sizes computed */
	if(outMinHeight) *outMinHeight=minHeight;
	if(outNaturalHeight) *outNaturalHeight=naturalHeight;
}

static void _xfdashboard_quicklaunch_get_preferred_width(ClutterActor *inActor,
															gfloat inForHeight,
															gfloat *outMinWidth,
															gfloat *outNaturalWidth)
{
	XfdashboardQuicklaunch			*self=XFDASHBOARD_QUICKLAUNCH(inActor);
	XfdashboardQuicklaunchPrivate	*priv=self->priv;
	gfloat							minWidth, naturalWidth;
	ClutterActor					*child;
	ClutterActorIter				iter;
	gfloat							childMinWidth, childNaturalWidth;
	gint							numberChildren;
	gfloat							scale;

	/* Set up default values */
	minWidth=naturalWidth=0.0f;

	/* Determine width for horizontal orientation ... */
	if(priv->orientation==CLUTTER_ORIENTATION_HORIZONTAL)
	{
		/* Iterate through visible children and determine width */
		numberChildren=0;
		clutter_actor_iter_init(&iter, CLUTTER_ACTOR(inActor));
		while(clutter_actor_iter_next(&iter, &child))
		{
			/* Only check visible children */
			if(!clutter_actor_is_visible(child)) continue;

			/* Get child's size */
			clutter_actor_get_preferred_width(child,
												inForHeight,
												&childMinWidth,
												&childNaturalWidth);

			/* Determine widths */
			minWidth+=childMinWidth;
			naturalWidth+=childNaturalWidth;

			/* Count visible children */
			numberChildren++;
		}

		/* Add spacing between children and spacing as padding */
		if(numberChildren>0)
		{
			minWidth+=(numberChildren+1)*priv->spacing;
			naturalWidth+=(numberChildren+1)*priv->spacing;
		}

		/* Room for divider between pinned and other running applications */
		minWidth+=_xfdashboard_quicklaunch_get_separator_gap(self);
		naturalWidth+=_xfdashboard_quicklaunch_get_separator_gap(self);
	}
		/* ... otherwise determine width for vertical orientation */
		else
		{
			/* Iterate through visible children and determine widths */
			numberChildren=0;
			clutter_actor_iter_init(&iter, CLUTTER_ACTOR(inActor));
			while(clutter_actor_iter_next(&iter, &child))
			{
				/* Only check visible children */
				if(!clutter_actor_is_visible(child)) continue;

				/* Get sizes of child */
				clutter_actor_get_preferred_width(child,
													-1,
													&childMinWidth,
													&childNaturalWidth);

				/* Determine widths */
				minWidth=MAX(minWidth, childMinWidth);
				naturalWidth=MAX(naturalWidth, childNaturalWidth);

				/* Count visible children */
				numberChildren++;
			}

			/* Check if we need to scale width because of the need to fit
			 * all visible children into given limiting height
			 */
			if(inForHeight>=0.0f)
			{
				scale=_xfdashboard_quicklaunch_get_scale_for_height(self, inForHeight, TRUE);
				minWidth*=scale;

				scale=_xfdashboard_quicklaunch_get_scale_for_height(self, inForHeight, FALSE);
				naturalWidth*=scale;
			}

			/* Add spacing as padding */
			if(numberChildren>0)
			{
				minWidth+=2*priv->spacing;
				naturalWidth+=2*priv->spacing;
			}
		}

	/* Store sizes computed */
	if(outMinWidth) *outMinWidth=minWidth;
	if(outNaturalWidth) *outNaturalWidth=naturalWidth;
}

/* Allocate position and size of actor and its children */
/* Hover zoom like Plank/macOS dock: every icon grows depending on its
 * distance to the pointer (cosine falloff over a few icons), so the zoom
 * follows the pointer continuously. Grown icons push their neighbours aside
 * and the whole row stays centered. Only the zoom "strength" (0..1) is
 * animated when the pointer enters or leaves the dock.
 */
static void _xfdashboard_quicklaunch_update_zoom(XfdashboardQuicklaunch *self)
{
	XfdashboardQuicklaunchPrivate	*priv=self->priv;
	ClutterActor					*child;
	ClutterActorIter				iter;
	ClutterActorBox					box;
	gfloat							zooms[256];
	gfloat							extra[256];
	gfloat							totalExtra, before;
	gint							n, i;
	gfloat							maxZoom;
	gfloat							prevEnd;
	gboolean						separatorPending;

	/* Like Plank: icons may rise above the dock, the dock keeps its height */
	maxZoom=priv->hoverZoom;

	/* First pass: zoom factor of each visible child */
	n=0;
	totalExtra=0.0f;
	clutter_actor_iter_init(&iter, CLUTTER_ACTOR(self));
	while(clutter_actor_iter_next(&iter, &child) && n<256)
	{
		gfloat						w, h, center, distance, range, z;

		if(!clutter_actor_is_visible(child)) continue;

		clutter_actor_get_allocation_box(child, &box);
		clutter_actor_box_get_size(&box, &w, &h);
		w*=priv->scaleCurrent;
		h*=priv->scaleCurrent;

		z=1.0f;
		if(priv->hoverZoom>1.0f && priv->zoomStrength>0.0f && priv->zoomPointer>=0.0f)
		{
			if(priv->orientation==CLUTTER_ORIENTATION_HORIZONTAL)
			{
				center=box.x1+(w/2.0f);
				range=w*1.5f;
			}
				else
				{
					center=box.y1+(h/2.0f);
					range=h*1.5f;
				}

			distance=ABS(priv->zoomPointer-center);
			if(range>0.0f && distance<range)
			{
				z=1.0f+((maxZoom-1.0f)*priv->zoomStrength*((1.0f+cosf(G_PI*distance/range))/2.0f));
			}
		}

		zooms[n]=z;
		extra[n]=(priv->orientation==CLUTTER_ORIENTATION_HORIZONTAL ? w : h)*(z-1.0f);
		totalExtra+=extra[n];
		n++;
	}

	/* Second pass: scale and shift children */
	priv->growTop=0.0f;
	priv->separatorCount=0;
	i=0;
	before=0.0f;
	prevEnd=-1.0f;
	separatorPending=FALSE;
	clutter_actor_iter_init(&iter, CLUTTER_ACTOR(self));
	while(clutter_actor_iter_next(&iter, &child) && i<n)
	{
		gfloat						w, h, shift;

		if(_xfdashboard_quicklaunch_is_divider(self, child))
		{
			if(prevEnd>=0.0f && _xfdashboard_quicklaunch_divider_active(self, child)) separatorPending=TRUE;
			continue;
		}

		if(!clutter_actor_is_visible(child)) continue;

		clutter_actor_get_allocation_box(child, &box);
		clutter_actor_box_get_size(&box, &w, &h);
		w*=priv->scaleCurrent;
		h*=priv->scaleCurrent;

		shift=before-(totalExtra/2.0f);
		clutter_actor_set_pivot_point(child, 0.0f, 0.0f);
		clutter_actor_set_scale(child, priv->scaleCurrent*zooms[i], priv->scaleCurrent*zooms[i]);
		if(priv->orientation==CLUTTER_ORIENTATION_HORIZONTAL)
		{
			/* Grow upwards from the bottom edge (stays inside the dock) */
			clutter_actor_set_translation(child, shift, -(h*(zooms[i]-1.0f)), 0.0f);
		}
			else
			{
				clutter_actor_set_translation(child, 0.0f, shift, 0.0f);
			}

		/* Divider sits in the middle of the gap between its neighbours */
		{
			gfloat					start, end;

			if(priv->orientation==CLUTTER_ORIENTATION_HORIZONTAL)
			{
				start=box.x1+shift;
				end=start+(w*zooms[i]);
			}
				else
				{
					start=box.y1+shift;
					end=start+(h*zooms[i]);
				}

			if(separatorPending)
			{
				if(priv->separatorCount<(gint)G_N_ELEMENTS(priv->separatorX))
				{
					priv->separatorX[priv->separatorCount++]=(prevEnd+start)/2.0f;
				}
				separatorPending=FALSE;
			}
			prevEnd=end;
		}

		before+=extra[i];
		i++;
	}

	/* Like Plank the dock itself grows with the zoomed icons */
	priv->growLeft=priv->growRight=(isfinite(totalExtra) ? MAX(0.0f, totalExtra/2.0f) : 0.0f);
	if(!isfinite(priv->growTop)) priv->growTop=0.0f;
	clutter_actor_queue_redraw(CLUTTER_ACTOR(self));
}

static void _xfdashboard_quicklaunch_zoom_on_new_frame(ClutterTimeline *inTimeline, gint inMsecs, gpointer inUserData)
{
	XfdashboardQuicklaunch			*self=XFDASHBOARD_QUICKLAUNCH(inUserData);
	XfdashboardQuicklaunchPrivate	*priv=self->priv;

	priv->zoomStrength=priv->zoomStrengthFrom+((priv->zoomStrengthTo-priv->zoomStrengthFrom)*clutter_timeline_get_progress(inTimeline));
	_xfdashboard_quicklaunch_update_zoom(self);
}

static void _xfdashboard_quicklaunch_zoom_strength_to(XfdashboardQuicklaunch *self, gfloat inStrength)
{
	XfdashboardQuicklaunchPrivate	*priv=self->priv;

	if(priv->zoomStrengthTo==inStrength &&
		(clutter_timeline_is_playing(priv->zoomTimeline) || priv->zoomStrength==inStrength))
	{
		return;
	}

	clutter_timeline_stop(priv->zoomTimeline);
	priv->zoomStrengthFrom=priv->zoomStrength;
	priv->zoomStrengthTo=inStrength;
	clutter_timeline_rewind(priv->zoomTimeline);
	clutter_timeline_start(priv->zoomTimeline);
}

static gboolean _xfdashboard_quicklaunch_on_captured_event(ClutterActor *inActor, ClutterEvent *inEvent, gpointer inUserData)
{
	XfdashboardQuicklaunch			*self=XFDASHBOARD_QUICKLAUNCH(inActor);
	XfdashboardQuicklaunchPrivate	*priv=self->priv;
	gfloat							stageX, stageY, x, y;

	if(clutter_event_type(inEvent)!=CLUTTER_MOTION) return(CLUTTER_EVENT_PROPAGATE);

	clutter_event_get_coords(inEvent, &stageX, &stageY);
	if(!clutter_actor_transform_stage_point(inActor, stageX, stageY, &x, &y)) return(CLUTTER_EVENT_PROPAGATE);

	priv->zoomPointer=(priv->orientation==CLUTTER_ORIENTATION_HORIZONTAL ? x : y);
	if(priv->zoomStrengthTo<1.0f || priv->zoomStrength<1.0f) _xfdashboard_quicklaunch_zoom_strength_to(self, 1.0f);
	if(!clutter_timeline_is_playing(priv->zoomTimeline)) _xfdashboard_quicklaunch_update_zoom(self);

	return(CLUTTER_EVENT_PROPAGATE);
}

static gboolean _xfdashboard_quicklaunch_on_leave(ClutterActor *inActor, ClutterEvent *inEvent, gpointer inUserData)
{
	ClutterActor					*related;

	/* Pointer left the whole dock (not just moved onto a child) */
	related=clutter_event_get_related(inEvent);
	if(related && (related==inActor || clutter_actor_contains(inActor, related))) return(CLUTTER_EVENT_PROPAGATE);

	_xfdashboard_quicklaunch_zoom_strength_to(XFDASHBOARD_QUICKLAUNCH(inActor), 0.0f);
	return(CLUTTER_EVENT_PROPAGATE);
}

/* Draw the dock background ourselves (rounded rectangle with outline) so it
 * can grow with the zoomed icons without relayouting the stage. Colors,
 * outline and corner radius come from the theme's background-* properties.
 */
static void _xfdashboard_quicklaunch_paint_node(ClutterActor *inActor, ClutterPaintNode *inRootNode)
{
	XfdashboardQuicklaunch			*self=XFDASHBOARD_QUICKLAUNCH(inActor);
	XfdashboardQuicklaunchPrivate	*priv=self->priv;
	XfdashboardBackground			*background=XFDASHBOARD_BACKGROUND(inActor);
	const XfdashboardGradientColor	*gradient;
	const ClutterColor				*fill, *line;
	ClutterColor					transparent={ 0, 0, 0, 0 };
	ClutterActorBox					box;
	ClutterPaintNode				*node;
	gfloat							size[2], fillColor[4], lineColor[4];
	gfloat							radius, lineWidth;
	guint8							opacity;

	if(clutter_feature_available(CLUTTER_FEATURE_SHADERS_GLSL))
	{
		if(!priv->dockPipeline)
		{
			CoglContext					*context;
			CoglSnippet					*snippet;

			context=clutter_backend_get_cogl_context(clutter_get_default_backend());
			priv->dockPipeline=cogl_pipeline_new(context);
			cogl_pipeline_set_layer_null_texture(priv->dockPipeline, 0, COGL_TEXTURE_TYPE_2D);
			/* Shader output is translucent: make sure cogl does not disable blending */
			cogl_pipeline_set_color4f(priv->dockPipeline, 1.0f, 1.0f, 1.0f, 0.5f);
			cogl_pipeline_set_blend(priv->dockPipeline, "RGBA = ADD(SRC_COLOR, DST_COLOR*(1-SRC_COLOR[A]))", NULL);
			snippet=cogl_snippet_new(COGL_SNIPPET_HOOK_FRAGMENT,
										"uniform vec2 xfd_size;\n"
										"uniform float xfd_radius;\n"
										"uniform float xfd_line;\n"
										"uniform vec4 xfd_fill;\n"
										"uniform vec4 xfd_stroke;\n",
										"vec2 xfd_p=cogl_tex_coord0_in.st*xfd_size;\n"
										"vec2 xfd_q=abs(xfd_p-xfd_size*0.5)-(xfd_size*0.5-vec2(xfd_radius));\n"
										"float xfd_d=length(max(xfd_q, vec2(0.0)))+min(max(xfd_q.x, xfd_q.y), 0.0)-xfd_radius;\n"
										"float xfd_all=clamp(0.5-xfd_d, 0.0, 1.0);\n"
										"float xfd_in=clamp(0.5-(xfd_d+xfd_line), 0.0, 1.0);\n"
										"cogl_color_out=xfd_fill*xfd_in+xfd_stroke*(xfd_all-xfd_in);\n");
			cogl_pipeline_add_snippet(priv->dockPipeline, snippet);
			cogl_object_unref(snippet);
		}

		gradient=xfdashboard_background_get_fill_color(background);
		fill=(gradient ? xfdashboard_gradient_color_get_solid_color(gradient) : NULL);
		gradient=xfdashboard_background_get_outline_color(background);
		line=(gradient ? xfdashboard_gradient_color_get_solid_color(gradient) : NULL);
		if(!fill) fill=&transparent;
		if(!line) line=&transparent;

		opacity=clutter_actor_get_paint_opacity(inActor);
		fillColor[3]=(fill->alpha/255.0f)*(opacity/255.0f);
		fillColor[0]=(fill->red/255.0f)*fillColor[3];
		fillColor[1]=(fill->green/255.0f)*fillColor[3];
		fillColor[2]=(fill->blue/255.0f)*fillColor[3];
		lineColor[3]=(line->alpha/255.0f)*(opacity/255.0f);
		lineColor[0]=(line->red/255.0f)*lineColor[3];
		lineColor[1]=(line->green/255.0f)*lineColor[3];
		lineColor[2]=(line->blue/255.0f)*lineColor[3];

		clutter_actor_get_allocation_box(inActor, &box);
		box.x2=clutter_actor_box_get_width(&box)+priv->growRight;
		box.y2=clutter_actor_box_get_height(&box);
		box.x1=-priv->growLeft;
		box.y1=-priv->growTop;
		size[0]=box.x2-box.x1;
		size[1]=box.y2-box.y1;

		radius=MIN(xfdashboard_background_get_fill_corner_radius(background), MIN(size[0], size[1])/2.0f);
		lineWidth=xfdashboard_background_get_outline_width(background);

		cogl_pipeline_set_uniform_float(priv->dockPipeline, cogl_pipeline_get_uniform_location(priv->dockPipeline, "xfd_size"), 2, 1, size);
		cogl_pipeline_set_uniform_1f(priv->dockPipeline, cogl_pipeline_get_uniform_location(priv->dockPipeline, "xfd_radius"), radius);
		cogl_pipeline_set_uniform_1f(priv->dockPipeline, cogl_pipeline_get_uniform_location(priv->dockPipeline, "xfd_line"), lineWidth);
		cogl_pipeline_set_uniform_float(priv->dockPipeline, cogl_pipeline_get_uniform_location(priv->dockPipeline, "xfd_fill"), 4, 1, fillColor);
		cogl_pipeline_set_uniform_float(priv->dockPipeline, cogl_pipeline_get_uniform_location(priv->dockPipeline, "xfd_stroke"), 4, 1, lineColor);

		node=clutter_pipeline_node_new(priv->dockPipeline);
		clutter_paint_node_set_name(node, "dock-background");
		clutter_paint_node_add_texture_rectangle(node, &box, 0.0f, 0.0f, 1.0f, 1.0f);
		clutter_paint_node_add_child(inRootNode, node);
		clutter_paint_node_unref(node);
	}

	/* Applications button while the applications view is open: a light band
	 * the full height of the dock behind it, like Plank's active item: clear
	 * at the top, brightest at the bottom, as wide as the item's slot
	 */
	if(priv->plankStyle &&
		priv->appsButton &&
		clutter_actor_is_visible(priv->appsButton) &&
		xfdashboard_toggle_button_get_toggle_state(XFDASHBOARD_TOGGLE_BUTTON(priv->appsButton)))
	{
		ClutterActorBox					buttonBox, bandBox;
		ClutterColor					bandColor={ 0xff, 0xff, 0xff, 0x48 };
		gfloat							tx, ty, dw, dh;
		gdouble							sx, sy;

		if(!priv->activeTexture)
		{
			cairo_surface_t				*surface;
			cairo_t						*cr;
			cairo_pattern_t				*pattern;
			CoglContext					*context;

			surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 64);
			cr=cairo_create(surface);
			pattern=cairo_pattern_create_linear(0, 0, 0, 64);
			cairo_pattern_add_color_stop_rgba(pattern, 0.0, 1.0, 1.0, 1.0, 0.0);
			cairo_pattern_add_color_stop_rgba(pattern, 1.0, 1.0, 1.0, 1.0, 1.0);
			cairo_set_source(cr, pattern);
			cairo_paint(cr);
			cairo_pattern_destroy(pattern);
			cairo_destroy(cr);
			cairo_surface_flush(surface);

			context=clutter_backend_get_cogl_context(clutter_get_default_backend());
			priv->activeTexture=COGL_TEXTURE(cogl_texture_2d_new_from_data(context, 1, 64,
																		COGL_PIXEL_FORMAT_BGRA_8888_PRE,
																		cairo_image_surface_get_stride(surface),
																		cairo_image_surface_get_data(surface),
																		NULL));
			cairo_surface_destroy(surface);
		}

		clutter_actor_get_size(inActor, &dw, &dh);
		clutter_actor_get_allocation_box(priv->appsButton, &buttonBox);
		clutter_actor_get_translation(priv->appsButton, &tx, &ty, NULL);
		clutter_actor_get_scale(priv->appsButton, &sx, &sy);

		bandColor.alpha=(guint8)(bandColor.alpha*(clutter_actor_get_paint_opacity(inActor)/255.0f));
		/* The item's slot: the button plus half the spacing on each side */
		bandBox.x1=floorf(buttonBox.x1+tx-priv->spacing/2.0f);
		bandBox.x2=ceilf(buttonBox.x1+tx+clutter_actor_box_get_width(&buttonBox)*(gfloat)sx+priv->spacing/2.0f);
		bandBox.y1=-priv->growTop;
		bandBox.y2=dh;

		node=clutter_texture_node_new(priv->activeTexture, &bandColor, CLUTTER_SCALING_FILTER_LINEAR, CLUTTER_SCALING_FILTER_LINEAR);
		clutter_paint_node_set_name(node, "dock-active-applications");
		clutter_paint_node_add_rectangle(node, &bandBox);
		clutter_paint_node_add_child(inRootNode, node);
		clutter_paint_node_unref(node);
	}

	/* Running indicators like Plank: one small glowing blue dot per window
	 * (up to three) near the bottom edge of the dock, under each icon.
	 * Only for themes that opt in with "plank-style: true;"
	 */
	if(priv->plankStyle)
	{
		ClutterActor					*child;
		gfloat							dw, dh, dotY=-1.0f;
		ClutterPaintNode				*dotNode=NULL;

		if(!priv->dotTexture)
		{
			/* Plank's running indicator copied pixel by pixel from its dock:
			 * a 3x2 light blue core with a deeper blue rim (5x4) inside a
			 * soft blue glow, 17x16 pixels around the dot's center. The glow
			 * is the rim blue at the alpha measured over Plank's dock, so it
			 * blends the same way on any dock color.
			 */
			static const guint8			dotAlpha[16][17]={
				{ 0, 0, 0, 0, 0, 0, 8, 14, 17, 14, 8, 0, 0, 0, 0, 0, 0 },
				{ 0, 0, 0, 0, 14, 29, 36, 37, 39, 37, 36, 29, 14, 0, 0, 0, 0 },
				{ 0, 0, 0, 21, 35, 40, 43, 44, 46, 44, 43, 40, 35, 21, 0, 0, 0 },
				{ 0, 0, 18, 36, 42, 48, 53, 57, 56, 57, 53, 48, 42, 36, 18, 0, 0 },
				{ 0, 12, 31, 41, 49, 57, 62, 67, 69, 67, 62, 57, 49, 41, 31, 12, 0 },
				{ 0, 20, 39, 46, 56, 64, 72, 96, 117, 96, 72, 64, 56, 46, 39, 20, 0 },
				{ 7, 25, 42, 50, 60, 71, 135, 224, 255, 224, 135, 71, 60, 50, 42, 25, 7 },
				{ 10, 29, 43, 53, 64, 93, 186, 255, 255, 255, 186, 93, 64, 53, 43, 29, 10 },
				{ 10, 30, 43, 53, 64, 93, 186, 255, 255, 255, 186, 93, 64, 53, 43, 30, 10 },
				{ 9, 27, 42, 52, 61, 71, 135, 224, 255, 224, 135, 71, 61, 52, 42, 27, 9 },
				{ 0, 20, 39, 46, 56, 64, 72, 96, 117, 96, 72, 64, 56, 46, 39, 20, 0 },
				{ 0, 12, 31, 41, 49, 57, 62, 67, 69, 67, 62, 57, 49, 41, 31, 12, 0 },
				{ 0, 0, 18, 36, 42, 48, 53, 57, 56, 57, 53, 48, 42, 36, 18, 0, 0 },
				{ 0, 0, 0, 21, 35, 40, 43, 44, 46, 44, 43, 40, 35, 21, 0, 0, 0 },
				{ 0, 0, 0, 0, 14, 29, 36, 37, 39, 37, 36, 29, 14, 0, 0, 0, 0 },
				{ 0, 0, 0, 0, 0, 0, 8, 14, 17, 14, 8, 0, 0, 0, 0, 0, 0 },
			};
			guint8						data[16*17*4];
			CoglContext					*context;
			gint						row, col;

			for(row=0; row<16; row++)
			{
				for(col=0; col<17; col++)
				{
					guint8				*px=&data[((row*17)+col)*4];
					guint				r=0x0a, g=0x53, b=0xcc, a=dotAlpha[row][col];

					if((row==7 || row==8) && col>=7 && col<=9)
					{
						if(col==8) { r=0x60; g=0x99; b=0xf7; }
							else { r=0x35; g=0x76; b=0xe1; }
						a=0xff;
					}
					/* BGRA, premultiplied */
					px[0]=(guint8)((b*a)/255);
					px[1]=(guint8)((g*a)/255);
					px[2]=(guint8)((r*a)/255);
					px[3]=(guint8)a;
				}
			}

			context=clutter_backend_get_cogl_context(clutter_get_default_backend());
			priv->dotTexture=COGL_TEXTURE(cogl_texture_2d_new_from_data(context, 17, 16,
																		COGL_PIXEL_FORMAT_BGRA_8888_PRE,
																		17*4,
																		data,
																		NULL));
		}

		clutter_actor_get_size(inActor, &dw, &dh);
		for(child=clutter_actor_get_first_child(inActor); child && priv->dotTexture; child=clutter_actor_get_next_sibling(child))
		{
			ClutterActorBox				childBox;
			GAppInfo					*appInfo;
			const GList					*windows;
			gint						count, k;
			gfloat						tx, ty, cx;
			gdouble						sx, sy;

			if(!clutter_actor_is_visible(child)) continue;
			if(!XFDASHBOARD_IS_APPLICATION_BUTTON(child)) continue;
			if(!xfdashboard_stylable_has_class(XFDASHBOARD_STYLABLE(child), "running")) continue;

			appInfo=xfdashboard_application_button_get_app_info(XFDASHBOARD_APPLICATION_BUTTON(child));
			if(!appInfo) continue;
			windows=xfdashboard_application_tracker_get_window_list_by_app_info(priv->appTracker, appInfo);
			count=MIN(3, (gint)g_list_length((GList*)windows));
			if(count<1) count=1;

			clutter_actor_get_allocation_box(child, &childBox);
			clutter_actor_get_translation(child, &tx, &ty, NULL);
			clutter_actor_get_scale(child, &sx, &sy);

			/* Like Plank: the dot's center sits 2px above the dock's bottom
			 * edge, so its rim touches the edge
			 */
			if(dotY<0.0f) dotY=floorf(dh)-2.0f;

			cx=childBox.x1+tx+((clutter_actor_box_get_width(&childBox)*(gfloat)sx)/2.0f);
			/* Pixel-exact: no filtering, whole pixels */
			if(!dotNode) dotNode=clutter_texture_node_new(priv->dotTexture, NULL, CLUTTER_SCALING_FILTER_NEAREST, CLUTTER_SCALING_FILTER_NEAREST);
			for(k=0; k<count; k++)
			{
				ClutterActorBox			dotBox;
				gfloat					dx=cx+((k-((count-1)/2.0f))*6.0f);
				gfloat					ty2=1.0f;

				dotBox.x1=floorf(dx)-8.0f;
				dotBox.x2=dotBox.x1+17.0f;
				dotBox.y1=dotY-8.0f;
				dotBox.y2=dotBox.y1+16.0f;

				/* Like Plank, the glow stays inside the dock: cut it at the
				 * dock's bottom edge
				 */
				if(dotBox.y2>floorf(dh))
				{
					ty2=(floorf(dh)-dotBox.y1)/16.0f;
					dotBox.y2=floorf(dh);
				}
				clutter_paint_node_add_texture_rectangle(dotNode, &dotBox, 0.0f, 0.0f, 1.0f, ty2);
			}
		}

		if(dotNode)
		{
			clutter_paint_node_set_name(dotNode, "dock-running-dots");
			clutter_paint_node_add_child(inRootNode, dotNode);
			clutter_paint_node_unref(dotNode);
		}
	}

	/* Divider between pinned and other running applications: a thin line in
	 * the dock's outline color (a bit stronger) over the middle of its height
	 */
	if(priv->separatorCount>0)
	{
		gint							k;
		const XfdashboardGradientColor	*fillGradient;
		const ClutterColor				*fillSolid;
		ClutterColor					dividerColor={ 0x00, 0x00, 0x00, 0x66 };
		ClutterActorBox					dividerBox;
		gfloat							dw, dh, luminance;

		/* Like Plank: dark line (40% black) on a light dock, light line
		 * (30% white) on a dark dock
		 */
		fillGradient=xfdashboard_background_get_fill_color(XFDASHBOARD_BACKGROUND(inActor));
		fillSolid=(fillGradient ? xfdashboard_gradient_color_get_solid_color(fillGradient) : NULL);
		if(fillSolid)
		{
			luminance=(0.2126f*fillSolid->red)+(0.7152f*fillSolid->green)+(0.0722f*fillSolid->blue);
			if(luminance<128.0f)
			{
				dividerColor.red=dividerColor.green=dividerColor.blue=0xff;
				dividerColor.alpha=0x4d;
			}
		}
		dividerColor.alpha=(guint8)((dividerColor.alpha*clutter_actor_get_paint_opacity(inActor))/255);

		clutter_actor_get_size(inActor, &dw, &dh);
		node=clutter_color_node_new(&dividerColor);
		clutter_paint_node_set_name(node, "dock-divider");
		for(k=0; k<priv->separatorCount; k++)
		{
			if(priv->orientation==CLUTTER_ORIENTATION_HORIZONTAL)
			{
				dividerBox.x1=floorf(priv->separatorX[k]-1.0f);
				dividerBox.x2=dividerBox.x1+2.0f;
				dividerBox.y1=floorf(dh*0.22f);
				dividerBox.y2=ceilf(dh*0.78f);
			}
				else
				{
					dividerBox.y1=floorf(priv->separatorX[k]-1.0f);
					dividerBox.y2=dividerBox.y1+2.0f;
					dividerBox.x1=floorf(dw*0.22f);
					dividerBox.x2=ceilf(dw*0.78f);
				}
			clutter_paint_node_add_rectangle(node, &dividerBox);
		}
		clutter_paint_node_add_child(inRootNode, node);
		clutter_paint_node_unref(node);
	}

	/* Let parent draw whatever the theme set up (normally nothing: background-type none) */
	if(CLUTTER_ACTOR_CLASS(xfdashboard_quicklaunch_parent_class)->paint_node)
	{
		CLUTTER_ACTOR_CLASS(xfdashboard_quicklaunch_parent_class)->paint_node(inActor, inRootNode);
	}
}

static gboolean _xfdashboard_quicklaunch_get_paint_volume(ClutterActor *inActor, ClutterPaintVolume *ioVolume)
{
	XfdashboardQuicklaunchPrivate	*priv=XFDASHBOARD_QUICKLAUNCH(inActor)->priv;
	ClutterActorBox					box;
	ClutterVertex					origin;

	if(!clutter_actor_has_allocation(inActor)) return(FALSE);

	clutter_actor_get_allocation_box(inActor, &box);
	origin.x=-priv->growLeft;
	origin.y=-priv->growTop;
	origin.z=0.0f;
	clutter_paint_volume_set_origin(ioVolume, &origin);
	clutter_paint_volume_set_width(ioVolume, MAX(0.0f, clutter_actor_box_get_width(&box)+priv->growLeft+priv->growRight));
	clutter_paint_volume_set_height(ioVolume, MAX(0.0f, clutter_actor_box_get_height(&box)+priv->growTop));
	return(TRUE);
}

static void _xfdashboard_quicklaunch_allocate(ClutterActor *inActor,
												const ClutterActorBox *inBox,
												ClutterAllocationFlags inFlags)
{
	XfdashboardQuicklaunch			*self=XFDASHBOARD_QUICKLAUNCH(inActor);
	XfdashboardQuicklaunchPrivate	*priv=self->priv;
	gfloat							availableWidth, availableHeight;
	gfloat							childWidth, childHeight;
	ClutterActor					*child;
	ClutterActorIter				iter;
	ClutterActorBox					childAllocation={ 0, };
	ClutterActorBox					oldChildAllocation={ 0, };
	gboolean						fixedPosition;
	gfloat							fixedX, fixedY;
	gfloat							separatorGap;

	/* Chain up to store the allocation of the actor */
	CLUTTER_ACTOR_CLASS(xfdashboard_quicklaunch_parent_class)->allocate(inActor, inBox, inFlags);

	/* Get available size */
	clutter_actor_box_get_size(inBox, &availableWidth, &availableHeight);

	/* Find scaling to get all children fit the allocation */
	if(priv->orientation==CLUTTER_ORIENTATION_HORIZONTAL)
	{
		priv->scaleCurrent=_xfdashboard_quicklaunch_get_scale_for_width(self, availableWidth, FALSE);
	}
		else
		{
			priv->scaleCurrent=_xfdashboard_quicklaunch_get_scale_for_height(self, availableHeight, FALSE);
		}

	/* Calculate new position and size of visible children */
	childAllocation.x1=childAllocation.y1=priv->spacing;
	separatorGap=_xfdashboard_quicklaunch_get_separator_gap(self);
	clutter_actor_iter_init(&iter, CLUTTER_ACTOR(inActor));
	while(clutter_actor_iter_next(&iter, &child))
	{
		/* Leave room for the divider */
		if(_xfdashboard_quicklaunch_is_divider(self, child))
		{
			if(separatorGap<=0.0f || !_xfdashboard_quicklaunch_divider_active(self, child)) continue;

			if(priv->orientation==CLUTTER_ORIENTATION_HORIZONTAL) childAllocation.x1=ceil(childAllocation.x1+_xfdashboard_quicklaunch_get_divider_width(self));
				else childAllocation.y1=ceil(childAllocation.y1+_xfdashboard_quicklaunch_get_divider_width(self));
			continue;
		}

		/* Is child visible? */
		if(!clutter_actor_is_visible(child)) continue;

		/* Calculate new position and size of child. Because we will set
		 * a scale factor to the actor we have to set the real unscaled
		 * width and height but the position should be "translated" to
		 * scaled sizes.
		 */
		clutter_actor_get_preferred_size(child, NULL, NULL, &childWidth, &childHeight);

		if(priv->orientation==CLUTTER_ORIENTATION_HORIZONTAL)
		{
			childAllocation.y1=ceil(MAX(((availableHeight-(childHeight*priv->scaleCurrent))/2.0f), priv->spacing));
			childAllocation.y2=ceil(childAllocation.y1+childHeight);
			childAllocation.x2=ceil(childAllocation.x1+childWidth);
		}
			else
			{
				childAllocation.x1=ceil(MAX(((availableWidth-(childWidth*priv->scaleCurrent))/2.0f), priv->spacing));
				childAllocation.x2=ceil(childAllocation.x1+childWidth);
				childAllocation.y2=ceil(childAllocation.y1+childHeight);
			}


		/* Respect fixed position of child */
		g_object_get(child,
						"fixed-position-set", &fixedPosition,
						"fixed-x", &fixedX,
						"fixed-y", &fixedY,
						NULL);

		if(fixedPosition)
		{
			oldChildAllocation.x1=childAllocation.x1;
			oldChildAllocation.x2=childAllocation.x2;
			oldChildAllocation.y1=childAllocation.y1;
			oldChildAllocation.y2=childAllocation.y2;

			childWidth=childAllocation.x2-childAllocation.x1;
			childHeight=childAllocation.y2-childAllocation.y1;

			childAllocation.x1=ceil(fixedX);
			childAllocation.x2=childAllocation.x1+childWidth;
			childAllocation.y1=ceil(fixedY);
			childAllocation.y2=childAllocation.y1+childHeight;
		}

		/* Allocate child */
		clutter_actor_allocate(child, &childAllocation, inFlags);

		/* Set up for next child */
		if(fixedPosition)
		{
			childAllocation.x1=oldChildAllocation.x1;
			childAllocation.x2=oldChildAllocation.x2;
			childAllocation.y1=oldChildAllocation.y1;
			childAllocation.y2=oldChildAllocation.y2;
		}

		childWidth*=priv->scaleCurrent;
		childHeight*=priv->scaleCurrent;
		if(priv->orientation==CLUTTER_ORIENTATION_HORIZONTAL) childAllocation.x1=ceil(childAllocation.x1+childWidth+priv->spacing);
			else childAllocation.y1=ceil(childAllocation.y1+childHeight+priv->spacing);
	}

	/* Layout scale combined with hover zoom */
	_xfdashboard_quicklaunch_update_zoom(self);
}

/* IMPLEMENTATION: Interface XfdashboardFocusable */

/* Determine if this actor supports selection */
static gboolean _xfdashboard_quicklaunch_focusable_supports_selection(XfdashboardFocusable *inFocusable)
{
	g_return_val_if_fail(XFDASHBOARD_IS_FOCUSABLE(inFocusable), FALSE);
	g_return_val_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(inFocusable), FALSE);

	/* This actor supports selection */
	return(TRUE);
}


/* Get current selection */
static ClutterActor* _xfdashboard_quicklaunch_focusable_get_selection(XfdashboardFocusable *inFocusable)
{
	XfdashboardQuicklaunch				*self;
	XfdashboardQuicklaunchPrivate		*priv;

	g_return_val_if_fail(XFDASHBOARD_IS_FOCUSABLE(inFocusable), CLUTTER_EVENT_PROPAGATE);
	g_return_val_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(inFocusable), CLUTTER_EVENT_PROPAGATE);

	self=XFDASHBOARD_QUICKLAUNCH(inFocusable);
	priv=self->priv;

	/* Return current selection */
	return(priv->selectedItem);
}

/* Set new selection */
static gboolean _xfdashboard_quicklaunch_focusable_set_selection(XfdashboardFocusable *inFocusable,
																	ClutterActor *inSelection)
{
	XfdashboardQuicklaunch				*self;
	XfdashboardQuicklaunchPrivate		*priv;

	g_return_val_if_fail(XFDASHBOARD_IS_FOCUSABLE(inFocusable), FALSE);
	g_return_val_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(inFocusable), FALSE);
	g_return_val_if_fail(!inSelection || CLUTTER_IS_ACTOR(inSelection), FALSE);

	self=XFDASHBOARD_QUICKLAUNCH(inFocusable);
	priv=self->priv;

	/* Check that selection is a child of this actor */
	if(inSelection &&
		!clutter_actor_contains(CLUTTER_ACTOR(self), inSelection))
	{
		ClutterActor						*parent;

		parent=clutter_actor_get_parent(inSelection);
		g_warning("%s is a child of %s and cannot be selected at %s",
					G_OBJECT_TYPE_NAME(inSelection),
					parent ? G_OBJECT_TYPE_NAME(parent) : "<nil>",
					G_OBJECT_TYPE_NAME(self));
	}

	/* Set new selection */
	priv->selectedItem=inSelection;

	/* New selection was set successfully */
	return(TRUE);
}

/* Find requested selection target depending of current selection */
static ClutterActor* _xfdashboard_quicklaunch_focusable_find_selection(XfdashboardFocusable *inFocusable,
																		ClutterActor *inSelection,
																		XfdashboardSelectionTarget inDirection)
{
	XfdashboardQuicklaunch				*self;
	XfdashboardQuicklaunchPrivate		*priv;
	ClutterActor						*selection;
	ClutterActor						*newSelection;
	gchar								*valueName;

	g_return_val_if_fail(XFDASHBOARD_IS_FOCUSABLE(inFocusable), FALSE);
	g_return_val_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(inFocusable), FALSE);
	g_return_val_if_fail(!inSelection || CLUTTER_IS_ACTOR(inSelection), NULL);
	g_return_val_if_fail(inDirection>=0 && inDirection<=XFDASHBOARD_SELECTION_TARGET_NEXT, NULL);

	self=XFDASHBOARD_QUICKLAUNCH(inFocusable);
	priv=self->priv;
	selection=inSelection;
	newSelection=NULL;

	/* If there is nothing selected, select first actor and return */
	if(!inSelection)
	{
		selection=clutter_actor_get_first_child(CLUTTER_ACTOR(self));

		valueName=xfdashboard_get_enum_value_name(XFDASHBOARD_TYPE_SELECTION_TARGET, inDirection);
		XFDASHBOARD_DEBUG(self, ACTOR,
							"No selection at %s, so select first child %s for direction %s",
							G_OBJECT_TYPE_NAME(self),
							selection ? G_OBJECT_TYPE_NAME(selection) : "<nil>",
							valueName);
		g_free(valueName);

		return(selection);
	}

	/* Check that selection is a child of this actor otherwise return NULL */
	if(!clutter_actor_contains(CLUTTER_ACTOR(self), inSelection))
	{
		ClutterActor						*parent;

		parent=clutter_actor_get_parent(inSelection);
		g_warning("Cannot lookup selection target at %s because %s is a child of %s",
					G_OBJECT_TYPE_NAME(self),
					G_OBJECT_TYPE_NAME(inSelection),
					parent ? G_OBJECT_TYPE_NAME(parent) : "<nil>");

		return(NULL);
	}

	/* Find target selection */
	switch(inDirection)
	{
		case XFDASHBOARD_SELECTION_TARGET_LEFT:
			if(priv->orientation==CLUTTER_ORIENTATION_HORIZONTAL)
			{
				newSelection=xfdashboard_quicklaunch_get_previous_selectable(self, inSelection);
			}
			break;

		case XFDASHBOARD_SELECTION_TARGET_RIGHT:
			if(priv->orientation==CLUTTER_ORIENTATION_HORIZONTAL)
			{
				newSelection=xfdashboard_quicklaunch_get_next_selectable(self, inSelection);
			}
			break;

		case XFDASHBOARD_SELECTION_TARGET_UP:
			if(priv->orientation==CLUTTER_ORIENTATION_VERTICAL)
			{
				newSelection=xfdashboard_quicklaunch_get_previous_selectable(self, inSelection);
			}
			break;

		case XFDASHBOARD_SELECTION_TARGET_DOWN:
			if(priv->orientation==CLUTTER_ORIENTATION_VERTICAL)
			{
				newSelection=xfdashboard_quicklaunch_get_next_selectable(self, inSelection);
			}
			break;

		case XFDASHBOARD_SELECTION_TARGET_FIRST:
		case XFDASHBOARD_SELECTION_TARGET_PAGE_UP:
		case XFDASHBOARD_SELECTION_TARGET_PAGE_LEFT:
			if(inDirection==XFDASHBOARD_SELECTION_TARGET_FIRST ||
				(inDirection==XFDASHBOARD_SELECTION_TARGET_PAGE_UP && priv->orientation==CLUTTER_ORIENTATION_VERTICAL) ||
				(inDirection==XFDASHBOARD_SELECTION_TARGET_PAGE_LEFT && priv->orientation==CLUTTER_ORIENTATION_HORIZONTAL))
			{
				newSelection=clutter_actor_get_first_child(CLUTTER_ACTOR(self));
				while(newSelection && !clutter_actor_is_visible(newSelection))
				{
					newSelection=clutter_actor_get_next_sibling(newSelection);
				}
			}
			break;

		case XFDASHBOARD_SELECTION_TARGET_LAST:
		case XFDASHBOARD_SELECTION_TARGET_PAGE_DOWN:
		case XFDASHBOARD_SELECTION_TARGET_PAGE_RIGHT:
			if(inDirection==XFDASHBOARD_SELECTION_TARGET_LAST ||
				(inDirection==XFDASHBOARD_SELECTION_TARGET_PAGE_DOWN && priv->orientation==CLUTTER_ORIENTATION_VERTICAL) ||
				(inDirection==XFDASHBOARD_SELECTION_TARGET_PAGE_RIGHT && priv->orientation==CLUTTER_ORIENTATION_HORIZONTAL))
			{
				newSelection=clutter_actor_get_last_child(CLUTTER_ACTOR(self));
				while(newSelection && !clutter_actor_is_visible(newSelection))
				{
					newSelection=clutter_actor_get_previous_sibling(newSelection);
				}
			}
			break;

		case XFDASHBOARD_SELECTION_TARGET_NEXT:
			newSelection=xfdashboard_quicklaunch_get_next_selectable(self, inSelection);
			while(newSelection && !clutter_actor_is_visible(newSelection))
			{
				newSelection=clutter_actor_get_next_sibling(newSelection);
			}

			if(!newSelection)
			{
				newSelection=xfdashboard_quicklaunch_get_previous_selectable(self, inSelection);
				while(newSelection && !clutter_actor_is_visible(newSelection))
				{
					newSelection=clutter_actor_get_next_sibling(newSelection);
				}
			}
			break;

		default:
			{
				valueName=xfdashboard_get_enum_value_name(XFDASHBOARD_TYPE_SELECTION_TARGET, inDirection);
				g_critical("Focusable object %s does not handle selection direction of type %s.",
							G_OBJECT_TYPE_NAME(self),
							valueName);
				g_free(valueName);
			}
			break;
	}

	/* If new selection could be found override current selection with it */
	if(newSelection) selection=newSelection;

	/* Return new selection found */
	XFDASHBOARD_DEBUG(self, ACTOR,
						"Selecting %s at %s for current selection %s in direction %u",
						selection ? G_OBJECT_TYPE_NAME(selection) : "<nil>",
						G_OBJECT_TYPE_NAME(self),
						inSelection ? G_OBJECT_TYPE_NAME(inSelection) : "<nil>",
						inDirection);

	return(selection);
}

/* Activate selection */
static gboolean _xfdashboard_quicklaunch_focusable_activate_selection(XfdashboardFocusable *inFocusable,
																		ClutterActor *inSelection)
{
	XfdashboardQuicklaunch				*self;

	g_return_val_if_fail(XFDASHBOARD_IS_FOCUSABLE(inFocusable), FALSE);
	g_return_val_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(inFocusable), FALSE);
	g_return_val_if_fail(CLUTTER_IS_ACTOR(inSelection), FALSE);

	self=XFDASHBOARD_QUICKLAUNCH(inFocusable);

	/* Check that selection is a child of this actor */
	if(!clutter_actor_contains(CLUTTER_ACTOR(self), inSelection))
	{
		ClutterActor						*parent;

		parent=clutter_actor_get_parent(inSelection);
		g_warning("%s is a child of %s and cannot be activated at %s",
					G_OBJECT_TYPE_NAME(inSelection),
					parent ? G_OBJECT_TYPE_NAME(parent) : "<nil>",
					G_OBJECT_TYPE_NAME(self));

		return(FALSE);
	}

	/* Activate selection */
	g_signal_emit_by_name(inSelection, "clicked");

	return(TRUE);
}

/* Interface initialization
 * Set up default functions
 */
void _xfdashboard_quicklaunch_focusable_iface_init(XfdashboardFocusableInterface *iface)
{
	iface->supports_selection=_xfdashboard_quicklaunch_focusable_supports_selection;
	iface->get_selection=_xfdashboard_quicklaunch_focusable_get_selection;
	iface->set_selection=_xfdashboard_quicklaunch_focusable_set_selection;
	iface->find_selection=_xfdashboard_quicklaunch_focusable_find_selection;
	iface->activate_selection=_xfdashboard_quicklaunch_focusable_activate_selection;
}

/* IMPLEMENTATION: GObject */

/* Dispose this object */
static void _xfdashboard_quicklaunch_dispose(GObject *inObject)
{
	XfdashboardQuicklaunchPrivate	*priv=XFDASHBOARD_QUICKLAUNCH(inObject)->priv;

	/* Release our allocated variables */
	if(priv->dockPipeline)
	{
		cogl_object_unref(priv->dockPipeline);
		priv->dockPipeline=NULL;
	}

	if(priv->zoomTimeline)
	{
		clutter_timeline_stop(priv->zoomTimeline);
		g_signal_handlers_disconnect_by_data(priv->zoomTimeline, inObject);
		g_object_unref(priv->zoomTimeline);
		priv->zoomTimeline=NULL;
	}

	if(priv->settingsFavouritesBinding)
	{
		g_object_unref(priv->settingsFavouritesBinding);
		priv->settingsFavouritesBinding=NULL;
	}

	if(priv->appTracker)
	{
		g_object_unref(priv->appTracker);
		priv->appTracker=NULL;
	}

	if(priv->appDB)
	{
		g_object_unref(priv->appDB);
		priv->appDB=NULL;
	}

	if(priv->favourites)
	{
		g_strfreev(priv->favourites);
		priv->favourites=NULL;
	}

	if(priv->separatorFavouritesToDynamic)
	{
		clutter_actor_destroy(priv->separatorFavouritesToDynamic);
		priv->separatorFavouritesToDynamic=NULL;
	}

	if(priv->windowsChangedIdleID)
	{
		g_source_remove(priv->windowsChangedIdleID);
		priv->windowsChangedIdleID=0;
	}

	if(priv->dotTexture)
	{
		cogl_object_unref(priv->dotTexture);
		priv->dotTexture=NULL;
	}

	if(priv->activeTexture)
	{
		cogl_object_unref(priv->activeTexture);
		priv->activeTexture=NULL;
	}

	if(priv->settings)
	{
		g_object_unref(priv->settings);
		priv->settings=NULL;
	}

	/* Call parent's class dispose method */
	G_OBJECT_CLASS(xfdashboard_quicklaunch_parent_class)->dispose(inObject);
}

/* Set/get properties */
static void _xfdashboard_quicklaunch_set_property(GObject *inObject,
													guint inPropID,
													const GValue *inValue,
													GParamSpec *inSpec)
{
	XfdashboardQuicklaunch			*self=XFDASHBOARD_QUICKLAUNCH(inObject);

	switch(inPropID)
	{
		case PROP_FAVOURITES:
			_xfdashboard_quicklaunch_set_favourites(self, inValue);
			break;

		case PROP_NORMAL_ICON_SIZE:
			xfdashboard_quicklaunch_set_normal_icon_size(self, g_value_get_float(inValue));
			break;

		case PROP_SPACING:
			xfdashboard_quicklaunch_set_spacing(self, g_value_get_float(inValue));
			break;

		case PROP_PLANK_STYLE:
			if(self->priv->plankStyle!=g_value_get_boolean(inValue))
			{
				self->priv->plankStyle=g_value_get_boolean(inValue);
				/* Apps button place, dividers and anchored items depend on it */
				if(self->priv->favourites) _xfdashboard_quicklaunch_update_icons_from_property(self);
				clutter_actor_queue_relayout(CLUTTER_ACTOR(self));
			}
			break;

		case PROP_HOVER_ZOOM:
			self->priv->hoverZoom=MAX(1.0f, g_value_get_float(inValue));
			clutter_actor_queue_relayout(CLUTTER_ACTOR(self));
			break;

		case PROP_ORIENTATION:
			xfdashboard_quicklaunch_set_orientation(self, g_value_get_enum(inValue));
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(inObject, inPropID, inSpec);
			break;
	}
}

static void _xfdashboard_quicklaunch_get_property(GObject *inObject,
													guint inPropID,
													GValue *outValue,
													GParamSpec *inSpec)
{
	XfdashboardQuicklaunch			*self=XFDASHBOARD_QUICKLAUNCH(inObject);
	XfdashboardQuicklaunchPrivate	*priv=self->priv;

	switch(inPropID)
	{
		case PROP_FAVOURITES:
			g_value_set_boxed(outValue, priv->favourites);
			break;

		case PROP_NORMAL_ICON_SIZE:
			g_value_set_float(outValue, priv->normalIconSize);
			break;

		case PROP_SPACING:
			g_value_set_float(outValue, priv->spacing);
			break;

		case PROP_PLANK_STYLE:
			g_value_set_boolean(outValue, priv->plankStyle);
			break;

		case PROP_HOVER_ZOOM:
			g_value_set_float(outValue, priv->hoverZoom);
			break;

		case PROP_ORIENTATION:
			g_value_set_enum(outValue, priv->orientation);
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
static void xfdashboard_quicklaunch_class_init(XfdashboardQuicklaunchClass *klass)
{
	XfdashboardActorClass	*actorClass=XFDASHBOARD_ACTOR_CLASS(klass);
	ClutterActorClass		*clutterActorClass=CLUTTER_ACTOR_CLASS(klass);
	GObjectClass			*gobjectClass=G_OBJECT_CLASS(klass);

	/* Override functions */
	gobjectClass->dispose=_xfdashboard_quicklaunch_dispose;
	gobjectClass->set_property=_xfdashboard_quicklaunch_set_property;
	gobjectClass->get_property=_xfdashboard_quicklaunch_get_property;

	clutterActorClass->get_preferred_width=_xfdashboard_quicklaunch_get_preferred_width;
	clutterActorClass->get_preferred_height=_xfdashboard_quicklaunch_get_preferred_height;
	clutterActorClass->allocate=_xfdashboard_quicklaunch_allocate;
	clutterActorClass->paint_node=_xfdashboard_quicklaunch_paint_node;
	clutterActorClass->get_paint_volume=_xfdashboard_quicklaunch_get_paint_volume;

	klass->selection_add_favourite=_xfdashboard_quicklaunch_selection_add_favourite;
	klass->selection_remove_favourite=_xfdashboard_quicklaunch_selection_remove_favourite;
	klass->favourite_reorder_left=_xfdashboard_quicklaunch_favourite_reorder_left;
	klass->favourite_reorder_right=_xfdashboard_quicklaunch_favourite_reorder_right;
	klass->favourite_reorder_up=_xfdashboard_quicklaunch_favourite_reorder_up;
	klass->favourite_reorder_down=_xfdashboard_quicklaunch_favourite_reorder_down;

	/* Define properties */
	/**
	 * XfdashboardQuicklaunch:favourites:
	 *
	 * A %NULL-terminated list of strings where each one contains either the desktop ID
	 * or an absolute file path to the desktop file of a favourite.
	 */
	XfdashboardQuicklaunchProperties[PROP_FAVOURITES]=
		g_param_spec_boxed("favourites",
							"Favourites",
							"An array of strings pointing to desktop files shown as icons",
							G_TYPE_STRV,
							G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	XfdashboardQuicklaunchProperties[PROP_NORMAL_ICON_SIZE]=
		g_param_spec_float("normal-icon-size",
								"Normal icon size",
								"Unscale size of icon",
								1.0, G_MAXFLOAT,
								1.0f,
								G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	XfdashboardQuicklaunchProperties[PROP_SPACING]=
		g_param_spec_float("spacing",
								"Spacing",
								"The spacing between children",
								0.0, G_MAXFLOAT,
								0.0,
								G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	XfdashboardQuicklaunchProperties[PROP_HOVER_ZOOM]=
		g_param_spec_float("hover-zoom",
								"Hover zoom",
								"Zoom factor of item under pointer (1.0 = no zoom)",
								1.0, 3.0,
								1.0,
								G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	XfdashboardQuicklaunchProperties[PROP_PLANK_STYLE]=
		g_param_spec_boolean("plank-style",
								"Plank style",
								"Draw Plank-like running dots and active band (theme opt-in)",
								FALSE,
								G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	XfdashboardQuicklaunchProperties[PROP_ORIENTATION]=
		g_param_spec_enum("orientation",
							"Orientation",
							"The orientation to layout children",
							CLUTTER_TYPE_ORIENTATION,
							CLUTTER_ORIENTATION_VERTICAL,
							G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties(gobjectClass, PROP_LAST, XfdashboardQuicklaunchProperties);

	/* Define stylable properties */
	xfdashboard_actor_install_stylable_property(actorClass, XfdashboardQuicklaunchProperties[PROP_NORMAL_ICON_SIZE]);
	xfdashboard_actor_install_stylable_property(actorClass, XfdashboardQuicklaunchProperties[PROP_SPACING]);
	xfdashboard_actor_install_stylable_property(actorClass, XfdashboardQuicklaunchProperties[PROP_HOVER_ZOOM]);
	xfdashboard_actor_install_stylable_property(actorClass, XfdashboardQuicklaunchProperties[PROP_PLANK_STYLE]);
	xfdashboard_actor_install_stylable_property(actorClass, XfdashboardQuicklaunchProperties[PROP_ORIENTATION]);

	/* Define signals */
	XfdashboardQuicklaunchSignals[SIGNAL_FAVOURITE_ADDED]=
		g_signal_new("favourite-added",
						G_TYPE_FROM_CLASS(klass),
						G_SIGNAL_RUN_LAST,
						G_STRUCT_OFFSET(XfdashboardQuicklaunchClass, favourite_added),
						NULL,
						NULL,
						g_cclosure_marshal_VOID__OBJECT,
						G_TYPE_NONE,
						1,
						G_TYPE_APP_INFO);

	XfdashboardQuicklaunchSignals[SIGNAL_FAVOURITE_REMOVED]=
		g_signal_new("favourite-removed",
						G_TYPE_FROM_CLASS(klass),
						G_SIGNAL_RUN_LAST,
						G_STRUCT_OFFSET(XfdashboardQuicklaunchClass, favourite_removed),
						NULL,
						NULL,
						g_cclosure_marshal_VOID__OBJECT,
						G_TYPE_NONE,
						1,
						G_TYPE_APP_INFO);

	XfdashboardQuicklaunchSignals[ACTION_SELECTION_ADD_FAVOURITE]=
		g_signal_new("selection-add-favourite",
						G_TYPE_FROM_CLASS(klass),
						G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
						G_STRUCT_OFFSET(XfdashboardQuicklaunchClass, selection_add_favourite),
						g_signal_accumulator_true_handled,
						NULL,
						_xfdashboard_marshal_BOOLEAN__OBJECT_STRING_BOXED,
						G_TYPE_BOOLEAN,
						3,
						XFDASHBOARD_TYPE_FOCUSABLE,
						G_TYPE_STRING,
						CLUTTER_TYPE_EVENT);

	XfdashboardQuicklaunchSignals[ACTION_SELECTION_REMOVE_FAVOURITE]=
		g_signal_new("selection-remove-favourite",
						G_TYPE_FROM_CLASS(klass),
						G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
						G_STRUCT_OFFSET(XfdashboardQuicklaunchClass, selection_remove_favourite),
						g_signal_accumulator_true_handled,
						NULL,
						_xfdashboard_marshal_BOOLEAN__OBJECT_STRING_BOXED,
						G_TYPE_BOOLEAN,
						3,
						XFDASHBOARD_TYPE_FOCUSABLE,
						G_TYPE_STRING,
						CLUTTER_TYPE_EVENT);

	XfdashboardQuicklaunchSignals[ACTION_FAVOURITE_REORDER_LEFT]=
		g_signal_new("favourite-reorder-left",
						G_TYPE_FROM_CLASS(klass),
						G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
						G_STRUCT_OFFSET(XfdashboardQuicklaunchClass, favourite_reorder_left),
						g_signal_accumulator_true_handled,
						NULL,
						_xfdashboard_marshal_BOOLEAN__OBJECT_STRING_BOXED,
						G_TYPE_BOOLEAN,
						3,
						XFDASHBOARD_TYPE_FOCUSABLE,
						G_TYPE_STRING,
						CLUTTER_TYPE_EVENT);

	XfdashboardQuicklaunchSignals[ACTION_FAVOURITE_REORDER_RIGHT]=
		g_signal_new("favourite-reorder-right",
						G_TYPE_FROM_CLASS(klass),
						G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
						G_STRUCT_OFFSET(XfdashboardQuicklaunchClass, favourite_reorder_right),
						g_signal_accumulator_true_handled,
						NULL,
						_xfdashboard_marshal_BOOLEAN__OBJECT_STRING_BOXED,
						G_TYPE_BOOLEAN,
						3,
						XFDASHBOARD_TYPE_FOCUSABLE,
						G_TYPE_STRING,
						CLUTTER_TYPE_EVENT);

	XfdashboardQuicklaunchSignals[ACTION_FAVOURITE_REORDER_UP]=
		g_signal_new("favourite-reorder-up",
						G_TYPE_FROM_CLASS(klass),
						G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
						G_STRUCT_OFFSET(XfdashboardQuicklaunchClass, favourite_reorder_up),
						g_signal_accumulator_true_handled,
						NULL,
						_xfdashboard_marshal_BOOLEAN__OBJECT_STRING_BOXED,
						G_TYPE_BOOLEAN,
						3,
						XFDASHBOARD_TYPE_FOCUSABLE,
						G_TYPE_STRING,
						CLUTTER_TYPE_EVENT);

	XfdashboardQuicklaunchSignals[ACTION_FAVOURITE_REORDER_DOWN]=
		g_signal_new("favourite-reorder-down",
						G_TYPE_FROM_CLASS(klass),
						G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
						G_STRUCT_OFFSET(XfdashboardQuicklaunchClass, favourite_reorder_down),
						g_signal_accumulator_true_handled,
						NULL,
						_xfdashboard_marshal_BOOLEAN__OBJECT_STRING_BOXED,
						G_TYPE_BOOLEAN,
						3,
						XFDASHBOARD_TYPE_FOCUSABLE,
						G_TYPE_STRING,
						CLUTTER_TYPE_EVENT);
}

/* Object initialization
 * Create private structure and set up default values
 */
static void xfdashboard_quicklaunch_init(XfdashboardQuicklaunch *self)
{
	XfdashboardQuicklaunchPrivate	*priv;
	ClutterRequestMode				requestMode;
	ClutterAction					*dropAction;

	priv=self->priv=xfdashboard_quicklaunch_get_instance_private(self);

	/* Set up default values */
	priv->favourites=NULL;
	priv->spacing=0.0f;
	priv->hoverZoom=1.0f;
	priv->plankStyle=FALSE;
	priv->orientation=CLUTTER_ORIENTATION_VERTICAL;
	priv->normalIconSize=1.0f;
	priv->scaleCurrent=DEFAULT_SCALE_MAX;
	priv->scaleMin=DEFAULT_SCALE_MIN;
	priv->scaleMax=DEFAULT_SCALE_MAX;
	priv->scaleStep=DEFAULT_SCALE_STEP;
	priv->dragMode=DRAG_MODE_NONE;
	priv->dragPreviewIcon=NULL;
	priv->selectedItem=NULL;
	priv->appDB=xfdashboard_core_get_application_database(NULL);
	priv->settings=g_object_ref(xfdashboard_core_get_settings(NULL));

	/* Set up this actor */
	clutter_actor_set_reactive(CLUTTER_ACTOR(self), TRUE);

	/* Hover zoom of items */
	priv->zoomPointer=-1.0f;
	priv->separatorCount=0;
	g_signal_connect(self, "actor-added", G_CALLBACK(_xfdashboard_quicklaunch_on_actor_added), NULL);
	priv->dotTexture=NULL;
	priv->activeTexture=NULL;
	priv->windowsChangedIdleID=0;
	{
		XfdashboardWindowTracker	*windowTracker=xfdashboard_core_get_window_tracker(NULL);

		g_signal_connect_object(windowTracker, "window-opened", G_CALLBACK(_xfdashboard_quicklaunch_on_windows_changed), self, G_CONNECT_SWAPPED);
		g_signal_connect_object(windowTracker, "window-closed", G_CALLBACK(_xfdashboard_quicklaunch_on_windows_changed), self, G_CONNECT_SWAPPED);
	}
	priv->zoomStrength=priv->zoomStrengthFrom=priv->zoomStrengthTo=0.0f;
	priv->zoomTimeline=clutter_timeline_new(180);
	clutter_timeline_set_progress_mode(priv->zoomTimeline, CLUTTER_EASE_OUT_CUBIC);
	g_signal_connect(priv->zoomTimeline, "new-frame", G_CALLBACK(_xfdashboard_quicklaunch_zoom_on_new_frame), self);
	g_signal_connect(self, "captured-event", G_CALLBACK(_xfdashboard_quicklaunch_on_captured_event), NULL);
	g_signal_connect(self, "leave-event", G_CALLBACK(_xfdashboard_quicklaunch_on_leave), NULL);
	requestMode=(priv->orientation==CLUTTER_ORIENTATION_HORIZONTAL ? CLUTTER_REQUEST_HEIGHT_FOR_WIDTH : CLUTTER_REQUEST_WIDTH_FOR_HEIGHT);
	clutter_actor_set_request_mode(CLUTTER_ACTOR(self), requestMode);

	dropAction=xfdashboard_drop_action_new();
	clutter_actor_add_action(CLUTTER_ACTOR(self), dropAction);
	g_signal_connect_swapped(dropAction, "begin", G_CALLBACK(_xfdashboard_quicklaunch_on_drop_begin), self);
	g_signal_connect_swapped(dropAction, "drop", G_CALLBACK(_xfdashboard_quicklaunch_on_drop_drop), self);
	g_signal_connect_swapped(dropAction, "end", G_CALLBACK(_xfdashboard_quicklaunch_on_drop_end), self);
	g_signal_connect_swapped(dropAction, "drag-enter", G_CALLBACK(_xfdashboard_quicklaunch_on_drop_enter), self);
	g_signal_connect_swapped(dropAction, "drag-motion", G_CALLBACK(_xfdashboard_quicklaunch_on_drop_motion), self);
	g_signal_connect_swapped(dropAction, "drag-leave", G_CALLBACK(_xfdashboard_quicklaunch_on_drop_leave), self);

	/* Add "applications" button */
	priv->appsButton=xfdashboard_toggle_button_new_with_text(_("Applications"));
	clutter_actor_set_name(priv->appsButton, "applications-button");
	xfdashboard_label_set_icon_size(XFDASHBOARD_LABEL(priv->appsButton), priv->normalIconSize);
	xfdashboard_label_set_sync_icon_size(XFDASHBOARD_LABEL(priv->appsButton), FALSE);
	clutter_actor_add_child(CLUTTER_ACTOR(self), priv->appsButton);

	/* Next add trash button to box but initially hidden and register as drop target */
	priv->trashButton=xfdashboard_toggle_button_new_with_text(_("Remove"));
	clutter_actor_set_name(priv->trashButton, "trash-button");
	clutter_actor_hide(priv->trashButton);
	xfdashboard_label_set_icon_size(XFDASHBOARD_LABEL(priv->trashButton), priv->normalIconSize);
	xfdashboard_label_set_sync_icon_size(XFDASHBOARD_LABEL(priv->trashButton), FALSE);
	clutter_actor_add_child(CLUTTER_ACTOR(self), priv->trashButton);

	dropAction=xfdashboard_drop_action_new();
	clutter_actor_add_action(priv->trashButton, CLUTTER_ACTION(dropAction));
	g_signal_connect_swapped(dropAction, "begin", G_CALLBACK(_xfdashboard_quicklaunch_on_trash_drop_begin), self);
	g_signal_connect_swapped(dropAction, "drop", G_CALLBACK(_xfdashboard_quicklaunch_on_trash_drop_drop), self);
	g_signal_connect_swapped(dropAction, "end", G_CALLBACK(_xfdashboard_quicklaunch_on_trash_drop_end), self);
	g_signal_connect_swapped(dropAction, "drag-enter", G_CALLBACK(_xfdashboard_quicklaunch_on_trash_drop_enter), self);
	g_signal_connect_swapped(dropAction, "drag-leave", G_CALLBACK(_xfdashboard_quicklaunch_on_trash_drop_leave), self);

	/* Add a hidden actor used as separator between application buttons for
	 * favourites and dynamically added one (for running non-favourite applications).
	 * It used to add application buttons for favourites before dynamically
	 * added ones.
	 */
	priv->separatorFavouritesToDynamic=clutter_actor_new();
	clutter_actor_hide(priv->separatorFavouritesToDynamic);
	clutter_actor_add_child(CLUTTER_ACTOR(self), priv->separatorFavouritesToDynamic);

	/* Bind to settings to react on changes */
	priv->settingsFavouritesBinding=
		g_object_bind_property(priv->settings,
								"favourites",
								self,
								"favourites",
								G_BINDING_SYNC_CREATE);

	/* Connect to application tracker to recognize other running application
	 * which are not known favourites.
	 */
	priv->appTracker=xfdashboard_core_get_application_tracker(NULL);
	g_signal_connect_swapped(priv->appTracker, "state-changed", G_CALLBACK(_xfdashboard_quicklaunch_on_app_tracker_state_changed), self);
}

/* IMPLEMENTATION: Public API */

/* Create new actor */
ClutterActor* xfdashboard_quicklaunch_new(void)
{
	return(g_object_new(XFDASHBOARD_TYPE_QUICKLAUNCH, NULL));
}

ClutterActor* xfdashboard_quicklaunch_new_with_orientation(ClutterOrientation inOrientation)
{
	g_return_val_if_fail(inOrientation==CLUTTER_ORIENTATION_HORIZONTAL || inOrientation==CLUTTER_ORIENTATION_VERTICAL, NULL);

	return(g_object_new(XFDASHBOARD_TYPE_QUICKLAUNCH,
						"orientation", inOrientation,
						NULL));
}

/* Get/set spacing between children */
gfloat xfdashboard_quicklaunch_get_normal_icon_size(XfdashboardQuicklaunch *self)
{
	g_return_val_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self), 0.0f);

	return(self->priv->normalIconSize);
}

void xfdashboard_quicklaunch_set_normal_icon_size(XfdashboardQuicklaunch *self, const gfloat inIconSize)
{
	XfdashboardQuicklaunchPrivate	*priv;

	g_return_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self));
	g_return_if_fail(inIconSize>=0.0f);

	priv=self->priv;

	/* Set value if changed */
	if(priv->normalIconSize!=inIconSize)
	{
		/* Set value */
		priv->normalIconSize=inIconSize;
		clutter_actor_queue_relayout(CLUTTER_ACTOR(self));

		xfdashboard_label_set_icon_size(XFDASHBOARD_LABEL(priv->appsButton), priv->normalIconSize);
		xfdashboard_label_set_icon_size(XFDASHBOARD_LABEL(priv->trashButton), priv->normalIconSize);

		/* Notify about property change */
		g_object_notify_by_pspec(G_OBJECT(self), XfdashboardQuicklaunchProperties[PROP_NORMAL_ICON_SIZE]);
	}
}

/* Get/set spacing between children */
gfloat xfdashboard_quicklaunch_get_spacing(XfdashboardQuicklaunch *self)
{
	g_return_val_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self), 0.0f);

	return(self->priv->spacing);
}

void xfdashboard_quicklaunch_set_spacing(XfdashboardQuicklaunch *self, const gfloat inSpacing)
{
	XfdashboardQuicklaunchPrivate	*priv;

	g_return_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self));
	g_return_if_fail(inSpacing>=0.0f);

	priv=self->priv;

	/* Set value if changed */
	if(priv->spacing!=inSpacing)
	{
		/* Set value */
		priv->spacing=inSpacing;
		clutter_actor_queue_relayout(CLUTTER_ACTOR(self));

		xfdashboard_background_set_corner_radius(XFDASHBOARD_BACKGROUND(self), priv->spacing);

		/* Notify about property change */
		g_object_notify_by_pspec(G_OBJECT(self), XfdashboardQuicklaunchProperties[PROP_SPACING]);
	}
}

/* Get/set orientation */
ClutterOrientation xfdashboard_quicklaunch_get_orientation(XfdashboardQuicklaunch *self)
{
	g_return_val_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self), CLUTTER_ORIENTATION_VERTICAL);

	return(self->priv->orientation);
}

void xfdashboard_quicklaunch_set_orientation(XfdashboardQuicklaunch *self, ClutterOrientation inOrientation)
{
	XfdashboardQuicklaunchPrivate	*priv;
	ClutterRequestMode				requestMode;

	g_return_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self));
	g_return_if_fail(inOrientation==CLUTTER_ORIENTATION_HORIZONTAL ||
						inOrientation==CLUTTER_ORIENTATION_VERTICAL);

	priv=self->priv;

	/* Set value if changed */
	if(priv->orientation!=inOrientation)
	{
		/* Set value */
		priv->orientation=inOrientation;

		requestMode=(priv->orientation==CLUTTER_ORIENTATION_HORIZONTAL ? CLUTTER_REQUEST_HEIGHT_FOR_WIDTH : CLUTTER_REQUEST_WIDTH_FOR_HEIGHT);
		clutter_actor_set_request_mode(CLUTTER_ACTOR(self), requestMode);

		clutter_actor_queue_relayout(CLUTTER_ACTOR(self));

		/* Notify about property change */
		g_object_notify_by_pspec(G_OBJECT(self), XfdashboardQuicklaunchProperties[PROP_ORIENTATION]);
	}
}

/* Get apps button */
XfdashboardToggleButton* xfdashboard_quicklaunch_get_apps_button(XfdashboardQuicklaunch *self)
{
	g_return_val_if_fail(XFDASHBOARD_IS_QUICKLAUNCH(self), NULL);

	return(XFDASHBOARD_TOGGLE_BUTTON(self->priv->appsButton));
}
