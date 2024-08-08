/*
 * Virt Viewer: A virtual machine console viewer
 *
 * Copyright (C) 2007-2012 Red Hat, Inc.
 * Copyright (C) 2009-2012 Daniel P. Berrange
 * Copyright (C) 2010 Marc-André Lureau
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Daniel P. Berrange <berrange@redhat.com>
 */

#include <config.h>

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <locale.h>
#include <glib/gprintf.h>
#include <glib/gi18n.h>
#include <math.h>

#include "virt-viewer-window.h"
#include "virt-viewer-display.h"
#include "virt-viewer-session.h"
#include "virt-viewer-app.h"
#include "virt-viewer-util.h"
#include "virt-viewer-timed-revealer.h"
#include "virt-viewer-display-vte.h"

#include "remote-viewer-iso-list-dialog.h"

#define ZOOM_STEP 10

/* Signal handlers for main window (move in a VirtViewerMainWindow?) */
gboolean virt_viewer_window_delete(GtkWidget *src, void *dummy, VirtViewerWindow *self);
void virt_viewer_window_guest_details_response(GtkDialog *dialog, gint response_id, gpointer user_data);

/* Internal methods */
static void virt_viewer_window_enable_modifiers(VirtViewerWindow *self);
static void virt_viewer_window_disable_modifiers(VirtViewerWindow *self);
static void virt_viewer_window_queue_resize(VirtViewerWindow *self);
static GMenu* virt_viewer_window_get_keycombo_menu(VirtViewerWindow *self);
static void virt_viewer_window_get_minimal_dimensions(VirtViewerWindow *self, guint *width, guint *height);
static gint virt_viewer_window_get_minimal_zoom_level(VirtViewerWindow *self);
static void virt_viewer_window_set_fullscreen(VirtViewerWindow *self,
                                              gboolean fullscreen);

enum {
    PROP_0,
    PROP_WINDOW,
    PROP_DISPLAY,
    PROP_SUBTITLE,
    PROP_APP,
    PROP_KEYMAP,
};

struct _VirtViewerWindow {
    GObject parent;
    VirtViewerApp *app;

    GtkBuilder *builder;
    GtkWidget *window;
    GtkAccelGroup *accel_group;
    VirtViewerNotebook *notebook;
    VirtViewerDisplay *display;
    VirtViewerTimedRevealer *revealer;

    gboolean accel_enabled;
    GValue accel_setting;
    GSList *accel_list;
    gboolean enable_mnemonics_save;
    gboolean grabbed;
    gint fullscreen_monitor;
    gboolean desktop_resize_pending;
    gboolean kiosk;

    gint zoomlevel;
    gboolean fullscreen;
    gchar *subtitle;
    gboolean initial_zoom_set;
    VirtViewerKeyMapping *keyMappings;
};

G_DEFINE_TYPE(VirtViewerWindow, virt_viewer_window, G_TYPE_OBJECT)

