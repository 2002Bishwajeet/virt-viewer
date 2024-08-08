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

#pragma once

#include <glib-object.h>
#include "virt-viewer-notebook.h"
#include "virt-viewer-display.h"

#define MIN_ZOOM_LEVEL 10
#define MAX_ZOOM_LEVEL 400
#define NORMAL_ZOOM_LEVEL 100

#define VIRT_VIEWER_TYPE_WINDOW virt_viewer_window_get_type()

G_DECLARE_FINAL_TYPE(VirtViewerWindow,
                     virt_viewer_window,
                     VIRT_VIEWER,
                     WINDOW,
                     GObject)

GType virt_viewer_window_get_type (void);

GtkWindow* virt_viewer_window_get_window (VirtViewerWindow* window);
VirtViewerNotebook* virt_viewer_window_get_notebook (VirtViewerWindow* window);
void virt_viewer_window_set_display(VirtViewerWindow *self, VirtViewerDisplay *display);
VirtViewerDisplay* virt_viewer_window_get_display(VirtViewerWindow *self);
void virt_viewer_window_set_usb_options_sensitive(VirtViewerWindow *self, gboolean sensitive);
void virt_viewer_window_set_usb_reset_sensitive(VirtViewerWindow *self, gboolean sensitive);
void virt_viewer_window_set_actions_sensitive(VirtViewerWindow *self, gboolean sensitive);
void virt_viewer_window_update_title(VirtViewerWindow *self);
void virt_viewer_window_show(VirtViewerWindow *self);
void virt_viewer_window_hide(VirtViewerWindow *self);
void virt_viewer_window_set_zoom_level(VirtViewerWindow *self, gint zoom_level);
void virt_viewer_window_zoom_out(VirtViewerWindow *self);
void virt_viewer_window_zoom_in(VirtViewerWindow *self);
void virt_viewer_window_zoom_reset(VirtViewerWindow *self);
gint virt_viewer_window_get_zoom_level(VirtViewerWindow *self);
void virt_viewer_window_leave_fullscreen(VirtViewerWindow *self);
void virt_viewer_window_enter_fullscreen(VirtViewerWindow *self, gint monitor);
GMenuModel *virt_viewer_window_get_menu(VirtViewerWindow *self, const gchar *menu_name);
GtkBuilder* virt_viewer_window_get_builder(VirtViewerWindow *window);
void virt_viewer_window_set_kiosk(VirtViewerWindow *self, gboolean enabled);
void virt_viewer_window_show_about(VirtViewerWindow *self);
void virt_viewer_window_show_guest_details(VirtViewerWindow *self);
void virt_viewer_window_screenshot(VirtViewerWindow *self);
void virt_viewer_window_change_cd(VirtViewerWindow *self);
