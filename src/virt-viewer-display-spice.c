/*
 * Virt Viewer: A virtual machine console viewer
 *
 * Copyright (C) 2007-2009 Red Hat, Inc.
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

#include <math.h>
#include <spice-client-gtk.h>

#include <glib/gi18n.h>

#include "virt-viewer-util.h"
#include "virt-viewer-display-spice.h"
#include "virt-viewer-auth.h"

typedef enum {
    AUTO_RESIZE_ALWAYS,
    AUTO_RESIZE_FULLSCREEN,
    AUTO_RESIZE_NEVER,
} AutoResizeState;

struct _VirtViewerDisplaySpice {
    VirtViewerDisplay parent;
    SpiceChannel *channel; /* weak reference */
    SpiceDisplay *display;
    AutoResizeState auto_resize;
    guint x;
    guint y;
};

G_DEFINE_TYPE(VirtViewerDisplaySpice, virt_viewer_display_spice, VIRT_VIEWER_TYPE_DISPLAY)

static void virt_viewer_display_spice_send_keys(VirtViewerDisplay *display,
                                                const guint *keyvals,
                                                int nkeyvals);
static GdkPixbuf *virt_viewer_display_spice_get_pixbuf(VirtViewerDisplay *display);
static void virt_viewer_display_spice_release_cursor(VirtViewerDisplay *display);
static gboolean virt_viewer_display_spice_selectable(VirtViewerDisplay *display);
static void virt_viewer_display_spice_enable(VirtViewerDisplay *display);
static void virt_viewer_display_spice_disable(VirtViewerDisplay *display);
static GArray * virt_viewer_display_spice_get_session_supported_codecs(VirtViewerDisplay *display);
static void virt_viewer_display_spice_request_codec(VirtViewerDisplay *display, gint codec_id);

static void
virt_viewer_display_spice_class_init(VirtViewerDisplaySpiceClass *klass)
{
    VirtViewerDisplayClass *dclass = VIRT_VIEWER_DISPLAY_CLASS(klass);

    dclass->send_keys = virt_viewer_display_spice_send_keys;
    dclass->get_pixbuf = virt_viewer_display_spice_get_pixbuf;
    dclass->release_cursor = virt_viewer_display_spice_release_cursor;
    dclass->selectable = virt_viewer_display_spice_selectable;
    dclass->enable = virt_viewer_display_spice_enable;
    dclass->disable = virt_viewer_display_spice_disable;
    dclass->codecs = virt_viewer_display_spice_get_session_supported_codecs;
    dclass->request_codec = virt_viewer_display_spice_request_codec;
}

static SpiceMainChannel*
get_main(VirtViewerDisplay *self)
{
    VirtViewerSessionSpice *session;

    session = VIRT_VIEWER_SESSION_SPICE(virt_viewer_display_get_session(self));

    return virt_viewer_session_spice_get_main_channel(session);
}

static void
virt_viewer_display_spice_monitor_geometry_changed(VirtViewerDisplaySpice *self)
{
    g_signal_emit_by_name(self, "monitor-geometry-changed", NULL);
}

static void update_enabled(VirtViewerDisplay *self, gboolean enabled, gboolean send)
{
    SpiceMainChannel *main_channel = get_main(self);
    guint nth;

    /* this may happen when finalizing */
    if (!main_channel)
        return;

    g_object_get(self, "nth-display", &nth, NULL);
    spice_main_channel_update_display_enabled(main_channel, nth, enabled, send);
}

static void
show_hint_changed(VirtViewerDisplay *self)
{
    /* just keep spice-gtk state up-to-date, but don't send change anything */
    update_enabled(self, virt_viewer_display_get_enabled(self), FALSE);
}

static void virt_viewer_display_spice_enable(VirtViewerDisplay *self)
{
    virt_viewer_display_set_enabled(self, TRUE);
    update_enabled(self, TRUE, TRUE);
}

static void virt_viewer_display_spice_disable(VirtViewerDisplay *self)
{
    virt_viewer_display_set_enabled(self, FALSE);
    update_enabled(self, FALSE, TRUE);
}