static void
virt_viewer_window_get_property (GObject *object, guint property_id,
                                 GValue *value, GParamSpec *pspec)
{
    VirtViewerWindow *self = VIRT_VIEWER_WINDOW(object);

    switch (property_id) {
    case PROP_SUBTITLE:
        g_value_set_string(value, self->subtitle);
        break;

    case PROP_WINDOW:
        g_value_set_object(value, self->window);
        break;

    case PROP_DISPLAY:
        g_value_set_object(value, virt_viewer_window_get_display(self));
        break;

    case PROP_APP:
        g_value_set_object(value, self->app);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
virt_viewer_window_set_property (GObject *object, guint property_id,
                                 const GValue *value, GParamSpec *pspec)
{
    VirtViewerWindow *self = VIRT_VIEWER_WINDOW(object);

    switch (property_id) {
    case PROP_SUBTITLE:
        g_free(self->subtitle);
        self->subtitle = g_value_dup_string(value);
        virt_viewer_window_update_title(VIRT_VIEWER_WINDOW(object));
        break;

    case PROP_APP:
        g_return_if_fail(self->app == NULL);
        self->app = g_value_get_object(value);
        break;

    case PROP_KEYMAP:
        g_free(self->keyMappings);
        self->keyMappings = (VirtViewerKeyMapping *)g_value_get_pointer(value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
virt_viewer_window_dispose (GObject *object)
{
    VirtViewerWindow *self = VIRT_VIEWER_WINDOW(object);
    GSList *it;

    if (self->display) {
        g_object_unref(self->display);
        self->display = NULL;
    }

    g_debug("Disposing window %p\n", object);

    if (self->window) {
        gtk_widget_destroy(self->window);
        self->window = NULL;
    }
    if (self->builder) {
        g_object_unref(self->builder);
        self->builder = NULL;
    }

    self->revealer = NULL;

    for (it = self->accel_list ; it != NULL ; it = it->next) {
        g_object_unref(G_OBJECT(it->data));
    }
    g_slist_free(self->accel_list);
    self->accel_list = NULL;

    g_free(self->subtitle);
    self->subtitle = NULL;

    g_value_unset(&self->accel_setting);

    G_OBJECT_CLASS (virt_viewer_window_parent_class)->dispose (object);
}

static void
rebuild_combo_menu(GObject    *gobject G_GNUC_UNUSED,
                   GParamSpec *pspec G_GNUC_UNUSED,
                   gpointer    user_data)
{
    VirtViewerWindow *self = user_data;
    GObject *button;
    GMenu *menu;

    menu = virt_viewer_window_get_keycombo_menu(self);

    button = gtk_builder_get_object(self->builder, "header-send-key");
    gtk_menu_button_set_menu_model(
        GTK_MENU_BUTTON(button),
        G_MENU_MODEL(menu));

    button = gtk_builder_get_object(self->builder, "toolbar-send-key");
    gtk_menu_button_set_menu_model(
        GTK_MENU_BUTTON(button),
        G_MENU_MODEL(menu));
}

static void
virt_viewer_window_constructed(GObject *object)
{
    VirtViewerWindow *self = VIRT_VIEWER_WINDOW(object);

    if (G_OBJECT_CLASS(virt_viewer_window_parent_class)->constructed)
        G_OBJECT_CLASS(virt_viewer_window_parent_class)->constructed(object);

    g_signal_connect(self->app, "notify::release-cursor-display-hotkey",
                     G_CALLBACK(rebuild_combo_menu), object);
    rebuild_combo_menu(NULL, NULL, object);
}

static void
virt_viewer_window_class_init (VirtViewerWindowClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = virt_viewer_window_get_property;
    object_class->set_property = virt_viewer_window_set_property;
    object_class->dispose = virt_viewer_window_dispose;
    object_class->constructed = virt_viewer_window_constructed;

    g_object_class_install_property(object_class,
                                    PROP_SUBTITLE,
                                    g_param_spec_string("subtitle",
                                                        "Subtitle",
                                                        "Window subtitle",
                                                        "",
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(object_class,
                                    PROP_WINDOW,
                                    g_param_spec_object("window",
                                                        "Window",
                                                        "GtkWindow",
                                                        GTK_TYPE_WIDGET,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(object_class,
                                    PROP_DISPLAY,
                                    g_param_spec_object("display",
                                                        "Display",
                                                        "VirtDisplay",
                                                        VIRT_VIEWER_TYPE_DISPLAY,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(object_class,
                                    PROP_APP,
                                    g_param_spec_object("app",
                                                        "App",
                                                        "VirtViewerApp",
                                                        VIRT_VIEWER_TYPE_APP,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(object_class,
                                    PROP_KEYMAP,
                                    g_param_spec_pointer("keymap",
                                                        "keymap",
                                                        "Remapped keys",
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_STATIC_STRINGS));

}

static void
virt_viewer_window_action_zoom_out(GSimpleAction *act G_GNUC_UNUSED,
                                   GVariant *param G_GNUC_UNUSED,
                                   gpointer opaque)
{
    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(opaque));

    virt_viewer_window_zoom_out(VIRT_VIEWER_WINDOW(opaque));
}

static void
virt_viewer_window_action_zoom_in(GSimpleAction *act G_GNUC_UNUSED,
                                  GVariant *param G_GNUC_UNUSED,
                                  gpointer opaque)
{
    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(opaque));

    virt_viewer_window_zoom_in(VIRT_VIEWER_WINDOW(opaque));
}

static void
virt_viewer_window_action_zoom_reset(GSimpleAction *act G_GNUC_UNUSED,
                                     GVariant *param G_GNUC_UNUSED,
                                     gpointer opaque)
{
    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(opaque));

    virt_viewer_window_zoom_reset(VIRT_VIEWER_WINDOW(opaque));
}

static void
virt_viewer_window_action_quit(GSimpleAction *act G_GNUC_UNUSED,
                               GVariant *param G_GNUC_UNUSED,
                               gpointer opaque)
{
    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(opaque));

    VirtViewerWindow *self = VIRT_VIEWER_WINDOW(opaque);

    virt_viewer_app_maybe_quit(self->app, self);
}

static void
virt_viewer_window_action_minimize(GSimpleAction *act G_GNUC_UNUSED,
                                   GVariant *param G_GNUC_UNUSED,
                                   gpointer opaque)
{
    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(opaque));

    VirtViewerWindow *self = VIRT_VIEWER_WINDOW(opaque);

    gtk_window_iconify(GTK_WINDOW(self->window));
}

static void
virt_viewer_window_action_about(GSimpleAction *act G_GNUC_UNUSED,
                                GVariant *param G_GNUC_UNUSED,
                                gpointer opaque)
{
    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(opaque));

    virt_viewer_window_show_about(VIRT_VIEWER_WINDOW(opaque));
}

static void
virt_viewer_window_action_guest_details(GSimpleAction *act G_GNUC_UNUSED,
                                        GVariant *param G_GNUC_UNUSED,
                                        gpointer opaque)
{
    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(opaque));

    virt_viewer_window_show_guest_details(VIRT_VIEWER_WINDOW(opaque));
}

static void
virt_viewer_window_action_fullscreen(GSimpleAction *act,
                                     GVariant *state,
                                     gpointer opaque)
{
    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(opaque));

    VirtViewerWindow *self = VIRT_VIEWER_WINDOW(opaque);
    gboolean fullscreen = g_variant_get_boolean(state);

    g_simple_action_set_state(act, g_variant_new_boolean(fullscreen));

    virt_viewer_window_set_fullscreen(self, fullscreen);
}

static void
virt_viewer_window_action_send_key(GSimpleAction *act G_GNUC_UNUSED,
                                   GVariant *param,
                                   gpointer opaque)
{
    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(opaque));

    VirtViewerWindow *self = VIRT_VIEWER_WINDOW(opaque);

    g_return_if_fail(self->display != NULL);
    gsize nkeys = 0;
    const guint *keys = g_variant_get_fixed_array(param,
                                                  &nkeys,
                                                  sizeof(guint32));
    g_return_if_fail(keys != NULL);

    virt_viewer_display_send_keys(VIRT_VIEWER_DISPLAY(self->display),
                                  keys, nkeys);
}

static void
virt_viewer_window_action_screenshot(GSimpleAction *act G_GNUC_UNUSED,
                                     GVariant *param G_GNUC_UNUSED,
                                     gpointer opaque)
{
    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(opaque));

    virt_viewer_window_screenshot(VIRT_VIEWER_WINDOW(opaque));
}

static void
virt_viewer_window_action_usb_device_select(GSimpleAction *act G_GNUC_UNUSED,
                                            GVariant *param G_GNUC_UNUSED,
                                            gpointer opaque)
{
    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(opaque));

    VirtViewerWindow *self = VIRT_VIEWER_WINDOW(opaque);

    virt_viewer_session_usb_device_selection(virt_viewer_app_get_session(self->app),
                                             GTK_WINDOW(self->window));
}

static void
virt_viewer_window_action_usb_device_reset(GSimpleAction *act G_GNUC_UNUSED,
                                           GVariant *param G_GNUC_UNUSED,
                                           gpointer opaque)
{
    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(opaque));

    VirtViewerWindow *self = VIRT_VIEWER_WINDOW(opaque);

    virt_viewer_session_usb_device_reset(virt_viewer_app_get_session(self->app));
}


static void
virt_viewer_window_action_release_cursor(GSimpleAction *act G_GNUC_UNUSED,
                                         GVariant *param G_GNUC_UNUSED,
                                         gpointer opaque)
{
    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(opaque));

    VirtViewerWindow *self = VIRT_VIEWER_WINDOW(opaque);

    g_return_if_fail(self->display != NULL);
    virt_viewer_display_release_cursor(VIRT_VIEWER_DISPLAY(self->display));
}

static void
virt_viewer_window_action_preferences(GSimpleAction *act G_GNUC_UNUSED,
                                      GVariant *param G_GNUC_UNUSED,
                                      gpointer opaque)
{
    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(opaque));

    VirtViewerWindow *self = VIRT_VIEWER_WINDOW(opaque);

    virt_viewer_app_show_preferences(self->app, self->window);
}

static void
virt_viewer_window_action_change_cd(GSimpleAction *act G_GNUC_UNUSED,
                                    GVariant *param G_GNUC_UNUSED,
                                    gpointer opaque)
{
    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(opaque));

    virt_viewer_window_change_cd(VIRT_VIEWER_WINDOW(opaque));
}

static void
virt_viewer_window_action_secure_attention(GSimpleAction *action G_GNUC_UNUSED,
                                           GVariant *param G_GNUC_UNUSED,
                                           gpointer opaque)
{
    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(opaque));

    VirtViewerWindow *self =  VIRT_VIEWER_WINDOW(opaque);
    guint keys[] = { GDK_KEY_Control_L, GDK_KEY_Alt_L, GDK_KEY_Delete };

    virt_viewer_display_send_keys(VIRT_VIEWER_DISPLAY(self->display),
                                  keys, G_N_ELEMENTS(keys));
}

static GActionEntry actions[] = {
    { .name = "zoom-out",
      .activate = virt_viewer_window_action_zoom_out },
    { .name = "zoom-in",
      .activate = virt_viewer_window_action_zoom_in },
    { .name = "zoom-reset",
      .activate = virt_viewer_window_action_zoom_reset },
    { .name = "quit",
      .activate = virt_viewer_window_action_quit },
    { .name = "minimize",
      .activate = virt_viewer_window_action_minimize },
    { .name = "about",
      .activate = virt_viewer_window_action_about },
    { .name = "guest-details",
      .activate = virt_viewer_window_action_guest_details },
    { .name = "fullscreen",
      .state = "false",
      .change_state = virt_viewer_window_action_fullscreen },
    { .name = "send-key",
      .parameter_type = "au",
      .activate = virt_viewer_window_action_send_key },
    { .name = "screenshot",
      .activate = virt_viewer_window_action_screenshot },
    { .name = "usb-device-select",
      .activate = virt_viewer_window_action_usb_device_select },
    { .name = "usb-device-reset",
      .activate = virt_viewer_window_action_usb_device_reset },
    { .name = "release-cursor",
      .activate = virt_viewer_window_action_release_cursor },
    { .name = "preferences",
      .activate = virt_viewer_window_action_preferences },
    { .name = "change-cd",
      .activate = virt_viewer_window_action_change_cd },
    { .name = "secure-attention",
      .activate = virt_viewer_window_action_secure_attention },
};

