/*
 * Virt Viewer: A virtual machine console viewer
 *
 * Copyright (C) 2007-2012 Red Hat, Inc.
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

#pragma once

#include <glib-object.h>
#include <gtk/gtk.h>
#include "virt-viewer-window.h"

#define VIRT_VIEWER_TYPE_APP virt_viewer_app_get_type()
G_DECLARE_DERIVABLE_TYPE(VirtViewerApp,
                         virt_viewer_app,
                         VIRT_VIEWER,
                         APP,
                         GtkApplication)

typedef struct {
    guint sourceKey;
    guint numMappedKeys;
    guint *mappedKeys;
    gboolean isLast;
} VirtViewerKeyMapping;

typedef enum {
    VIRT_VIEWER_CURSOR_AUTO,
    VIRT_VIEWER_CURSOR_LOCAL,
} VirtViewerCursor;

struct _VirtViewerAppClass {
    GtkApplicationClass parent_class;

    /*< private >*/
    gboolean (*start) (VirtViewerApp *self, GError **error);
    gboolean (*initial_connect) (VirtViewerApp *self, GError **error);
    gboolean (*activate) (VirtViewerApp *self, GError **error);
    void (*deactivated) (VirtViewerApp *self, gboolean connect_error);
    gboolean (*open_connection)(VirtViewerApp *self, int *fd);
    void (*add_option_entries)(VirtViewerApp *self, GOptionContext *context, GOptionGroup *group);
};

GType virt_viewer_app_get_type (void);

void virt_viewer_app_set_debug(gboolean debug);
gboolean virt_viewer_app_start(VirtViewerApp *app, GError **error);
void virt_viewer_app_maybe_quit(VirtViewerApp *self, VirtViewerWindow *window);
VirtViewerWindow* virt_viewer_app_get_main_window(VirtViewerApp *self);
void virt_viewer_app_trace(VirtViewerApp *self, const char *fmt, ...) G_GNUC_PRINTF(2, 3);
void virt_viewer_app_simple_message_dialog(VirtViewerApp *self, const char *fmt, ...) G_GNUC_PRINTF(2, 3);
gboolean virt_viewer_app_is_active(VirtViewerApp *app);
void virt_viewer_app_free_connect_info(VirtViewerApp *self);
gboolean virt_viewer_app_create_session(VirtViewerApp *self, const gchar *type, GError **error);
gboolean virt_viewer_app_activate(VirtViewerApp *self, GError **error);
gboolean virt_viewer_app_initial_connect(VirtViewerApp *self, GError **error);
gboolean virt_viewer_app_get_direct(VirtViewerApp *self);
void virt_viewer_app_set_direct(VirtViewerApp *self, gboolean direct);
char** virt_viewer_app_get_hotkey_names(void);
gchar* virt_viewer_app_get_release_cursor_display_hotkey(VirtViewerApp *self);
void virt_viewer_app_set_release_cursor_display_hotkey(VirtViewerApp *self, const gchar *hotkey);
void virt_viewer_app_set_hotkey(VirtViewerApp *self, const gchar *hotkey_name, const gchar *hotkey);
void virt_viewer_app_set_hotkeys(VirtViewerApp *self, const gchar *hotkeys);
void virt_viewer_app_set_attach(VirtViewerApp *self, gboolean attach);
gboolean virt_viewer_app_get_attach(VirtViewerApp *self);
void virt_viewer_app_set_shared(VirtViewerApp *self, gboolean shared);
gboolean virt_viewer_app_get_shared(VirtViewerApp *self);
void virt_viewer_app_set_cursor(VirtViewerApp *self, VirtViewerCursor cursor);
VirtViewerCursor virt_viewer_app_get_cursor(VirtViewerApp *self);
gboolean virt_viewer_app_has_session(VirtViewerApp *self);
void virt_viewer_app_set_connect_info(VirtViewerApp *self,
                                      const gchar *host,
                                      const gchar *ghost,
                                      const gchar *gport,
                                      const gchar *gtlsport,
                                      const gchar *transport,
                                      const gchar *unixsock,
                                      const gchar *user,
                                      gint port,
                                      const gchar *guri);

void virt_viewer_app_show_status(VirtViewerApp *self, const gchar *fmt, ...) G_GNUC_PRINTF(2, 3);
void virt_viewer_app_show_display(VirtViewerApp *self);
GList* virt_viewer_app_get_windows(VirtViewerApp *self);
VirtViewerSession* virt_viewer_app_get_session(VirtViewerApp *self);
gboolean virt_viewer_app_get_fullscreen(VirtViewerApp *app);
void virt_viewer_app_clear_hotkeys(VirtViewerApp *app);
GList* virt_viewer_app_get_initial_displays(VirtViewerApp* self);
gint virt_viewer_app_get_initial_monitor_for_display(VirtViewerApp* self, gint display);
void virt_viewer_app_show_preferences(VirtViewerApp *app, GtkWidget *parent);
gboolean virt_viewer_app_get_session_cancelled(VirtViewerApp *self);

gboolean virt_viewer_app_get_config_share_clipboard(VirtViewerApp *self);
void virt_viewer_app_set_config_share_clipboard(VirtViewerApp *self, gboolean enable);

gboolean virt_viewer_app_get_supports_share_clipboard(VirtViewerApp *self);
void virt_viewer_app_set_supports_share_clipboard(VirtViewerApp *self, gboolean enable);
void window_menu_set_current_codec( VirtViewerApp *self, GVariant *value);
