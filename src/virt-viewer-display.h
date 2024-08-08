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

#include <gtk/gtk.h>
#include "virt-viewer-enums.h"

#define MIN_DISPLAY_WIDTH 320
#define MIN_DISPLAY_HEIGHT 200

#define VIRT_VIEWER_TYPE_DISPLAY virt_viewer_display_get_type()
G_DECLARE_DERIVABLE_TYPE(VirtViewerDisplay,
                         virt_viewer_display,
                         VIRT_VIEWER,
                         DISPLAY,
                         GtkBin)

typedef struct _VirtViewerSession       VirtViewerSession;
typedef struct _VirtViewerSessionClass  VirtViewerSessionClass;

typedef struct _VirtViewerDisplayChannel VirtViewerDisplayChannel;
typedef struct _VirtViewerVideoCodec VirtViewerVideoCodec;

typedef enum {
    VIRT_VIEWER_DISPLAY_SHOW_HINT_READY            = 1 << 0,
    VIRT_VIEWER_DISPLAY_SHOW_HINT_DISABLED         = 1 << 1,
    VIRT_VIEWER_DISPLAY_SHOW_HINT_SET              = 1 << 2,
} VirtViewerDisplayShowHintFlags;

struct _VirtViewerDisplayClass {
    GtkBinClass parent_class;

    /* virtual methods */
    void (*send_keys)(VirtViewerDisplay *display,
                      const guint *keyvals, int nkeyvals);
    GdkPixbuf *(*get_pixbuf)(VirtViewerDisplay *display);
    void (*release_cursor)(VirtViewerDisplay *display);

    void (*close)(VirtViewerDisplay *display);
    gboolean (*selectable)(VirtViewerDisplay *display);
    void (*enable)(VirtViewerDisplay *display);
    void (*disable)(VirtViewerDisplay *display);
    GArray * (*codecs)(VirtViewerDisplay *display);
    void (*request_codec)(VirtViewerDisplay *display,gint codec_id);
};

struct _VirtViewerVideoCodec {
    char name[20];
    gint id;
};

#define VIRT_VIEWER_DISPLAY_CAN_SCREENSHOT(display) \
    (display && (VIRT_VIEWER_DISPLAY_GET_CLASS(display)->get_pixbuf != NULL))

#define VIRT_VIEWER_DISPLAY_CAN_SEND_KEYS(display) \
    (display && (VIRT_VIEWER_DISPLAY_GET_CLASS(display)->send_keys != NULL))

GType virt_viewer_display_get_type(void);

GtkWidget *virt_viewer_display_new(void);

void virt_viewer_display_set_desktop_size(VirtViewerDisplay *display,
                                          guint width,
                                          guint height);

void virt_viewer_display_get_desktop_size(VirtViewerDisplay *display,
                                          guint *width,
                                          guint *height);

void virt_viewer_display_set_zoom_level(VirtViewerDisplay *display,
                                        guint zoom);
guint virt_viewer_display_get_zoom_level(VirtViewerDisplay *display);
void virt_viewer_display_set_zoom(VirtViewerDisplay *display,
                                  gboolean zoom);
gboolean virt_viewer_display_get_zoom(VirtViewerDisplay *display);

void virt_viewer_display_send_keys(VirtViewerDisplay *display,
                                   const guint *keyvals, int nkeyvals);
GdkPixbuf* virt_viewer_display_get_pixbuf(VirtViewerDisplay *display);
void virt_viewer_display_set_show_hint(VirtViewerDisplay *display, guint mask, gboolean enable);
guint virt_viewer_display_get_show_hint(VirtViewerDisplay *display);
VirtViewerSession* virt_viewer_display_get_session(VirtViewerDisplay *display);
void virt_viewer_display_set_monitor(VirtViewerDisplay *display, gint monitor);
gint virt_viewer_display_get_monitor(VirtViewerDisplay *display);
void virt_viewer_display_set_fullscreen(VirtViewerDisplay *display, gboolean fullscreen);
gboolean virt_viewer_display_get_fullscreen(VirtViewerDisplay *display);
void virt_viewer_display_release_cursor(VirtViewerDisplay *display);

void virt_viewer_display_close(VirtViewerDisplay *display);
void virt_viewer_display_set_enabled(VirtViewerDisplay *display, gboolean enabled);
void virt_viewer_display_enable(VirtViewerDisplay *display);
void virt_viewer_display_disable(VirtViewerDisplay *display);
GArray* virt_viewer_display_session_supported_codecs(VirtViewerDisplay *display);
void virt_viewer_display_session_request_codec(VirtViewerDisplay *display, gint codec_id);
gboolean virt_viewer_display_get_enabled(VirtViewerDisplay *display);
gboolean virt_viewer_display_get_selectable(VirtViewerDisplay *display);
void virt_viewer_display_queue_resize(VirtViewerDisplay *display);
void virt_viewer_display_get_preferred_monitor_geometry(VirtViewerDisplay *self, GdkRectangle* preferred);
gint virt_viewer_display_get_nth(VirtViewerDisplay *self);
void virt_viewer_display_set_auto_resize(VirtViewerDisplay *self, gboolean enabled);
gboolean virt_viewer_display_get_auto_resize(VirtViewerDisplay *self);