static void
virt_viewer_window_init (VirtViewerWindow *self)
{
    GtkWidget *overlay;
    GtkWidget *toolbar;
    GSList *accels;
    GObject *menu;
    GtkBuilder *menuBuilder;

    self->fullscreen_monitor = -1;
    g_value_init(&self->accel_setting, G_TYPE_STRING);

    self->notebook = virt_viewer_notebook_new();
    gtk_widget_show(GTK_WIDGET(self->notebook));

    self->builder = virt_viewer_util_load_ui("virt-viewer.ui");

    gtk_builder_connect_signals(self->builder, self);

    self->accel_group = GTK_ACCEL_GROUP(gtk_builder_get_object(self->builder, "accelgroup"));

    overlay = GTK_WIDGET(gtk_builder_get_object(self->builder, "viewer-overlay"));
    toolbar = GTK_WIDGET(gtk_builder_get_object(self->builder, "toolbar"));

    gtk_container_remove(GTK_CONTAINER(overlay), toolbar);
    gtk_container_add(GTK_CONTAINER(overlay), GTK_WIDGET(self->notebook));
    self->revealer = virt_viewer_timed_revealer_new(toolbar);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), GTK_WIDGET(self->revealer));

    self->window = GTK_WIDGET(gtk_builder_get_object(self->builder, "viewer"));

    g_action_map_add_action_entries(G_ACTION_MAP(self->window), actions,
                                    G_N_ELEMENTS(actions), self);

    gtk_window_add_accel_group(GTK_WINDOW(self->window), self->accel_group);

    menuBuilder =
        gtk_builder_new_from_resource(VIRT_VIEWER_RESOURCE_PREFIX "/ui/virt-viewer-menus.ui");

    menu = gtk_builder_get_object(self->builder, "header-action");
    gtk_menu_button_set_menu_model(
        GTK_MENU_BUTTON(menu),
        G_MENU_MODEL(gtk_builder_get_object(menuBuilder, "action-menu")));

    menu = gtk_builder_get_object(self->builder, "header-machine");
    gtk_menu_button_set_menu_model(
        GTK_MENU_BUTTON(menu),
        G_MENU_MODEL(gtk_builder_get_object(menuBuilder, "machine-menu")));
    menu = gtk_builder_get_object(self->builder, "header-video-settings");
    gtk_menu_button_set_menu_model(
        GTK_MENU_BUTTON(menu),
        G_MENU_MODEL(gtk_builder_get_object(menuBuilder, "video-settings-menu"))); 

    menu = gtk_builder_get_object(self->builder, "toolbar-action");
    gtk_menu_button_set_menu_model(
        GTK_MENU_BUTTON(menu),
        G_MENU_MODEL(gtk_builder_get_object(menuBuilder, "action-menu")));

    menu = gtk_builder_get_object(self->builder, "toolbar-machine");
    gtk_menu_button_set_menu_model(
        GTK_MENU_BUTTON(menu),
        G_MENU_MODEL(gtk_builder_get_object(menuBuilder, "machine-menu")));

    menu = gtk_builder_get_object(self->builder, "toolbar-video-settings");
    gtk_menu_button_set_menu_model(
        GTK_MENU_BUTTON(menu),
        G_MENU_MODEL(gtk_builder_get_object(menuBuilder, "video-settings-menu")));

    virt_viewer_window_update_title(self);
    gtk_window_set_resizable(GTK_WINDOW(self->window), TRUE);
    self->accel_enabled = TRUE;

    accels = gtk_accel_groups_from_object(G_OBJECT(self->window));
    for ( ; accels ; accels = accels->next) {
        self->accel_list = g_slist_append(self->accel_list, accels->data);
        g_object_ref(G_OBJECT(accels->data));
    }

    self->zoomlevel = NORMAL_ZOOM_LEVEL;
}

static void
virt_viewer_window_desktop_resize(VirtViewerDisplay *display G_GNUC_UNUSED,
                                  VirtViewerWindow *self)
{
    if (!gtk_widget_get_visible(self->window)) {
        self->desktop_resize_pending = TRUE;
        return;
    }
    virt_viewer_window_queue_resize(self);
}

static gint
virt_viewer_window_get_real_zoom_level(VirtViewerWindow *self)
{
    GtkAllocation allocation;
    guint width, height;

    g_return_val_if_fail(self->display != NULL, NORMAL_ZOOM_LEVEL);

    gtk_widget_get_allocation(GTK_WIDGET(self->display), &allocation);
    virt_viewer_display_get_desktop_size(self->display, &width, &height);

    return round((double) NORMAL_ZOOM_LEVEL * allocation.width / width);
}

void
virt_viewer_window_zoom_out(VirtViewerWindow *self)
{
    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(self));

    if (VIRT_VIEWER_IS_DISPLAY_VTE(self->display)) {
        virt_viewer_display_vte_zoom_out(VIRT_VIEWER_DISPLAY_VTE(self->display));
    } else {
        virt_viewer_window_set_zoom_level(self,
                                          virt_viewer_window_get_real_zoom_level(self) - ZOOM_STEP);
    }
}

void
virt_viewer_window_zoom_in(VirtViewerWindow *self)
{
    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(self));

    if (VIRT_VIEWER_IS_DISPLAY_VTE(self->display)) {
        virt_viewer_display_vte_zoom_in(VIRT_VIEWER_DISPLAY_VTE(self->display));
    } else {
        virt_viewer_window_set_zoom_level(self,
                                          virt_viewer_window_get_real_zoom_level(self) + ZOOM_STEP);
    }
}

void
virt_viewer_window_zoom_reset(VirtViewerWindow *self)
{
    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(self));

    if (VIRT_VIEWER_IS_DISPLAY_VTE(self->display)) {
        virt_viewer_display_vte_zoom_reset(VIRT_VIEWER_DISPLAY_VTE(self->display));
    } else {
        virt_viewer_window_set_zoom_level(self, NORMAL_ZOOM_LEVEL);
    }
}

/* Kick GtkWindow to tell it to adjust to our new widget sizes */
static void
virt_viewer_window_queue_resize(VirtViewerWindow *self)
{
    GtkRequisition nat;
    GtkWidget *child;
    guint border;

    border = gtk_container_get_border_width(GTK_CONTAINER(self->window));
    child = gtk_bin_get_child(GTK_BIN(self->window));
    gtk_window_set_default_size(GTK_WINDOW(self->window), -1, -1);
    gtk_widget_get_preferred_size(child, NULL, &nat);
    gtk_window_resize(GTK_WINDOW(self->window), nat.width + border, nat.height + border);
}

static void
virt_viewer_window_move_to_monitor(VirtViewerWindow *self)
{
    GdkRectangle mon;
    gint n = self->fullscreen_monitor;

    if (n == -1)
        return;

    gdk_screen_get_monitor_geometry(gdk_screen_get_default(), n, &mon);
    gtk_window_move(GTK_WINDOW(self->window), mon.x, mon.y);

    gtk_widget_set_size_request(self->window,
                                mon.width,
                                mon.height);
}