static void
virt_viewer_display_spice_init(VirtViewerDisplaySpice *self)
{
    self->auto_resize = AUTO_RESIZE_ALWAYS;

    g_signal_connect(self, "notify::show-hint", G_CALLBACK(show_hint_changed), NULL);
}

static void
virt_viewer_display_spice_send_keys(VirtViewerDisplay *display,
                                    const guint *keyvals,
                                    int nkeyvals)
{
    VirtViewerDisplaySpice *self = VIRT_VIEWER_DISPLAY_SPICE(display);

    g_return_if_fail(self != NULL);
    g_return_if_fail(self->display != NULL);

    spice_display_send_keys(self->display, keyvals, nkeyvals, SPICE_DISPLAY_KEY_EVENT_CLICK);
}

static GdkPixbuf *
virt_viewer_display_spice_get_pixbuf(VirtViewerDisplay *display)
{
    VirtViewerDisplaySpice *self = VIRT_VIEWER_DISPLAY_SPICE(display);

    g_return_val_if_fail(self != NULL, NULL);
    g_return_val_if_fail(self->display != NULL, NULL);

    return spice_display_get_pixbuf(self->display);
}

static void
update_display_ready(VirtViewerDisplaySpice *self)
{
    gboolean ready;

    g_object_get(self->display, "ready", &ready, NULL);

    virt_viewer_display_set_show_hint(VIRT_VIEWER_DISPLAY(self),
                                      VIRT_VIEWER_DISPLAY_SHOW_HINT_READY, ready);
}

static void
virt_viewer_display_spice_keyboard_grab(SpiceDisplay *display G_GNUC_UNUSED,
                                        int grabbed,
                                        VirtViewerDisplaySpice *self)
{
    if (grabbed)
        g_signal_emit_by_name(self, "display-keyboard-grab");
    else
        g_signal_emit_by_name(self, "display-keyboard-ungrab");
}


static void
virt_viewer_display_spice_mouse_grab(SpiceDisplay *display G_GNUC_UNUSED,
                                     int grabbed,
                                     VirtViewerDisplaySpice *self)
{
    if (grabbed)
        g_signal_emit_by_name(self, "display-pointer-grab");
    else
        g_signal_emit_by_name(self, "display-pointer-ungrab");
}


static void
virt_viewer_display_spice_size_allocate(VirtViewerDisplaySpice *self,
                                        GtkAllocation *allocation,
                                        gpointer data G_GNUC_UNUSED)
{
    GtkRequisition preferred;

    if (!virt_viewer_display_get_enabled(VIRT_VIEWER_DISPLAY(self)))
        return;

    /* ignore all allocations before the widget gets mapped to screen since we
     * only want to trigger guest resizing due to user actions
     */
    if (!gtk_widget_get_mapped(GTK_WIDGET(self)))
        return;

    /* when the window gets resized due to a change in zoom level, we don't want
     * to re-size the guest display.  So if we get an allocation event that
     * resizes the window to the size it already wants to be (based on desktop
     * size and zoom level), just return early
     */
    gtk_widget_get_preferred_size(GTK_WIDGET(self), NULL, &preferred);
    if (preferred.width == allocation->width
        && preferred.height == allocation->height) {
        return;
    }

    if (self->auto_resize != AUTO_RESIZE_NEVER)
        virt_viewer_display_spice_monitor_geometry_changed(self);

    if (self->auto_resize == AUTO_RESIZE_FULLSCREEN)
        self->auto_resize = AUTO_RESIZE_NEVER;
}

static void
zoom_level_changed(VirtViewerDisplaySpice *self,
                   GParamSpec *pspec G_GNUC_UNUSED,
                   VirtViewerApp *app G_GNUC_UNUSED)
{
    if (self->auto_resize != AUTO_RESIZE_NEVER)
        return;

    virt_viewer_display_spice_monitor_geometry_changed(self);
}