static gboolean
mapped(GtkWidget *widget, GdkEvent *event G_GNUC_UNUSED,
       VirtViewerWindow *self)
{
    g_signal_handlers_disconnect_by_func(widget, mapped, self);
    self->fullscreen = FALSE;
    virt_viewer_window_enter_fullscreen(self, self->fullscreen_monitor);
    return FALSE;
}

void
virt_viewer_window_leave_fullscreen(VirtViewerWindow *self)
{
    /* if we enter and leave fullscreen mode before being shown, make sure to
     * disconnect the mapped signal handler */
    g_signal_handlers_disconnect_by_func(self->window, mapped, self);

    if (!self->fullscreen)
        return;

    self->fullscreen = FALSE;
    self->fullscreen_monitor = -1;
    if (self->display) {
        virt_viewer_display_set_monitor(self->display, -1);
        virt_viewer_display_set_fullscreen(self->display, FALSE);
    }
    virt_viewer_timed_revealer_force_reveal(self->revealer, FALSE);
    gtk_widget_set_size_request(self->window, -1, -1);
    gtk_window_unfullscreen(GTK_WINDOW(self->window));

}

void
virt_viewer_window_enter_fullscreen(VirtViewerWindow *self, gint monitor)
{
    if (self->fullscreen && self->fullscreen_monitor != monitor)
        virt_viewer_window_leave_fullscreen(self);

    if (self->fullscreen)
        return;

    self->fullscreen_monitor = monitor;
    self->fullscreen = TRUE;

    if (!gtk_widget_get_mapped(self->window)) {
        /*
         * To avoid some races with metacity, the window should be placed
         * as early as possible, before it is (re)allocated & mapped
         * Position & size should not be queried yet. (rhbz#809546).
         */
        virt_viewer_window_move_to_monitor(self);
        g_signal_connect(self->window, "map-event", G_CALLBACK(mapped), self);
        return;
    }

    if (!self->kiosk) {
        virt_viewer_timed_revealer_force_reveal(self->revealer, TRUE);
    }

    if (self->display) {
        virt_viewer_display_set_monitor(self->display, monitor);
        virt_viewer_display_set_fullscreen(self->display, TRUE);
    }
    virt_viewer_window_move_to_monitor(self);

    if (monitor == -1) {
        // just go fullscreen on the current monitor
        gtk_window_fullscreen(GTK_WINDOW(self->window));
    } else {
        gtk_window_fullscreen_on_monitor(GTK_WINDOW(self->window),
                                         gdk_screen_get_default(), monitor);
    }
}

#define MAX_KEY_COMBO 4
struct keyComboDef {
    guint32 keys[MAX_KEY_COMBO];
    const char *accel_label;
};

static const struct keyComboDef keyCombos[] = {
    { { GDK_KEY_Control_L, GDK_KEY_Alt_L, GDK_KEY_Delete, GDK_KEY_VoidSymbol }, "<Control><Alt>Delete" },
    { { GDK_KEY_Control_L, GDK_KEY_Alt_L, GDK_KEY_BackSpace, GDK_KEY_VoidSymbol }, "<Control><Alt>BackSpace" },
    { { GDK_KEY_VoidSymbol }, "" },
    { { GDK_KEY_Control_L, GDK_KEY_Alt_L, GDK_KEY_F1, GDK_KEY_VoidSymbol }, "<Control><Alt>F1" },
    { { GDK_KEY_Control_L, GDK_KEY_Alt_L, GDK_KEY_F2, GDK_KEY_VoidSymbol }, "<Control><Alt>F2" },
    { { GDK_KEY_Control_L, GDK_KEY_Alt_L, GDK_KEY_F3, GDK_KEY_VoidSymbol }, "<Control><Alt>F3" },
    { { GDK_KEY_Control_L, GDK_KEY_Alt_L, GDK_KEY_F4, GDK_KEY_VoidSymbol }, "<Control><Alt>F4" },
    { { GDK_KEY_Control_L, GDK_KEY_Alt_L, GDK_KEY_F5, GDK_KEY_VoidSymbol }, "<Control><Alt>F5" },
    { { GDK_KEY_Control_L, GDK_KEY_Alt_L, GDK_KEY_F6, GDK_KEY_VoidSymbol }, "<Control><Alt>F6" },
    { { GDK_KEY_Control_L, GDK_KEY_Alt_L, GDK_KEY_F7, GDK_KEY_VoidSymbol }, "<Control><Alt>F7" },
    { { GDK_KEY_Control_L, GDK_KEY_Alt_L, GDK_KEY_F8, GDK_KEY_VoidSymbol }, "<Control><Alt>F8" },
    { { GDK_KEY_Control_L, GDK_KEY_Alt_L, GDK_KEY_F9, GDK_KEY_VoidSymbol }, "<Control><Alt>F9" },
    { { GDK_KEY_Control_L, GDK_KEY_Alt_L, GDK_KEY_F10, GDK_KEY_VoidSymbol }, "<Control><Alt>F10" },
    { { GDK_KEY_Control_L, GDK_KEY_Alt_L, GDK_KEY_F11, GDK_KEY_VoidSymbol }, "<Control><Alt>F11" },
    { { GDK_KEY_Control_L, GDK_KEY_Alt_L, GDK_KEY_F12, GDK_KEY_VoidSymbol }, "<Control><Alt>F12" },
    { { GDK_KEY_VoidSymbol }, "" },
    { { GDK_KEY_Print, GDK_KEY_VoidSymbol }, "Print" },
};

static guint
get_nkeys(const guint32 *keys)
{
    guint i;

    for (i = 0; keys[i] != GDK_KEY_VoidSymbol; )
        i++;

    return i;
}

static void
virt_viewer_menu_add_combo(VirtViewerWindow *self G_GNUC_UNUSED, GMenu *menu,
                           const guint *keys, const gchar *label)
{
    GMenuItem *item = g_menu_item_new(label, NULL);

    g_menu_item_set_action_and_target_value(item,
                                            "win.send-key",
                                            g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
                                                                      keys,
                                                                      get_nkeys(keys),
                                                                      sizeof(guint32)));

    g_menu_append_item(menu, item);
}

static guint*
accel_key_to_keys(guint accel_key,
                  GdkModifierType accel_mods)
{
    guint i;
    guint *val, *keys;
    static const struct {
        const guint mask;
        const guint key;
    } modifiers[] = {
        {GDK_SHIFT_MASK, GDK_KEY_Shift_L},
        {GDK_CONTROL_MASK, GDK_KEY_Control_L},
        {GDK_MOD1_MASK, GDK_KEY_Alt_L},
    };

    g_warn_if_fail((accel_mods &
                    ~(GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK)) == 0);

    keys = val = g_new(guint, G_N_ELEMENTS(modifiers) + 2); /* up to 3 modifiers, key and the stop symbol */
    /* first, send the modifiers */
    for (i = 0; i < G_N_ELEMENTS(modifiers); i++) {
        if (accel_mods & modifiers[i].mask)
            *val++ = modifiers[i].key;
    }

    /* only after, the non-modifier key (ctrl-t, not t-ctrl) */
    *val++ = accel_key;
    /* stop symbol */
    *val = GDK_KEY_VoidSymbol;

    return keys;
}

static GMenu *
virt_viewer_window_get_keycombo_menu(VirtViewerWindow *self)
{
    gint i, j;
    GMenu *menu = g_menu_new();
    GMenu *sectionitems = g_menu_new();
    GMenuItem *section = g_menu_item_new_section(NULL, G_MENU_MODEL(sectionitems));

    g_menu_append_item(menu, section);

    for (i = 0 ; i < G_N_ELEMENTS(keyCombos); i++) {
        if (keyCombos[i].keys[0] == GDK_KEY_VoidSymbol) {
            sectionitems = g_menu_new();
            section = g_menu_item_new_section(NULL, G_MENU_MODEL(sectionitems));
            g_menu_append_item(menu, section);
        } else {
            gchar *label = NULL;
            guint key;
            GdkModifierType mods;
            gtk_accelerator_parse(keyCombos[i].accel_label, &key, &mods);
            label = gtk_accelerator_get_label(key, mods);

            virt_viewer_menu_add_combo(self, sectionitems, keyCombos[i].keys, label);
            g_free(label);
        }
    }

    gchar **accelactions = gtk_application_list_action_descriptions(GTK_APPLICATION(self->app));

    sectionitems = g_menu_new();
    section = g_menu_item_new_section(NULL, G_MENU_MODEL(sectionitems));
    g_menu_append_item(menu, section);

    for (i = 0; accelactions[i] != NULL; i++) {
        if (g_str_equal(accelactions[i], "win.release-cursor")) {
            gchar *display_hotkey = virt_viewer_app_get_release_cursor_display_hotkey(self->app);
            if (display_hotkey) {
                gchar *accel = spice_hotkey_to_gtk_accelerator(display_hotkey);
                guint accel_key;
                GdkModifierType accel_mods;
                gtk_accelerator_parse(accel, &accel_key, &accel_mods);

                guint *keys = accel_key_to_keys(accel_key, accel_mods);
                gchar *label = spice_hotkey_to_display_hotkey(display_hotkey);
                virt_viewer_menu_add_combo(self, sectionitems, keys, label);
                g_free(label);
                g_free(keys);
            }
        }

        gchar **accels = gtk_application_get_accels_for_action(GTK_APPLICATION(self->app),
                                                               accelactions[i]);

        for (j = 0; accels[j] != NULL; j++) {
            guint accel_key;
            GdkModifierType accel_mods;
            gtk_accelerator_parse(accels[j], &accel_key, &accel_mods);

            guint *keys = accel_key_to_keys(accel_key, accel_mods);
            gchar *label = gtk_accelerator_get_label(accel_key, accel_mods);
            virt_viewer_menu_add_combo(self, sectionitems, keys, label);
            g_free(label);
            g_free(keys);
        }
        g_strfreev(accels);
    }

    g_strfreev(accelactions);

    return menu;
}

void
virt_viewer_window_disable_modifiers(VirtViewerWindow *self)
{
    GtkSettings *settings = gtk_settings_get_default();
    GValue empty;
    GSList *accels;

    if (!self->accel_enabled)
        return;

    /* This stops F10 activating menu bar */
    memset(&empty, 0, sizeof empty);
    g_value_init(&empty, G_TYPE_STRING);
    g_object_get_property(G_OBJECT(settings), "gtk-menu-bar-accel", &self->accel_setting);
    g_object_set_property(G_OBJECT(settings), "gtk-menu-bar-accel", &empty);

    /* This stops global accelerators like Ctrl+Q == Quit */
    for (accels = self->accel_list ; accels ; accels = accels->next) {
        if (self->accel_group == accels->data && !self->kiosk)
            continue;
        gtk_window_remove_accel_group(GTK_WINDOW(self->window), accels->data);
    }

    /* This stops menu bar shortcuts like Alt+F == File */
    g_object_get(settings,
                 "gtk-enable-mnemonics", &self->enable_mnemonics_save,
                 NULL);
    g_object_set(settings,
                 "gtk-enable-mnemonics", FALSE,
                 NULL);

    self->accel_enabled = FALSE;
}

void
virt_viewer_window_enable_modifiers(VirtViewerWindow *self)
{
    GtkSettings *settings = gtk_settings_get_default();
    GSList *accels;
    GSList *attached_accels;

    if (self->accel_enabled)
        return;

    /* This allows F10 activating menu bar */
    g_object_set_property(G_OBJECT(settings), "gtk-menu-bar-accel", &self->accel_setting);
    attached_accels = gtk_accel_groups_from_object(G_OBJECT(self->window));

    /* This allows global accelerators like Ctrl+Q == Quit */
    for (accels = self->accel_list ; accels ; accels = accels->next) {
        /* Do not attach accels that are already attached. */
        if (attached_accels && g_slist_find(attached_accels, accels->data))
            continue;

        gtk_window_add_accel_group(GTK_WINDOW(self->window), accels->data);
    }

    /* This allows menu bar shortcuts like Alt+F == File */
    g_object_set(settings,
                 "gtk-enable-mnemonics", self->enable_mnemonics_save,
                 NULL);

    self->accel_enabled = TRUE;
}


G_MODULE_EXPORT gboolean
virt_viewer_window_delete(GtkWidget *src G_GNUC_UNUSED,
                          void *dummy G_GNUC_UNUSED,
                          VirtViewerWindow *self)
{
    g_debug("Window closed");
    virt_viewer_app_maybe_quit(self->app, self);
    return TRUE;
}


static void
virt_viewer_window_set_fullscreen(VirtViewerWindow *self,
                                  gboolean fullscreen)
{
    if (fullscreen) {
        virt_viewer_window_enter_fullscreen(self, -1);
    } else {
        /* leave all windows fullscreen state */
        if (virt_viewer_app_get_fullscreen(self->app))
            g_object_set(self->app, "fullscreen", FALSE, NULL);
        /* or just this window */
        else
            virt_viewer_window_leave_fullscreen(self);
    }
}


static void add_if_writable (GdkPixbufFormat *data, GHashTable *formats)
{
    if (gdk_pixbuf_format_is_writable(data)) {
        gchar **extensions;
        gchar **it;
        extensions = gdk_pixbuf_format_get_extensions(data);
        for (it = extensions; *it != NULL; it++) {
            g_hash_table_insert(formats, g_strdup(*it), data);
        }
        g_strfreev(extensions);
    }
}

static GHashTable *init_image_formats(G_GNUC_UNUSED gpointer user_data)
{
    GHashTable *format_map;
    GSList *formats = gdk_pixbuf_get_formats();

    format_map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    g_slist_foreach(formats, (GFunc)add_if_writable, format_map);
    g_slist_free (formats);

    return format_map;
}

static GdkPixbufFormat *get_image_format(const char *filename)
{
    static GOnce image_formats_once = G_ONCE_INIT;
    const char *ext;

    g_once(&image_formats_once, (GThreadFunc)init_image_formats, NULL);

    ext = strrchr(filename, '.');
    if (ext == NULL)
        return NULL;

    ext++; /* skip '.' */

    return g_hash_table_lookup(image_formats_once.retval, ext);
}

static gboolean
virt_viewer_window_save_screenshot(VirtViewerWindow *self,
                                   const char *file,
                                   GError **error)
{
    GdkPixbuf *pix = virt_viewer_display_get_pixbuf(VIRT_VIEWER_DISPLAY(self->display));
    GdkPixbufFormat *format = get_image_format(file);
    gboolean result;

    if (format == NULL) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                    _("Unable to determine image format for file '%s'"), file);
        result = FALSE;
    } else {
        char *type = gdk_pixbuf_format_get_name(format);
        g_debug("saving to %s", type);
        result = gdk_pixbuf_save(pix, file, type, error, NULL);
        g_free(type);
    }

    g_object_unref(pix);
    return result;
}