static void
release_cursor_display_hotkey_changed(VirtViewerApp *app,
                                      GParamSpec *pspec G_GNUC_UNUSED,
                                      VirtViewerDisplaySpice *self)
{
    gboolean kiosk;
    gchar *hotkey;
    g_object_get(app, "kiosk", &kiosk, NULL);
    hotkey = virt_viewer_app_get_release_cursor_display_hotkey(app);

    if (kiosk || hotkey == NULL) {
        /* disable default grab sequence */
        SpiceGrabSequence *seq = spice_grab_sequence_new(0, NULL);
        spice_display_set_grab_keys(self->display, seq);
        spice_grab_sequence_free(seq);
    } else {
        hotkey = spice_hotkey_to_display_hotkey(hotkey);
        SpiceGrabSequence *seq = spice_grab_sequence_new_from_string(hotkey);
        g_free(hotkey);
        spice_display_set_grab_keys(self->display, seq);
        spice_grab_sequence_free(seq);
    }
}

static void
resize_policy_changed(VirtViewerDisplaySpice *self,
                      GParamSpec *pspec G_GNUC_UNUSED,
                      VirtViewerApp *app)
{
    if (virt_viewer_display_get_auto_resize(VIRT_VIEWER_DISPLAY(self))) {
        if (virt_viewer_display_get_fullscreen(VIRT_VIEWER_DISPLAY(self))) {
            gboolean auto_conf;
            g_object_get(app, "fullscreen", &auto_conf, NULL);
            if (auto_conf)
                self->auto_resize = AUTO_RESIZE_NEVER;
            else
                self->auto_resize = AUTO_RESIZE_FULLSCREEN;
        } else {
            self->auto_resize = AUTO_RESIZE_ALWAYS;
        }
    } else {
        self->auto_resize = AUTO_RESIZE_NEVER;
    }
}

GtkWidget *
virt_viewer_display_spice_new(VirtViewerSessionSpice *session,
                              SpiceChannel *channel,
                              gint monitorid)
{
    VirtViewerDisplaySpice *self;
    VirtViewerApp *app;
    gint channelid;
    SpiceSession *s;

    g_return_val_if_fail(SPICE_IS_DISPLAY_CHANNEL(channel), NULL);

    g_object_get(channel, "channel-id", &channelid, NULL);
    if (channelid != 0 && monitorid != 0) {
        g_warning("Unsupported graphics configuration:\n"
                  "spice-gtk only supports multiple graphics channels if they are single-head");
        return NULL;
    }

    self = g_object_new(VIRT_VIEWER_TYPE_DISPLAY_SPICE,
                        "session", session,
                        // either monitorid is always 0 or channelid
                        // is, we can't have display (0, 2) and (2, 0)
                        // for example
                        "nth-display", channelid + monitorid,
                        NULL);
    self->channel = channel;

    g_object_get(session, "spice-session", &s, NULL);
    self->display = spice_display_new_with_monitor(s, channelid, monitorid);
    g_object_unref(s);

    virt_viewer_signal_connect_object(self->display, "notify::ready",
                                      G_CALLBACK(update_display_ready), self,
                                      G_CONNECT_SWAPPED);
    update_display_ready(self);

    gtk_container_add(GTK_CONTAINER(self), GTK_WIDGET(self->display));
    gtk_widget_show(GTK_WIDGET(self->display));
    g_object_set(self->display,
                 "grab-keyboard", TRUE,
                 "grab-mouse", TRUE,
                 "resize-guest", FALSE,
                 "scaling", TRUE,
                 NULL);

    virt_viewer_signal_connect_object(self->display, "keyboard-grab",
                                      G_CALLBACK(virt_viewer_display_spice_keyboard_grab), self, 0);
    virt_viewer_signal_connect_object(self->display, "mouse-grab",
                                      G_CALLBACK(virt_viewer_display_spice_mouse_grab), self, 0);
    virt_viewer_signal_connect_object(self, "size-allocate",
                                      G_CALLBACK(virt_viewer_display_spice_size_allocate), self, 0);


    app = virt_viewer_session_get_app(VIRT_VIEWER_SESSION(session));
    virt_viewer_signal_connect_object(app, "notify::release-cursor-display-hotkey",
                                      G_CALLBACK(release_cursor_display_hotkey_changed), self, 0);
    virt_viewer_signal_connect_object(self, "notify::fullscreen",
                                      G_CALLBACK(resize_policy_changed), app, 0);
    virt_viewer_signal_connect_object(self, "notify::auto-resize",
                                      G_CALLBACK(resize_policy_changed), app, 0);
    virt_viewer_signal_connect_object(self, "notify::zoom-level",
                                      G_CALLBACK(zoom_level_changed), app, 0);
    resize_policy_changed(self, NULL, app);
    release_cursor_display_hotkey_changed(app, NULL, self);

    return GTK_WIDGET(self);
}