void
virt_viewer_window_screenshot(VirtViewerWindow *self)
{
    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(self));

    GtkWidget *dialog;
    const char *image_dir;

    g_return_if_fail(self->display != NULL);

    dialog = gtk_file_chooser_dialog_new(_("Save screenshot"),
                                         NULL,
                                         GTK_FILE_CHOOSER_ACTION_SAVE,
                                         _("_Cancel"), GTK_RESPONSE_CANCEL,
                                         _("_Save"), GTK_RESPONSE_ACCEPT,
                                         NULL);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER (dialog), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dialog),
                                 GTK_WINDOW(self->window));
    image_dir = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
    if (image_dir != NULL)
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER (dialog), image_dir);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER (dialog), _("Screenshot.png"));

retry_dialog:
    if (gtk_dialog_run(GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename;
        GError *error = NULL;

        filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER (dialog));
        if (g_strrstr(filename, ".") == NULL) {
            // no extension provided
            GtkWidget *msg_dialog ;
            g_free(filename);
            msg_dialog = gtk_message_dialog_new (GTK_WINDOW(dialog), GTK_DIALOG_MODAL,
                                                 GTK_MESSAGE_WARNING, GTK_BUTTONS_CLOSE,
                                                 _("Please add an extension to the file name"));
            gtk_dialog_run(GTK_DIALOG(msg_dialog));
            gtk_widget_destroy(msg_dialog);
            goto retry_dialog;
        }

        if (!virt_viewer_window_save_screenshot(self, filename, &error)) {
            virt_viewer_app_simple_message_dialog(self->app,
                                                  "%s", error->message);
            g_error_free(error);
        }
        g_free(filename);
    }

    gtk_widget_destroy(dialog);
}


void
virt_viewer_window_show_guest_details(VirtViewerWindow *self)
{
    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(self));

    GtkBuilder *ui = virt_viewer_util_load_ui("virt-viewer-guest-details.ui");
    char *name = NULL;
    char *uuid = NULL;

    g_return_if_fail(ui != NULL);

    GtkWidget *dialog = GTK_WIDGET(gtk_builder_get_object(ui, "guestdetailsdialog"));
    GtkWidget *namelabel = GTK_WIDGET(gtk_builder_get_object(ui, "namevaluelabel"));
    GtkWidget *guidlabel = GTK_WIDGET(gtk_builder_get_object(ui, "guidvaluelabel"));

    g_return_if_fail(dialog && namelabel && guidlabel);

    g_object_get(self->app, "guest-name", &name, "uuid", &uuid, NULL);

    if (!name || *name == '\0')
        name = g_strdup(C_("Unknown name", "Unknown"));
    if (!uuid || *uuid == '\0')
        uuid = g_strdup(C_("Unknown UUID", "Unknown"));
    gtk_label_set_text(GTK_LABEL(namelabel), name);
    gtk_label_set_text(GTK_LABEL(guidlabel), uuid);
    g_free(name);
    g_free(uuid);

    gtk_window_set_transient_for(GTK_WINDOW(dialog),
                                 GTK_WINDOW(self->window));

    gtk_builder_connect_signals(ui, self);

    gtk_widget_show_all(dialog);

    g_object_unref(G_OBJECT(ui));
}

G_MODULE_EXPORT void
virt_viewer_window_guest_details_response(GtkDialog *dialog,
                                          gint response_id,
                                          gpointer user_data G_GNUC_UNUSED)
{
    if (response_id == GTK_RESPONSE_CLOSE)
        gtk_widget_hide(GTK_WIDGET(dialog));
}

void
virt_viewer_window_show_about(VirtViewerWindow *self)
{
    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(self));

    GtkBuilder *about;
    GtkWidget *dialog;
    GdkPixbuf *icon;

    about = virt_viewer_util_load_ui("virt-viewer-about.ui");

    dialog = GTK_WIDGET(gtk_builder_get_object(about, "about"));

    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dialog), VERSION BUILDID);

    icon = gdk_pixbuf_new_from_resource(VIRT_VIEWER_RESOURCE_PREFIX"/icons/48x48/virt-viewer.png", NULL);
    if (icon != NULL) {
        gtk_about_dialog_set_logo(GTK_ABOUT_DIALOG(dialog), icon);
        g_object_unref(icon);
    } else {
        gtk_about_dialog_set_logo_icon_name(GTK_ABOUT_DIALOG(dialog), "virt-viewer");
    }

    gtk_window_set_transient_for(GTK_WINDOW(dialog),
                                 GTK_WINDOW(self->window));

    gtk_builder_connect_signals(about, self);

    gtk_widget_show_all(dialog);

    g_object_unref(G_OBJECT(about));
}


#if HAVE_OVIRT
static void
iso_dialog_response(GtkDialog *dialog,
                    gint response_id,
                    gpointer user_data G_GNUC_UNUSED)
{
    if (response_id == GTK_RESPONSE_NONE)
        return;

    gtk_widget_destroy(GTK_WIDGET(dialog));
}
#endif

void
virt_viewer_window_change_cd(VirtViewerWindow *self G_GNUC_UNUSED)
{
#if HAVE_OVIRT
    GtkWidget *dialog;
    GObject *foreign_menu;

    g_object_get(G_OBJECT(self->app), "ovirt-foreign-menu", &foreign_menu, NULL);
    dialog = remote_viewer_iso_list_dialog_new(GTK_WINDOW(self->window), foreign_menu);
    g_object_unref(foreign_menu);

    if (!dialog)
        dialog = gtk_message_dialog_new(GTK_WINDOW(self->window),
                                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                        GTK_MESSAGE_ERROR,
                                        GTK_BUTTONS_CLOSE,
                                        _("Unable to connect to oVirt"));

    g_signal_connect(dialog, "response", G_CALLBACK(iso_dialog_response), NULL);
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
#endif
}


VirtViewerNotebook*
virt_viewer_window_get_notebook (VirtViewerWindow *self)
{
    return VIRT_VIEWER_NOTEBOOK(self->notebook);
}

GtkWindow*
virt_viewer_window_get_window (VirtViewerWindow *self)
{
    return GTK_WINDOW(self->window);
}

static void
virt_viewer_window_pointer_grab(VirtViewerDisplay *display G_GNUC_UNUSED,
                                VirtViewerWindow *self)
{
    self->grabbed = TRUE;
    virt_viewer_window_update_title(self);
}

static void
virt_viewer_window_pointer_ungrab(VirtViewerDisplay *display G_GNUC_UNUSED,
                                  VirtViewerWindow *self)
{
    self->grabbed = FALSE;
    virt_viewer_window_update_title(self);
}

static void
virt_viewer_window_keyboard_grab(VirtViewerDisplay *display G_GNUC_UNUSED,
                                 VirtViewerWindow *self)
{
    virt_viewer_window_disable_modifiers(self);
}

static void
virt_viewer_window_keyboard_ungrab(VirtViewerDisplay *display G_GNUC_UNUSED,
                                   VirtViewerWindow *self)
{
    virt_viewer_window_enable_modifiers(self);
}

void
virt_viewer_window_update_title(VirtViewerWindow *self)
{
    char *title;
    char *grabhint = NULL;
    GtkWidget *header;
    GtkWidget *toolbar;

    header = GTK_WIDGET(gtk_builder_get_object(self->builder, "header"));
    toolbar = GTK_WIDGET(gtk_builder_get_object(self->builder, "toolbar"));

    if (self->grabbed) {
        gchar *label;
        gchar *display_hotkey;
        guint accel_key = 0;
        GdkModifierType accel_mods = 0;
        gchar **accels;

        display_hotkey = virt_viewer_app_get_release_cursor_display_hotkey(self->app);
        if (display_hotkey) {
            label = spice_hotkey_to_display_hotkey(display_hotkey);
        } else {
            accels = gtk_application_get_accels_for_action(GTK_APPLICATION(self->app), "win.release-cursor");
            if (accels[0])
                gtk_accelerator_parse(accels[0], &accel_key, &accel_mods);
            g_strfreev(accels);
            g_debug("release-cursor accel key: key=%u, mods=%x", accel_key, accel_mods);
            label = gtk_accelerator_get_label(accel_key, accel_mods);
        }

        grabhint = g_strdup_printf(_("(Press %s to release pointer)"), label);
        g_free(label);

        if (self->subtitle) {
            /* translators:
             * This is "<ungrab accelerator> <subtitle> - <appname>"
             * Such as: "(Press Ctrl+Alt to release pointer) BigCorpTycoon MOTD - Virt Viewer"
             */
            title = g_strdup_printf(_("%s %s - %s"),
                                    grabhint,
                                    self->subtitle,
                                    g_get_application_name());
        } else {
            /* translators:
             * This is "<ungrab accelerator> - <appname>"
             * Such as: "(Press Ctrl+Alt to release pointer) - Virt Viewer"
             */
            title = g_strdup_printf(_("%s - %s"),
                                    grabhint,
                                    g_get_application_name());
        }
    } else if (self->subtitle) {
        /* translators:
         * This is "<subtitle> - <appname>"
         * Such as: "BigCorpTycoon MOTD - Virt Viewer"
         */
        title = g_strdup_printf(_("%s - %s"),
                                self->subtitle,
                                g_get_application_name());
    } else {
        title = g_strdup(g_get_application_name());
    }

    gtk_window_set_title(GTK_WINDOW(self->window), title);
    if (self->subtitle) {
        gtk_header_bar_set_title(GTK_HEADER_BAR(header), self->subtitle);
        gtk_header_bar_set_title(GTK_HEADER_BAR(toolbar), self->subtitle);
    } else {
        gtk_header_bar_set_title(GTK_HEADER_BAR(header), g_get_application_name());
        gtk_header_bar_set_title(GTK_HEADER_BAR(toolbar), g_get_application_name());
    }
    if (grabhint) {
        gtk_header_bar_set_subtitle(GTK_HEADER_BAR(header), grabhint);
        gtk_header_bar_set_subtitle(GTK_HEADER_BAR(toolbar), grabhint);
    } else {
        gtk_header_bar_set_subtitle(GTK_HEADER_BAR(header), "");
        gtk_header_bar_set_subtitle(GTK_HEADER_BAR(toolbar), "");
    }

    g_free(title);
    g_free(grabhint);
}

void
virt_viewer_window_set_usb_options_sensitive(VirtViewerWindow *self, gboolean sensitive)
{
    GAction *action;
    GActionMap *map;

    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(self));

    map = G_ACTION_MAP(self->window);
    action = g_action_map_lookup_action(map, "usb-device-select");
    g_simple_action_set_enabled(G_SIMPLE_ACTION(action), sensitive);
}

void
virt_viewer_window_set_usb_reset_sensitive(VirtViewerWindow *self, gboolean sensitive)
{
    GAction *action;
    GActionMap *map;

    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(self));

    map = G_ACTION_MAP(self->window);
    action = g_action_map_lookup_action(map, "usb-device-reset");
    g_simple_action_set_enabled(G_SIMPLE_ACTION(action), sensitive);
}

void
virt_viewer_window_set_actions_sensitive(VirtViewerWindow *self, gboolean sensitive)
{
    GAction *action;
    GActionMap *map;

    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(self));

    map = G_ACTION_MAP(self->window);
    action = g_action_map_lookup_action(map, "preferences");
    g_simple_action_set_enabled(G_SIMPLE_ACTION(action), sensitive);

    action = g_action_map_lookup_action(map, "screenshot");
    g_simple_action_set_enabled(G_SIMPLE_ACTION(action),
                                sensitive &&
                                VIRT_VIEWER_DISPLAY_CAN_SCREENSHOT(self->display));

    action = g_action_map_lookup_action(map, "zoom-in");
    g_simple_action_set_enabled(G_SIMPLE_ACTION(action), sensitive);

    action = g_action_map_lookup_action(map, "zoom-out");
    g_simple_action_set_enabled(G_SIMPLE_ACTION(action), sensitive);

    action = g_action_map_lookup_action(map, "zoom-reset");
    g_simple_action_set_enabled(G_SIMPLE_ACTION(action), sensitive);

    action = g_action_map_lookup_action(map, "send-key");
    g_simple_action_set_enabled(G_SIMPLE_ACTION(action),
                                sensitive &&
                                VIRT_VIEWER_DISPLAY_CAN_SEND_KEYS(self->display));
}

static void
display_show_hint(VirtViewerDisplay *display,
                  GParamSpec *pspec G_GNUC_UNUSED,
                  VirtViewerWindow *self)
{
    GAction *action;
    GActionMap *map;
    guint hint;

    g_object_get(display, "show-hint", &hint, NULL);

    hint = (hint & VIRT_VIEWER_DISPLAY_SHOW_HINT_READY);

    if (!self->initial_zoom_set && hint && virt_viewer_display_get_enabled(display)) {
        self->initial_zoom_set = TRUE;
        virt_viewer_window_set_zoom_level(self, self->zoomlevel);
    }

    map = G_ACTION_MAP(self->window);
    action = g_action_map_lookup_action(map, "screenshot");
    g_simple_action_set_enabled(G_SIMPLE_ACTION(action),
                                hint);
}


static gboolean
window_key_pressed (GtkWidget *widget G_GNUC_UNUSED,
                    GdkEvent  *ev,
                   VirtViewerWindow *self)
{
    GdkEventKey  *event;
    VirtViewerDisplay *display;
    display = self->display;
    event = (GdkEventKey *)ev;

    gtk_widget_grab_focus(GTK_WIDGET(display));

    // Look through keymaps - if set for mappings and intercept
    if (self->keyMappings) {
        VirtViewerKeyMapping *ptr, *matched;
        ptr = self->keyMappings;
        matched = NULL;
        do {
                if (event->keyval == ptr->sourceKey) {
                        matched = ptr;
                }
                if (ptr->isLast) {
                        break;
                }
                ptr++;
        } while (matched == NULL);

        if (matched) {
                if (matched->mappedKeys == NULL) {
                        // Key to be ignored and not pass through to VM
                        g_debug("Blocking keypress '%s'", gdk_keyval_name(matched->sourceKey));
                } else {
                        g_debug("Sending through mapped keys");
                        virt_viewer_display_send_keys(display,
                                matched->mappedKeys, matched->numMappedKeys);
                }
                return TRUE;
        }

    }
    g_debug("Key pressed was keycode='0x%x', gdk_keyname='%s'", event->keyval, gdk_keyval_name(event->keyval));
    return gtk_widget_event(GTK_WIDGET(display), ev);
}