static void
virt_viewer_display_spice_release_cursor(VirtViewerDisplay *display)
{
    VirtViewerDisplaySpice *self = VIRT_VIEWER_DISPLAY_SPICE(display);

#if SPICE_GTK_CHECK_VERSION(0,40,0)
    spice_display_keyboard_ungrab(self->display);
#endif
    spice_display_mouse_ungrab(self->display);
}

static gboolean
virt_viewer_display_spice_selectable(VirtViewerDisplay *self)
{
    gboolean agent_connected;
    SpiceMainChannel *mainc;

    mainc = get_main(self);
    g_object_get(mainc,
                 "agent-connected", &agent_connected,
                 NULL);

    return agent_connected;
}

void
virt_viewer_display_spice_set_desktop(VirtViewerDisplay *display,
                                      guint x, guint y,
                                      guint width, guint height)
{
    VirtViewerDisplaySpice *self;
    guint desktopWidth, desktopHeight;
    gint scale_factor = gtk_widget_get_scale_factor(GTK_WIDGET(display));

    /* parameter width and height are guest monitor physical pixel sizes */
    /* convert to application pixel sizes */
    width /= scale_factor;
    height /= scale_factor;

    g_return_if_fail(VIRT_VIEWER_IS_DISPLAY_SPICE(display));
    self = VIRT_VIEWER_DISPLAY_SPICE(display);

    virt_viewer_display_get_desktop_size(display, &desktopWidth, &desktopHeight);

    if (desktopWidth == width && desktopHeight == height && self->x == x && self->y == y)
        return;

    g_object_set(G_OBJECT(display), "desktop-width", width, "desktop-height", height, NULL);
    self->x = x;
    self->y = y;

    virt_viewer_display_queue_resize(display);

    g_signal_emit_by_name(display, "display-desktop-resize");
}

GArray * virt_viewer_display_spice_get_session_supported_codecs(VirtViewerDisplay *display) {
    VirtViewerDisplaySpice *self = VIRT_VIEWER_DISPLAY_SPICE(display);
        g_return_val_if_fail(self != NULL,NULL);
        g_return_val_if_fail(self->display != NULL,NULL);
        GArray * codec_ids = spice_display_channel_get_supported_codecs( SPICE_DISPLAY_CHANNEL(self->channel));
        GArray * codecs = g_array_sized_new(FALSE,FALSE,sizeof(VirtViewerVideoCodec),codec_ids->len);
        for (int i = 0; i < codec_ids->len; i++) {
            int codec_id = g_array_index(codec_ids,int,i);
            VirtViewerVideoCodec codec =  {.id=codec_id};
            strncpy(codec.name, spice_display_get_codec_name(codec_id), sizeof(codec.name) - 1);
            g_array_append_val(codecs, codec );
        }
        g_array_free(codec_ids,TRUE);
        return codecs;
    }

void virt_viewer_display_spice_request_codec(VirtViewerDisplay *display,gint codec_id){
    VirtViewerDisplaySpice *self = VIRT_VIEWER_DISPLAY_SPICE(display);
    g_return_if_fail(self != NULL);
    g_return_if_fail(self->display != NULL);
    spice_display_channel_change_preferred_video_codec_type( self->channel, codec_id);
}