void
virt_viewer_window_set_display(VirtViewerWindow *self, VirtViewerDisplay *display)
{
    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(self));
    g_return_if_fail(display == NULL || VIRT_VIEWER_IS_DISPLAY(display));

    if (self->display) {
        gtk_notebook_remove_page(GTK_NOTEBOOK(self->notebook), 1);
        g_object_unref(self->display);
        self->display = NULL;
    }

    if (display != NULL) {
        self->display = g_object_ref(display);

        virt_viewer_display_set_monitor(VIRT_VIEWER_DISPLAY(self->display), self->fullscreen_monitor);
        virt_viewer_display_set_fullscreen(VIRT_VIEWER_DISPLAY(self->display), self->fullscreen);

        gtk_widget_show_all(GTK_WIDGET(display));
        gtk_notebook_append_page(GTK_NOTEBOOK(self->notebook), GTK_WIDGET(display), NULL);
        gtk_widget_realize(GTK_WIDGET(display));

        virt_viewer_signal_connect_object(self->window, "key-press-event",
                                          G_CALLBACK(window_key_pressed), self, 0);

        /* switch back to non-display if not ready */
        if (!(virt_viewer_display_get_show_hint(display) &
              VIRT_VIEWER_DISPLAY_SHOW_HINT_READY))
            gtk_notebook_set_current_page(GTK_NOTEBOOK(self->notebook), 0);

        virt_viewer_signal_connect_object(display, "display-pointer-grab",
                                          G_CALLBACK(virt_viewer_window_pointer_grab), self, 0);
        virt_viewer_signal_connect_object(display, "display-pointer-ungrab",
                                          G_CALLBACK(virt_viewer_window_pointer_ungrab), self, 0);
        virt_viewer_signal_connect_object(display, "display-keyboard-grab",
                                          G_CALLBACK(virt_viewer_window_keyboard_grab), self, 0);
        virt_viewer_signal_connect_object(display, "display-keyboard-ungrab",
                                          G_CALLBACK(virt_viewer_window_keyboard_ungrab), self, 0);
        virt_viewer_signal_connect_object(display, "display-desktop-resize",
                                          G_CALLBACK(virt_viewer_window_desktop_resize), self, 0);
        virt_viewer_signal_connect_object(display, "notify::show-hint",
                                          G_CALLBACK(display_show_hint), self, 0);

        display_show_hint(display, NULL, self);

        if (virt_viewer_display_get_enabled(display))
            virt_viewer_window_desktop_resize(display, self);
    }
}

static void
virt_viewer_window_enable_kiosk(VirtViewerWindow *self)
{
    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(self));

    virt_viewer_timed_revealer_force_reveal(self->revealer, FALSE);

    /* You probably also want X11 Option "DontVTSwitch" "true" */
    /* and perhaps more distro/desktop-specific options */
    virt_viewer_window_disable_modifiers(self);
}

void
virt_viewer_window_show(VirtViewerWindow *self)
{
    if (self->display && !virt_viewer_display_get_enabled(self->display))
        virt_viewer_display_enable(self->display);

    if (self->desktop_resize_pending) {
        virt_viewer_window_queue_resize(self);
        self->desktop_resize_pending = FALSE;
    }

    gtk_widget_show(self->window);

    if (self->fullscreen)
        virt_viewer_window_move_to_monitor(self);
}

void
virt_viewer_window_hide(VirtViewerWindow *self)
{
    if (self->kiosk) {
        g_warning("Can't hide windows in kiosk mode");
        return;
    }

    gtk_widget_hide(self->window);

    if (self->display) {
        VirtViewerDisplay *display = self->display;
        virt_viewer_display_disable(display);
    }
}

void
virt_viewer_window_set_zoom_level(VirtViewerWindow *self, gint zoom_level)
{
    gint min_zoom;

    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(self));

    if (zoom_level < MIN_ZOOM_LEVEL)
        zoom_level = MIN_ZOOM_LEVEL;
    if (zoom_level > MAX_ZOOM_LEVEL)
        zoom_level = MAX_ZOOM_LEVEL;
    self->zoomlevel = zoom_level;

    if (!self->display)
        return;

    min_zoom = virt_viewer_window_get_minimal_zoom_level(self);
    if (min_zoom > self->zoomlevel) {
        g_debug("Cannot set zoom level %d, using %d", self->zoomlevel, min_zoom);
        self->zoomlevel = min_zoom;
    }

    if (self->zoomlevel == virt_viewer_display_get_zoom_level(self->display) &&
        self->zoomlevel == virt_viewer_window_get_real_zoom_level(self)) {
        g_debug("Zoom level not changed, using: %d", self->zoomlevel);
        return;
    }

    virt_viewer_display_set_zoom_level(VIRT_VIEWER_DISPLAY(self->display), self->zoomlevel);

    if (!VIRT_VIEWER_IS_DISPLAY_VTE(self->display)) {
        virt_viewer_window_queue_resize(self);
    }
}

gint virt_viewer_window_get_zoom_level(VirtViewerWindow *self)
{
    g_return_val_if_fail(VIRT_VIEWER_IS_WINDOW(self), NORMAL_ZOOM_LEVEL);
    return self->zoomlevel;
}

GMenuModel *
virt_viewer_window_get_menu(VirtViewerWindow *self, const gchar * menu_name)
{
    GObject *menu;
    GMenuModel *model;
    g_return_val_if_fail(VIRT_VIEWER_IS_WINDOW(self), NULL);

    menu = gtk_builder_get_object(self->builder, menu_name);
    model = gtk_menu_button_get_menu_model(GTK_MENU_BUTTON(menu));

    return g_menu_model_get_item_link(model, 0, G_MENU_LINK_SECTION);
}

GtkBuilder*
virt_viewer_window_get_builder(VirtViewerWindow *self)
{
    g_return_val_if_fail(VIRT_VIEWER_IS_WINDOW(self), NULL);

    return self->builder;
}

VirtViewerDisplay*
virt_viewer_window_get_display(VirtViewerWindow *self)
{
    g_return_val_if_fail(VIRT_VIEWER_WINDOW(self), NULL);

    return self->display;
}

void
virt_viewer_window_set_kiosk(VirtViewerWindow *self, gboolean enabled)
{
    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(self));
    g_return_if_fail(enabled == !!enabled);

    if (self->kiosk == enabled)
        return;

    self->kiosk = enabled;

    if (enabled)
        virt_viewer_window_enable_kiosk(self);
    else
        g_debug("disabling kiosk not implemented yet");
}

static void
virt_viewer_window_get_minimal_dimensions(VirtViewerWindow *self G_GNUC_UNUSED,
                                          guint *width,
                                          guint *height)
{
    *height = MIN_DISPLAY_HEIGHT;
    *width = MIN_DISPLAY_WIDTH;
}

/**
 * virt_viewer_window_get_minimal_zoom_level:
 * @self: a #VirtViewerWindow
 *
 * Calculates the zoom level with respect to the desktop dimensions
 *
 * Returns: minimal possible zoom level (multiple of ZOOM_STEP)
 */
static gint
virt_viewer_window_get_minimal_zoom_level(VirtViewerWindow *self)
{
    guint min_width, min_height;
    guint width, height; /* desktop dimensions */
    gint zoom;
    double width_ratio, height_ratio;

    g_return_val_if_fail(VIRT_VIEWER_IS_WINDOW(self) &&
                         self->display != NULL, MIN_ZOOM_LEVEL);

    virt_viewer_window_get_minimal_dimensions(self, &min_width, &min_height);
    virt_viewer_display_get_desktop_size(virt_viewer_window_get_display(self), &width, &height);

    /* e.g. minimal width = 200, desktop width = 550 => width ratio = 0.36
     * which means that the minimal zoom level is 40 (4 * ZOOM_STEP)
     */
    width_ratio = (double) min_width / width;
    height_ratio = (double) min_height / height;
    zoom = ceil(10 * MAX(width_ratio, height_ratio));

    /* make sure that the returned zoom level is in the range from MIN_ZOOM_LEVEL to NORMAL_ZOOM_LEVEL */
    return CLAMP(zoom * ZOOM_STEP, MIN_ZOOM_LEVEL, NORMAL_ZOOM_LEVEL);
}
