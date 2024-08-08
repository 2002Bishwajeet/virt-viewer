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
#include <gio/gio.h>
#include <glib/gprintf.h>
#include <glib/gi18n.h>
#include <errno.h>

#ifndef G_OS_WIN32
#include <glib-unix.h>
#include <sys/socket.h>
#include <sys/un.h>
#else
#include <windows.h>
#endif

#include "virt-viewer-app.h"
#include "virt-viewer-resources.h"
#include "virt-viewer-auth.h"
#include "virt-viewer-window.h"
#include "virt-viewer-session.h"
#include "virt-viewer-util.h"
#ifdef HAVE_GTK_VNC
#include "virt-viewer-session-vnc.h"
#endif
#ifdef HAVE_SPICE_GTK
#include "virt-viewer-session-spice.h"
#endif

#include "virt-viewer-display-vte.h"

gboolean doDebug = FALSE;

/* Signal handlers for about dialog */
void virt_viewer_app_about_close(GtkWidget *dialog, VirtViewerApp *self);
void virt_viewer_app_about_delete(GtkWidget *dialog, void *dummy, VirtViewerApp *self);


/* Internal methods */
static void virt_viewer_app_connected(VirtViewerSession *session,
                                      VirtViewerApp *self);
static void virt_viewer_app_initialized(VirtViewerSession *session,
                                        VirtViewerApp *self);
static void virt_viewer_app_disconnected(VirtViewerSession *session,
                                         const gchar *msg,
                                         VirtViewerApp *self);
static void virt_viewer_app_auth_refused(VirtViewerSession *session,
                                         const char *msg,
                                         VirtViewerApp *self);
static void virt_viewer_app_auth_unsupported(VirtViewerSession *session,
                                             const char *msg,
                                        VirtViewerApp *self);
static void virt_viewer_app_usb_failed(VirtViewerSession *session,
                                       const char *msg,
                                       VirtViewerApp *self);

static void virt_viewer_app_server_cut_text(VirtViewerSession *session,
                                            const gchar *text,
                                            VirtViewerApp *self);
static void virt_viewer_app_bell(VirtViewerSession *session,
                                 VirtViewerApp *self);

static void virt_viewer_app_cancelled(VirtViewerSession *session,
                                      VirtViewerApp *self);

static void virt_viewer_app_channel_open(VirtViewerSession *session,
                                         VirtViewerSessionChannel *channel,
                                         VirtViewerApp *self);
static void virt_viewer_app_update_pretty_address(VirtViewerApp *self);
static void virt_viewer_app_set_fullscreen(VirtViewerApp *self, gboolean fullscreen);
static void virt_viewer_app_update_menu_displays(VirtViewerApp *self);
static void virt_viewer_update_smartcard_accels(VirtViewerApp *self);
static void virt_viewer_update_usbredir_accels(VirtViewerApp *self);
static void virt_viewer_app_add_option_entries(VirtViewerApp *self, GOptionContext *context, GOptionGroup *group);
static VirtViewerWindow *virt_viewer_app_get_nth_window(VirtViewerApp *self, gint nth);
static VirtViewerWindow *virt_viewer_app_get_vte_window(VirtViewerApp *self, const gchar *name);
static void virt_viewer_app_set_actions_sensitive(VirtViewerApp *self);
static void virt_viewer_app_set_display_auto_resize(VirtViewerApp *self,
                                                    VirtViewerDisplay *display);

/* Application actions */
static void virt_viewer_app_action_monitor(GSimpleAction *act,
                                           GVariant *param,
                                           gpointer opaque);
static void virt_viewer_app_action_vte(GSimpleAction *act,
                                       GVariant *state,
                                       gpointer opaque);
static void window_code_change_cb (GSimpleAction *action, GVariant *value, gpointer user_data);


typedef struct _VirtViewerAppPrivate VirtViewerAppPrivate;
struct _VirtViewerAppPrivate {
    VirtViewerWindow *main_window;
    GtkWidget *main_notebook;
    GList *windows;
    GHashTable *displays; /* !vte */
    GHashTable *initial_display_map;
    gchar *clipboard;
    GtkWidget *preferences;
    GtkFileChooser *preferences_shared_folder;
    GResource *resource;
    gboolean direct;
    gboolean verbose;
    gboolean authretry;
    gboolean started;
    gboolean fullscreen;
    gboolean attach;
    gboolean shared;
    gboolean quitting;
    gboolean kiosk;
    gboolean vm_ui;
    gboolean vm_running;
    gboolean initialized;

    VirtViewerSession *session;
    gboolean active;
    gboolean connected;
    gboolean cancelled;
    char *unixsock;
    char *guri; /* preferred over ghost:gport */
    char *ghost;
    char *gport;
    char *gtlsport;
    char *host; /* ssh */
    int port;/* ssh */
    char *user; /* ssh */
    char *transport;
    char *pretty_address;
    gchar *guest_name;
    gboolean grabbed;
    char *title;
    char *uuid;
    VirtViewerCursor cursor;

    GKeyFile *config;
    gchar *config_file;

    gchar *release_cursor_display_hotkey;
    gchar **insert_smartcard_accel;
    gchar **remove_smartcard_accel;
    gchar **usb_device_reset_accel;
    gboolean quit_on_disconnect;
    gboolean supports_share_clipboard;
    VirtViewerKeyMapping *keyMappings;
};


G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE(VirtViewerApp, virt_viewer_app, GTK_TYPE_APPLICATION)

enum {
    PROP_0,
    PROP_VERBOSE,
    PROP_SESSION,
    PROP_GUEST_NAME,
    PROP_GURI,
    PROP_FULLSCREEN,
    PROP_TITLE,
    PROP_RELEASE_CURSOR_DISPLAY_HOTKEY,
    PROP_KIOSK,
    PROP_QUIT_ON_DISCONNECT,
    PROP_UUID,
    PROP_VM_UI,
    PROP_VM_RUNNING,
    PROP_CONFIG_SHARE_CLIPBOARD,
    PROP_SUPPORTS_SHARE_CLIPBOARD,
};

void
virt_viewer_app_set_debug(gboolean debug)
{
    if (debug) {
        const gchar *doms = g_getenv("G_MESSAGES_DEBUG");
        if (!doms) {
            g_setenv("G_MESSAGES_DEBUG", G_LOG_DOMAIN, 1);
        } else if (!g_str_equal(doms, "all") &&
                   !strstr(doms, G_LOG_DOMAIN)) {
            gchar *newdoms = g_strdup_printf("%s %s", doms, G_LOG_DOMAIN);
            g_setenv("G_MESSAGES_DEBUG", newdoms, 1);
            g_free(newdoms);
        }
    }
    doDebug = debug;
}

static GtkWidget* G_GNUC_PRINTF(2, 3)
virt_viewer_app_make_message_dialog(VirtViewerApp *self,
                                    const char *fmt, ...)
{
    g_return_val_if_fail(VIRT_VIEWER_IS_APP(self), NULL);
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    GtkWindow *window = GTK_WINDOW(virt_viewer_window_get_window(priv->main_window));
    GtkWidget *dialog;
    char *msg;
    va_list vargs;

    va_start(vargs, fmt);
    msg = g_strdup_vprintf(fmt, vargs);
    va_end(vargs);

    dialog = gtk_message_dialog_new(window,
                                    GTK_DIALOG_MODAL |
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_ERROR,
                                    GTK_BUTTONS_OK,
                                    "%s",
                                    msg);

    g_free(msg);

    return dialog;
}

void
virt_viewer_app_simple_message_dialog(VirtViewerApp *self,
                                      const char *fmt, ...)
{
    GtkWidget *dialog;
    char *msg;
    va_list vargs;

    va_start(vargs, fmt);
    msg = g_strdup_vprintf(fmt, vargs);
    va_end(vargs);

    dialog = virt_viewer_app_make_message_dialog(self, "%s", msg);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    g_free(msg);
}

static void
virt_viewer_app_save_config(VirtViewerApp *self)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    GError *error = NULL;
    gchar *dir, *data;

    dir = g_path_get_dirname(priv->config_file);
    if (g_mkdir_with_parents(dir, S_IRWXU) == -1)
        g_warning("failed to create config directory");
    g_free(dir);

    if (priv->uuid && priv->guest_name && g_key_file_has_group(priv->config, priv->uuid)) {
        // if there's no comment for this uuid settings group, add a comment
        // with the vm name so user can make sense of it later.
        gchar *comment = g_key_file_get_comment(priv->config, priv->uuid, NULL, &error);
        if (error) {
            g_debug("Unable to get comment from key file: %s", error->message);
            g_clear_error(&error);
        }

        if (comment == NULL ||
                (comment != NULL && g_strstr_len(comment, -1, priv->guest_name) == NULL)) {
            /* Note that this function appends the guest's name string as last
             * comment in case there were comments there already */
            g_key_file_set_comment(priv->config, priv->uuid, NULL, priv->guest_name, NULL);
        }
        g_free(comment);
    }

    if ((data = g_key_file_to_data(priv->config, NULL, &error)) == NULL ||
        !g_file_set_contents(priv->config_file, data, -1, &error)) {
        g_warning("Couldn't save configuration: %s", error->message);
        g_clear_error(&error);
    }
    g_free(data);
}

static void
virt_viewer_app_quit(VirtViewerApp *self)
{
    g_return_if_fail(VIRT_VIEWER_IS_APP(self));
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    g_return_if_fail(!priv->kiosk);

    virt_viewer_app_save_config(self);

    if (priv->vm_ui && virt_viewer_session_has_vm_action(priv->session,
                                                         VIRT_VIEWER_SESSION_VM_ACTION_QUIT)) {
        virt_viewer_session_vm_action(VIRT_VIEWER_SESSION(priv->session),
                                      VIRT_VIEWER_SESSION_VM_ACTION_QUIT);
    }

    priv->quitting = TRUE;
    if (priv->session) {
        virt_viewer_session_close(VIRT_VIEWER_SESSION(priv->session));
        if (priv->connected) {
            return;
        }
    }

    g_application_quit(G_APPLICATION(self));
}

static gint
get_n_client_monitors(void)
{
    return gdk_screen_get_n_monitors(gdk_screen_get_default());
}

GList* virt_viewer_app_get_initial_displays(VirtViewerApp* self)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);

    if (!priv->initial_display_map) {
        GList *l = NULL;
        gint i;
        gint n = get_n_client_monitors();

        for (i = 0; i < n; i++) {
            l = g_list_append(l, GINT_TO_POINTER(i));
        }
        return l;
    }
    return g_hash_table_get_keys(priv->initial_display_map);
}

static gint virt_viewer_app_get_first_monitor(VirtViewerApp *self)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);

    if (priv->fullscreen && priv->initial_display_map) {
        gint first = G_MAXINT;
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init(&iter, priv->initial_display_map);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            gint monitor = GPOINTER_TO_INT(key);
            first = MIN(first, monitor);
        }
        return first;
    }
    return 0;
}

gint virt_viewer_app_get_initial_monitor_for_display(VirtViewerApp* self, gint display)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    gint monitor = display;

    if (priv->initial_display_map) {
        gpointer value = NULL;
        if (g_hash_table_lookup_extended(priv->initial_display_map, GINT_TO_POINTER(display), NULL, &value)) {
            monitor = GPOINTER_TO_INT(value);
        } else {
            monitor = -1;
        }
    }
    if (monitor >= get_n_client_monitors()) {
        g_debug("monitor for display %d does not exist", display);
        monitor = -1;
    }

    return monitor;
}

static void
app_window_try_fullscreen(VirtViewerApp *self G_GNUC_UNUSED,
                          VirtViewerWindow *win, gint nth)
{
    gint monitor = virt_viewer_app_get_initial_monitor_for_display(self, nth);
    if (monitor == -1) {
        g_debug("skipping fullscreen for display %d", nth);
        return;
    }

    virt_viewer_window_enter_fullscreen(win, monitor);
}

static GHashTable*
virt_viewer_app_get_monitor_mapping_for_section(VirtViewerApp *self, const gchar *section)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    GError *error = NULL;
    gsize nmappings = 0;
    gchar **mappings = NULL;
    GHashTable *mapping = NULL;

    mappings = g_key_file_get_string_list(priv->config,
                                          section, "monitor-mapping", &nmappings, &error);
    if (error) {
        if (error->code != G_KEY_FILE_ERROR_GROUP_NOT_FOUND
            && error->code != G_KEY_FILE_ERROR_KEY_NOT_FOUND)
            g_warning("Error reading monitor assignments for %s: %s", section, error->message);
        g_clear_error(&error);
    } else {
        mapping = virt_viewer_parse_monitor_mappings(mappings, nmappings, get_n_client_monitors());
    }
    g_strfreev(mappings);

    return mapping;
}

/*
 *  save the association display/monitor in the config for reuse on next connection
 */
static void virt_viewer_app_set_monitor_mapping_for_display(VirtViewerApp *self,
                                                            VirtViewerDisplay *display)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    GError *error = NULL;
    gsize nmappings = 0;
    gchar **mappings = NULL;
    gchar **tokens = NULL;

    int i;

    gint virt_viewer_display = virt_viewer_display_get_nth(display);
    gint virt_viewer_monitor = virt_viewer_display_get_monitor(display);

    if (virt_viewer_monitor == -1) {
        // find which monitor the window is on
#if GTK_CHECK_VERSION(3, 22, 0)
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        GdkDisplay *gdk_dpy = gdk_display_get_default();
        VirtViewerWindow *vvWindow = virt_viewer_app_get_nth_window(self, virt_viewer_display);
        GdkWindow *window = gtk_widget_get_window(
                                            GTK_WIDGET(virt_viewer_window_get_window(vvWindow)));
        GdkMonitor *pMonitor = gdk_display_get_monitor_at_window(gdk_dpy, window);

        // compare this monitor with the list of monitors from the display
        gint num_monitors = gdk_display_get_n_monitors(gdk_dpy);
        if (num_monitors > 0) {
            for (i = 0; i < num_monitors; i++) {
                GdkMonitor *tmp = gdk_display_get_monitor(gdk_dpy, i);
                if (tmp == pMonitor) {
                    virt_viewer_monitor = i;
                    break;
                }
            }
        }
        G_GNUC_END_IGNORE_DEPRECATIONS
#endif
        if (virt_viewer_monitor == -1) {
            // could not determine the monitor - abort
            return;
        }
    }

    // IDs are 0 based, but the config uses 1-based numbering
    virt_viewer_display++;
    virt_viewer_monitor++;

    mappings = g_key_file_get_string_list(priv->config, priv->uuid,
                                          "monitor-mapping", &nmappings, &error);
    if (error) {
        if (error->code != G_KEY_FILE_ERROR_GROUP_NOT_FOUND
                && error->code != G_KEY_FILE_ERROR_KEY_NOT_FOUND)
            g_warning("Error reading monitor assignments for %s: %s",
                      priv->uuid, error->message);
        g_clear_error(&error);

        // no mapping available for the VM: we will create a new one
    }

    for (i = 0; i < nmappings; i++) {
        gchar *endptr = NULL;
        gint disp, monitor;

        tokens = g_strsplit(mappings[i], ":", 2);
        if (g_strv_length(tokens) != 2) {
            // config error
            g_strfreev(tokens);
            goto end;
        }

        disp = strtol(tokens[0], &endptr, 10);
        if ((endptr && *endptr != '\0') || disp < 1) {
            // config error
            g_strfreev(tokens);
            goto end;
        }

        if (disp == virt_viewer_display) {
            // found the display we have to save. Verify if it changed mappings
            monitor = strtol(tokens[1], &endptr, 10);
            if ((endptr && *endptr != '\0') || monitor < 1) {
                // config error
                g_strfreev(tokens);
                goto end;
            }

            g_strfreev(tokens);
            if (monitor == virt_viewer_monitor) {
                // no change in the config - just exit
                goto end;
            }

            // save the modified mapping
            g_snprintf(mappings[i], strlen(mappings[i]) + 1, "%d:%d",
                       virt_viewer_display, virt_viewer_monitor);
            break;
        }
        g_strfreev(tokens);
    }
    if (i == nmappings) {
        // this display was not saved yet - add it
        nmappings++;
        mappings = g_realloc(mappings, (nmappings + 1) * sizeof(gchar *));
        mappings[nmappings - 1] = g_strdup_printf("%d:%d", virt_viewer_display, virt_viewer_monitor);
        mappings[nmappings] = NULL;
    }
    g_key_file_set_string_list(priv->config, priv->uuid, "monitor-mapping",
                               (const gchar * const *) mappings, nmappings);
    virt_viewer_app_save_config(self);

end:
    g_strfreev(mappings);
}

static
void virt_viewer_app_apply_monitor_mapping(VirtViewerApp *self)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    GHashTable *mapping = NULL;

    // apply mapping only in fullscreen
    if (!virt_viewer_app_get_fullscreen(self))
        return;

    mapping = virt_viewer_app_get_monitor_mapping_for_section(self, priv->uuid);
    if (!mapping) {
        g_debug("No guest-specific fullscreen config, using fallback");
        mapping = virt_viewer_app_get_monitor_mapping_for_section(self, "fallback");
    }

    if (priv->initial_display_map)
        g_hash_table_unref(priv->initial_display_map);

    priv->initial_display_map = mapping;

    // if we're changing our initial display map, move any existing windows to
    // the appropriate monitors according to the per-vm configuration
    if (mapping) {
        GList *l;
        gint i = 0;

        for (l = priv->windows; l; l = l->next) {
            app_window_try_fullscreen(self, VIRT_VIEWER_WINDOW(l->data), i);
            i++;
        }
    }
}

static
void virt_viewer_app_set_uuid_string(VirtViewerApp *self, const gchar *uuid_string)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    if (g_strcmp0(priv->uuid, uuid_string) == 0)
        return;

    g_debug("%s: UUID changed to %s", G_STRFUNC, uuid_string);

    g_free(priv->uuid);
    priv->uuid = g_strdup(uuid_string);

    virt_viewer_app_apply_monitor_mapping(self);
}

static
void virt_viewer_app_set_keymap(VirtViewerApp *self, const gchar *keymap_string)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    gchar **key, **keymaps = NULL, **valkey, **valuekeys = NULL;
    VirtViewerKeyMapping *keyMappingArray, *keyMappingPtr;
    guint *mappedArray, *ptrMove;

    if (keymap_string == NULL) {
        g_debug("keymap string is empty - nothing to do");
        priv->keyMappings = NULL;
        return;
    }

    g_debug("keymap string set to %s", keymap_string);

    g_return_if_fail(VIRT_VIEWER_IS_APP(self));

    g_debug("keymap command-line set to %s", keymap_string);
    if (keymap_string) {
        keymaps = g_strsplit(keymap_string, ",", -1);
    }

    if (!keymaps || g_strv_length(keymaps) == 0) {
        g_strfreev(keymaps);
        return;
    }

    keyMappingPtr = keyMappingArray = g_new0(VirtViewerKeyMapping,  g_strv_length(keymaps));

    g_debug("Allocated %d number of mappings",  g_strv_length(keymaps));

    for (key = keymaps; *key != NULL; key++) {
        gchar *srcKey = strstr(*key, "=");
        const gchar *value = (srcKey == NULL) ? NULL : (*srcKey = '\0', srcKey + 1);
        if (value == NULL) {
                g_warning("Missing mapping value for key '%s'", srcKey);
                continue;
        }

        // Key value must be resolved to GDK key code
        // along with mapped key which can also be void (for no action)
        guint kcode;
        kcode = gdk_keyval_from_name(*key);
        if (kcode == GDK_KEY_VoidSymbol) {
                g_warning("Unable to lookup '%s' key", *key);
                continue;
        }
        g_debug("Mapped source key '%s' to %x", *key, kcode);

        valuekeys = g_strsplit(value, "+", -1);

        keyMappingPtr->sourceKey = kcode;
        keyMappingPtr->numMappedKeys = g_strv_length(valuekeys);
        keyMappingPtr->isLast = FALSE;

        if (!valuekeys || g_strv_length(valuekeys) == 0) {
                g_debug("No value set for key '%s' it will be blocked", *key);
                keyMappingPtr->mappedKeys = NULL;
                keyMappingPtr++;
                g_strfreev(valuekeys);
                continue;
        }

        ptrMove = mappedArray = g_new0(guint, g_strv_length(valuekeys));

        guint mcode;
        for (valkey = valuekeys; *valkey != NULL; valkey++) {
                g_debug("Value key to map '%s'", *valkey);
                mcode = gdk_keyval_from_name(*valkey);
                if (mcode == GDK_KEY_VoidSymbol) {
                        g_warning("Unable to lookup mapped key '%s' it will be ignored", *valkey);
                }
                g_debug("Mapped dest key '%s' to %x", *valkey, mcode);
                *ptrMove++ = mcode;
        }
        keyMappingPtr->mappedKeys = mappedArray;
        keyMappingPtr++;
        g_strfreev(valuekeys);

    }
    keyMappingPtr--;
    keyMappingPtr->isLast=TRUE;

    priv->keyMappings = keyMappingArray;
    g_strfreev(keymaps);
}

void
virt_viewer_app_maybe_quit(VirtViewerApp *self, VirtViewerWindow *window)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    GError *error = NULL;

    if (priv->kiosk) {
        g_warning("The app is in kiosk mode and can't quit");
        return;
    }

    gboolean ask = g_key_file_get_boolean(priv->config,
                                          "virt-viewer", "ask-quit", &error);
    if (error) {
        ask = TRUE;
        g_clear_error(&error);
    }

    if (ask) {
        GtkWidget *dialog =
            gtk_message_dialog_new (virt_viewer_window_get_window(window),
                    GTK_DIALOG_DESTROY_WITH_PARENT,
                    GTK_MESSAGE_QUESTION,
                    GTK_BUTTONS_OK_CANCEL,
                    _("Do you want to close the session?"));

        GtkWidget *check = gtk_check_button_new_with_label(_("Do not ask me again"));
        gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), check);
        gtk_widget_show(check);

        gtk_dialog_set_default_response (GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
        gint result = gtk_dialog_run(GTK_DIALOG(dialog));

        gboolean dont_ask = FALSE;
        g_object_get(check, "active", &dont_ask, NULL);
        g_key_file_set_boolean(priv->config,
                    "virt-viewer", "ask-quit", !dont_ask);

        gtk_widget_destroy(dialog);
        switch (result) {
            case GTK_RESPONSE_OK:
                virt_viewer_app_quit(self);
                break;
            default:
                break;
        }
    } else {
        virt_viewer_app_quit(self);
    }

}

static void count_window_visible(gpointer value,
                                 gpointer user_data)
{
    GtkWindow *win = virt_viewer_window_get_window(VIRT_VIEWER_WINDOW(value));
    guint *n = (guint*)user_data;

    if (gtk_widget_get_visible(GTK_WIDGET(win)))
        *n += 1;
}

static guint
virt_viewer_app_get_n_windows_visible(VirtViewerApp *self)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    guint n = 0;
    g_list_foreach(priv->windows, count_window_visible, &n);
    return n;
}

static void hide_one_window(gpointer value,
                            gpointer user_data G_GNUC_UNUSED)
{
    VirtViewerApp* self = VIRT_VIEWER_APP(user_data);
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    gboolean connect_error = !priv->cancelled && !priv->connected;
#ifdef HAVE_GTK_VNC
    if (VIRT_VIEWER_IS_SESSION_VNC(priv->session)) {
        connect_error = !priv->cancelled && !priv->initialized;
    }
#endif

    if (connect_error || priv->main_window != value)
        virt_viewer_window_hide(VIRT_VIEWER_WINDOW(value));
}

static void
virt_viewer_app_hide_all_windows(VirtViewerApp *self)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    g_list_foreach(priv->windows, hide_one_window, self);
}

G_MODULE_EXPORT void
virt_viewer_app_about_close(GtkWidget *dialog,
                            VirtViewerApp *self G_GNUC_UNUSED)
{
    gtk_widget_hide(dialog);
    gtk_widget_destroy(dialog);
}

G_MODULE_EXPORT void
virt_viewer_app_about_delete(GtkWidget *dialog,
                             void *dummy G_GNUC_UNUSED,
                             VirtViewerApp *self G_GNUC_UNUSED)
{
    gtk_widget_hide(dialog);
    gtk_widget_destroy(dialog);
}

#ifndef G_OS_WIN32

static int
virt_viewer_app_open_tunnel(const char **cmd)
{
    int fd[2];
    pid_t pid;

    if (socketpair(PF_UNIX, SOCK_STREAM, 0, fd) < 0)
        return -1;

    pid = fork();
    if (pid == -1) {
        close(fd[0]);
        close(fd[1]);
        return -1;
    }

    if (pid == 0) { /* child */
        close(fd[0]);
        close(0);
        close(1);
        if (dup(fd[1]) < 0)
            _exit(1);
        if (dup(fd[1]) < 0)
            _exit(1);
        close(fd[1]);
        execvp("ssh", (char *const*)cmd);
        _exit(1);
    }
    close(fd[1]);
    return fd[0];
}


static int
virt_viewer_app_open_tunnel_ssh(const char *sshhost,
                                int sshport,
                                const char *sshuser,
                                const char *host,
                                const char *port,
                                const char *unixsock)
{
    const char *cmd[8];
    char portstr[12] = { 0 };
    int n = 0;
    GString *cat;

    cmd[n++] = "ssh";
    if (sshport) {
        cmd[n++] = "-p";
        sprintf(portstr, "%d", sshport);
        cmd[n++] = portstr;
    }
    if (sshuser) {
        cmd[n++] = "-l";
        cmd[n++] = sshuser;
    }
    cmd[n++] = sshhost;

    cat = g_string_new("sh -c 'if (command -v socat) >/dev/null 2>&1");

    g_string_append(cat, "; then socat - ");
    if (port) {
        // Wrap raw IPv6 address in []
        const char *connect_str;
        if (strstr(host, ":") != NULL) {
            connect_str = "TCP:[%s]:%s";
        } else {
            connect_str = "TCP:%s:%s";
        }
        g_string_append_printf(cat, connect_str, host, port);
    } else
        g_string_append_printf(cat, "UNIX-CONNECT:%s", unixsock);

    g_string_append(cat, "; else nc ");
    if (port)
        g_string_append_printf(cat, "%s %s", host, port);
    else
        g_string_append_printf(cat, "-U %s", unixsock);

    g_string_append(cat, "; fi'");

    cmd[n++] = cat->str;
    cmd[n++] = NULL;

    n = virt_viewer_app_open_tunnel(cmd);
    g_string_free(cat, TRUE);

    return n;
}

static int
virt_viewer_app_open_unix_sock(const char *unixsock, GError **error)
{
    struct sockaddr_un addr;
    int fd;

    if (strlen(unixsock) + 1 > sizeof(addr.sun_path)) {
        g_set_error(error, VIRT_VIEWER_ERROR, VIRT_VIEWER_ERROR_FAILED,
                    _("Address is too long for UNIX socket_path: %s"), unixsock);
        return -1;
    }

    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, unixsock);

    if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        g_set_error(error, VIRT_VIEWER_ERROR, VIRT_VIEWER_ERROR_FAILED,
                    _("Creating UNIX socket failed: %s"), g_strerror(errno));
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        g_set_error(error, VIRT_VIEWER_ERROR, VIRT_VIEWER_ERROR_FAILED,
                    _("Connecting to UNIX socket failed: %s"), g_strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

#endif /* ! G_OS_WIN32 */

void
virt_viewer_app_trace(VirtViewerApp *self,
                      const char *fmt, ...)
{
    g_return_if_fail(VIRT_VIEWER_IS_APP(self));
    va_list ap;
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);

    if (doDebug) {
        va_start(ap, fmt);
        g_logv(G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, fmt, ap);
        va_end(ap);
    }

    if (priv->verbose) {
        va_start(ap, fmt);
        g_vprintf(fmt, ap);
        va_end(ap);
        g_print("\n");
    }
}

static const gchar*
virt_viewer_app_get_title(VirtViewerApp *self)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    const gchar *title;
    g_return_val_if_fail(VIRT_VIEWER_IS_APP(self), NULL);

    title = priv->title;
    if (!title)
        title = priv->guest_name;
    if (!title)
        title = priv->guri;

    return title;
}

static void
virt_viewer_app_set_window_subtitle(VirtViewerApp *app,
                                    VirtViewerWindow *window,
                                    int nth)
{
    gchar *subtitle = NULL;
    const gchar *title = virt_viewer_app_get_title(app);

    if (title != NULL) {
        VirtViewerDisplay *display = virt_viewer_window_get_display(window);
        gchar *d = strstr(title, "%d");
        gchar *desc = NULL;

        if (display && VIRT_VIEWER_IS_DISPLAY_VTE(display)) {
            g_object_get(display, "name", &desc, NULL);
        } else  {
            desc = g_strdup_printf("%d", nth + 1);
        }

        if (d != NULL) {
            *d = '\0';
            subtitle = g_strdup_printf("%s%s%s", title, desc, d + 2);
            *d = '%';
        } else
            subtitle = g_strdup_printf("%s (%s)", title, desc);
        g_free(desc);
    }

    g_object_set(window, "subtitle", subtitle, NULL);
    g_free(subtitle);
}

static void
set_subtitle(gpointer value,
             gpointer user_data)
{
    VirtViewerApp *app = user_data;
    VirtViewerWindow *window = value;
    VirtViewerDisplay *display = virt_viewer_window_get_display(window);

    if (!display)
        return;

    virt_viewer_app_set_window_subtitle(app, window,
                                        virt_viewer_display_get_nth(display));
}

static void
virt_viewer_app_set_all_window_subtitles(VirtViewerApp *self)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    g_list_foreach(priv->windows, set_subtitle, self);
}

static void update_title(gpointer value,
                         gpointer user_data G_GNUC_UNUSED)
{
    virt_viewer_window_update_title(VIRT_VIEWER_WINDOW(value));
}

static void
virt_viewer_app_update_title(VirtViewerApp *self)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    g_list_foreach(priv->windows, update_title, NULL);
}

static void set_usb_options_sensitive(gpointer value,
                                      gpointer user_data)
{
    virt_viewer_window_set_usb_options_sensitive(VIRT_VIEWER_WINDOW(value),
                                                 GPOINTER_TO_INT(user_data));
}

static void
virt_viewer_app_set_usb_options_sensitive(VirtViewerApp *self, gboolean sensitive)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    g_list_foreach(priv->windows, set_usb_options_sensitive,
                   GINT_TO_POINTER(sensitive));
}

static void set_usb_reset_sensitive(gpointer value,
                                    gpointer user_data)
{
    virt_viewer_window_set_usb_reset_sensitive(VIRT_VIEWER_WINDOW(value),
                                               GPOINTER_TO_INT(user_data));
}

static void
virt_viewer_app_set_usb_reset_sensitive(VirtViewerApp *self, gboolean sensitive)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    g_list_foreach(priv->windows, set_usb_reset_sensitive,
                   GINT_TO_POINTER(sensitive));
}

static void
set_actions_sensitive(gpointer value, gpointer user_data)
{
    VirtViewerApp *self = VIRT_VIEWER_APP(user_data);
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    virt_viewer_window_set_actions_sensitive(VIRT_VIEWER_WINDOW(value),
                                             priv->connected);
}

static void
virt_viewer_app_set_actions_sensitive(VirtViewerApp *self)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    GActionMap *map = G_ACTION_MAP(self);
    GAction *action;

    g_list_foreach(priv->windows, set_actions_sensitive, self);

    action = g_action_map_lookup_action(map, "machine-pause");
    g_simple_action_set_enabled(G_SIMPLE_ACTION(action),
                                priv->connected &&
                                priv->vm_ui &&
                                virt_viewer_session_has_vm_action(priv->session,
                                                                  VIRT_VIEWER_SESSION_VM_ACTION_PAUSE));

    action = g_action_map_lookup_action(map, "machine-reset");
    g_simple_action_set_enabled(G_SIMPLE_ACTION(action),
                                priv->connected &&
                                priv->vm_ui &&
                                virt_viewer_session_has_vm_action(priv->session,
                                                                  VIRT_VIEWER_SESSION_VM_ACTION_RESET));

    action = g_action_map_lookup_action(map, "machine-powerdown");
    g_simple_action_set_enabled(G_SIMPLE_ACTION(action),
                                priv->connected &&
                                priv->vm_ui &&
                                virt_viewer_session_has_vm_action(priv->session,
                                                                  VIRT_VIEWER_SESSION_VM_ACTION_POWER_DOWN));

    action = g_action_map_lookup_action(map, "auto-resize");
    g_simple_action_set_enabled(G_SIMPLE_ACTION(action),
                                priv->connected);
}

static VirtViewerWindow *
virt_viewer_app_get_nth_window(VirtViewerApp *self, gint nth)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    GList *l;

    if (nth < 0)
        return NULL;

    for (l = priv->windows; l; l = l->next) {
        VirtViewerDisplay *display = virt_viewer_window_get_display(l->data);
        if (display
            && (virt_viewer_display_get_nth(display) == nth)) {
            return l->data;
        }
    }
    return NULL;
}

static VirtViewerWindow *
virt_viewer_app_get_vte_window(VirtViewerApp *self, const gchar *name)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    GList *l;

    if (!name)
        return NULL;

    for (l = priv->windows; l; l = l->next) {
        VirtViewerDisplay *display = virt_viewer_window_get_display(l->data);
        if (display &&
            VIRT_VIEWER_IS_DISPLAY_VTE(display)) {
            char *thisname = NULL;
            gboolean match;
            g_object_get(display, "name", &thisname, NULL);
            match = thisname && g_str_equal(name, thisname);
            g_free(thisname);
            if (match) {
                return l->data;
            }
        }
    }
    return NULL;
}

static void
viewer_window_visible_cb(GtkWidget *widget G_GNUC_UNUSED,
                         gpointer user_data)
{
    virt_viewer_app_update_menu_displays(VIRT_VIEWER_APP(user_data));
}

static gboolean
virt_viewer_app_has_usbredir(VirtViewerApp *self)
{
    return virt_viewer_app_has_session(self) &&
           virt_viewer_session_get_has_usbredir(virt_viewer_app_get_session(self));
}

static VirtViewerWindow*
virt_viewer_app_window_new(VirtViewerApp *self, gint nth)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    VirtViewerWindow* window;
    GtkWindow *w;
    gboolean has_usbredir;

    window = virt_viewer_app_get_nth_window(self, nth);
    if (window)
        return window;

    window = g_object_new(VIRT_VIEWER_TYPE_WINDOW, "app", self, NULL);
    virt_viewer_window_set_kiosk(window, priv->kiosk);
    if (priv->main_window)
        virt_viewer_window_set_zoom_level(window, virt_viewer_window_get_zoom_level(priv->main_window));

    priv->windows = g_list_append(priv->windows, window);
    virt_viewer_app_set_window_subtitle(self, window, nth);
    virt_viewer_app_update_menu_displays(self);

    has_usbredir = virt_viewer_app_has_usbredir(self);
    virt_viewer_window_set_usb_options_sensitive(window, has_usbredir);
    virt_viewer_window_set_usb_reset_sensitive(window, has_usbredir);

    w = virt_viewer_window_get_window(window);
    g_object_set_data(G_OBJECT(w), "virt-viewer-window", window);
    gtk_application_add_window(GTK_APPLICATION(self), w);

    if (priv->fullscreen)
        app_window_try_fullscreen(self, window, nth);

    g_signal_connect(w, "hide", G_CALLBACK(viewer_window_visible_cb), self);
    g_signal_connect(w, "show", G_CALLBACK(viewer_window_visible_cb), self);

    if (priv->keyMappings) {
       g_object_set(window, "keymap", priv->keyMappings, NULL);
    }

    return window;
}

static void
window_weak_notify(gpointer data, GObject *where_was G_GNUC_UNUSED)
{
    VirtViewerDisplay *display = VIRT_VIEWER_DISPLAY(data);

    g_object_set_data(G_OBJECT(display), "virt-viewer-window", NULL);
}

static VirtViewerWindow *
ensure_window_for_display(VirtViewerApp *self, VirtViewerDisplay *display)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    gint nth = virt_viewer_display_get_nth(display);
    VirtViewerWindow *win = virt_viewer_app_get_nth_window(self, nth);

    if (VIRT_VIEWER_IS_DISPLAY_VTE(display)) {
        win = g_object_get_data(G_OBJECT(display), "virt-viewer-window");
    }

    if (win == NULL) {
        GList *l = priv->windows;

        /* There should always be at least a main window created at startup */
        g_return_val_if_fail(l != NULL, NULL);
        /* if there's a window that doesn't yet have an associated display, use
         * that window */
        for (; l; l = l->next) {
            if (virt_viewer_window_get_display(VIRT_VIEWER_WINDOW(l->data)) == NULL)
                break;
        }
        if (l && virt_viewer_window_get_display(VIRT_VIEWER_WINDOW(l->data)) == NULL) {
            win = VIRT_VIEWER_WINDOW(l->data);
            g_debug("Found a window without a display, reusing for display #%d", nth);
            if (priv->fullscreen && !priv->kiosk)
                app_window_try_fullscreen(self, win, nth);
        } else {
            win = virt_viewer_app_window_new(self, nth);
        }

        virt_viewer_app_set_display_auto_resize(self, display);
        virt_viewer_window_set_display(win, display);
        if (VIRT_VIEWER_IS_DISPLAY_VTE(display)) {
            g_object_set_data(G_OBJECT(display), "virt-viewer-window", win);
            g_object_weak_ref(G_OBJECT(win), window_weak_notify, display);
        }

        virt_viewer_window_set_actions_sensitive(win, priv->connected);
    }
    virt_viewer_app_set_window_subtitle(self, win, nth);

    return win;
}

static VirtViewerWindow *
display_show_notebook_get_window(VirtViewerApp *self, VirtViewerDisplay *display)
{
    VirtViewerWindow *win = ensure_window_for_display(self, display);
    VirtViewerNotebook *nb = virt_viewer_window_get_notebook(win);

    virt_viewer_notebook_show_display(nb);
    return win;
}

static void
display_show_hint(VirtViewerDisplay *display,
                  GParamSpec *pspec G_GNUC_UNUSED,
                  gpointer user_data G_GNUC_UNUSED)
{
    VirtViewerApp *self = virt_viewer_session_get_app(virt_viewer_display_get_session(display));
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    VirtViewerNotebook *nb;
    VirtViewerWindow *win;
    gint nth;
    guint hint;

    g_object_get(display,
                 "nth-display", &nth,
                 "show-hint", &hint,
                 NULL);

    win = virt_viewer_app_get_nth_window(self, nth);

    if (priv->fullscreen &&
        nth >= get_n_client_monitors()) {
        if (win)
            virt_viewer_window_hide(win);
    } else if (hint & VIRT_VIEWER_DISPLAY_SHOW_HINT_DISABLED) {
        if (win)
            virt_viewer_window_hide(win);
    } else {
        if (hint & VIRT_VIEWER_DISPLAY_SHOW_HINT_READY) {
            win = display_show_notebook_get_window(self, display);
            virt_viewer_window_show(win);
        } else {
            if (!priv->kiosk && win) {
                nb = virt_viewer_window_get_notebook(win);
                virt_viewer_notebook_show_status(nb, _("Waiting for display %d..."), nth + 1);
            }
        }
    }
    virt_viewer_app_update_menu_displays(self);
}

static void
virt_viewer_app_display_added(VirtViewerSession *session G_GNUC_UNUSED,
                              VirtViewerDisplay *display,
                              VirtViewerApp *self)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    gint nth;

    g_object_get(display, "nth-display", &nth, NULL);
    g_debug("Insert display %d %p", nth, display);

    if (VIRT_VIEWER_IS_DISPLAY_VTE(display)) {
        VirtViewerWindow *win = display_show_notebook_get_window(self, display);
        virt_viewer_window_hide(win);
        virt_viewer_app_update_menu_displays(self);
        return;
    }

    g_hash_table_insert(priv->displays, GINT_TO_POINTER(nth), g_object_ref(display));
    virt_viewer_app_set_display_auto_resize(self, display);

    g_signal_connect(display, "notify::show-hint",
                     G_CALLBACK(display_show_hint), NULL);
    g_object_notify(G_OBJECT(display), "show-hint"); /* call display_show_hint */
}

static void virt_viewer_app_remove_nth_window(VirtViewerApp *self,
                                              gint nth)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    VirtViewerWindow *win = virt_viewer_app_get_nth_window(self, nth);
    if (!win)
        return;
    virt_viewer_window_set_display(win, NULL);
    if (win == priv->main_window) {
        g_debug("Not removing main window %d %p", nth, win);
        return;
    }
    virt_viewer_window_hide(win);

    g_debug("Remove window %d %p", nth, win);
    priv->windows = g_list_remove(priv->windows, win);

    g_object_unref(win);
}

static void
virt_viewer_app_display_removed(VirtViewerSession *session G_GNUC_UNUSED,
                                VirtViewerDisplay *display,
                                VirtViewerApp *self)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    gint nth;

    if(virt_viewer_display_get_fullscreen(display))
        virt_viewer_app_set_monitor_mapping_for_display(self, display) ;

    g_object_get(display, "nth-display", &nth, NULL);
    virt_viewer_app_remove_nth_window(self, nth);
    g_hash_table_remove(priv->displays, GINT_TO_POINTER(nth));
    virt_viewer_app_update_menu_displays(self);
}

static void
virt_viewer_app_display_updated(VirtViewerSession *session G_GNUC_UNUSED,
                                VirtViewerApp *self)
{
    virt_viewer_app_update_menu_displays(self);
}

static void
virt_viewer_app_has_usbredir_updated(VirtViewerSession *session,
                                     GParamSpec *pspec G_GNUC_UNUSED,
                                     VirtViewerApp *self)
{
    gboolean has_usbredir = virt_viewer_session_get_has_usbredir(session);

    virt_viewer_app_set_usb_options_sensitive(self, has_usbredir);
    virt_viewer_app_set_usb_reset_sensitive(self, has_usbredir);
    virt_viewer_update_usbredir_accels(self);
}

static void notify_software_reader_cb(GObject    *gobject G_GNUC_UNUSED,
                                      GParamSpec *pspec G_GNUC_UNUSED,
                                      gpointer    user_data)
{
    virt_viewer_update_smartcard_accels(VIRT_VIEWER_APP(user_data));
}

gboolean
virt_viewer_app_create_session(VirtViewerApp *self, const gchar *type, GError **error)
{
    g_return_val_if_fail(VIRT_VIEWER_IS_APP(self), FALSE);
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    g_return_val_if_fail(priv->session == NULL, FALSE);
    g_return_val_if_fail(type != NULL, FALSE);

#ifdef HAVE_GTK_VNC
    if (g_ascii_strcasecmp(type, "vnc") == 0) {
        GtkWindow *window = virt_viewer_window_get_window(priv->main_window);
        virt_viewer_app_trace(self, "Guest %s has a %s display",
                              priv->guest_name, type);
        priv->session = virt_viewer_session_vnc_new(self, window);
    } else
#endif
#ifdef HAVE_SPICE_GTK
    if (g_ascii_strcasecmp(type, "spice") == 0) {
        GtkWindow *window = virt_viewer_window_get_window(priv->main_window);
        virt_viewer_app_trace(self, "Guest %s has a %s display",
                              priv->guest_name, type);
        priv->session = virt_viewer_session_spice_new(self, window);
    } else
#endif
    {
        g_set_error(error,
                    VIRT_VIEWER_ERROR, VIRT_VIEWER_ERROR_FAILED,
                    _("Unsupported graphic type '%s'"), type);

        virt_viewer_app_trace(self, "Guest %s has unsupported %s display type",
                              priv->guest_name, type);
        return FALSE;
    }

    g_signal_connect(priv->session, "session-initialized",
                     G_CALLBACK(virt_viewer_app_initialized), self);
    g_signal_connect(priv->session, "session-connected",
                     G_CALLBACK(virt_viewer_app_connected), self);
    g_signal_connect(priv->session, "session-disconnected",
                     G_CALLBACK(virt_viewer_app_disconnected), self);
    g_signal_connect(priv->session, "session-channel-open",
                     G_CALLBACK(virt_viewer_app_channel_open), self);
    g_signal_connect(priv->session, "session-auth-refused",
                     G_CALLBACK(virt_viewer_app_auth_refused), self);
    g_signal_connect(priv->session, "session-auth-unsupported",
                     G_CALLBACK(virt_viewer_app_auth_unsupported), self);
    g_signal_connect(priv->session, "session-usb-failed",
                     G_CALLBACK(virt_viewer_app_usb_failed), self);
    g_signal_connect(priv->session, "session-display-added",
                     G_CALLBACK(virt_viewer_app_display_added), self);
    g_signal_connect(priv->session, "session-display-removed",
                     G_CALLBACK(virt_viewer_app_display_removed), self);
    g_signal_connect(priv->session, "session-display-updated",
                     G_CALLBACK(virt_viewer_app_display_updated), self);
    g_signal_connect(priv->session, "notify::has-usbredir",
                     G_CALLBACK(virt_viewer_app_has_usbredir_updated), self);

    g_signal_connect(priv->session, "session-cut-text",
                     G_CALLBACK(virt_viewer_app_server_cut_text), self);
    g_signal_connect(priv->session, "session-bell",
                     G_CALLBACK(virt_viewer_app_bell), self);
    g_signal_connect(priv->session, "session-cancelled",
                     G_CALLBACK(virt_viewer_app_cancelled), self);

    g_signal_connect(priv->session, "notify::software-smartcard-reader",
                     (GCallback)notify_software_reader_cb, self);
    return TRUE;
}

static gboolean
virt_viewer_app_default_open_connection(VirtViewerApp *self G_GNUC_UNUSED, int *fd)
{
    *fd = -1;
    return TRUE;
}


static int
virt_viewer_app_open_connection(VirtViewerApp *self, int *fd)
{
    VirtViewerAppClass *klass;

    g_return_val_if_fail(VIRT_VIEWER_IS_APP(self), -1);
    klass = VIRT_VIEWER_APP_GET_CLASS(self);

    return klass->open_connection(self, fd);
}


#ifndef G_OS_WIN32
static void
virt_viewer_app_channel_open(VirtViewerSession *session,
                             VirtViewerSessionChannel *channel,
                             VirtViewerApp *self)
{
    VirtViewerAppPrivate *priv;
    int fd = -1;
    gchar *error_message = NULL;

    g_return_if_fail(self != NULL);

    if (!virt_viewer_app_open_connection(self, &fd))
        return;

    g_debug("After open connection callback fd=%d", fd);

    priv = virt_viewer_app_get_instance_private(self);
    if (priv->transport && g_ascii_strcasecmp(priv->transport, "ssh") == 0 &&
        !priv->direct && fd == -1) {
        if ((fd = virt_viewer_app_open_tunnel_ssh(priv->host, priv->port, priv->user,
                                                  priv->ghost, priv->gport, priv->unixsock)) < 0) {
            error_message = g_strdup(_("Connect to SSH failed."));
            g_debug("channel open ssh tunnel: %s", error_message);
        }
    }
    if (fd < 0 && priv->unixsock) {
        GError *error = NULL;
        if ((fd = virt_viewer_app_open_unix_sock(priv->unixsock, &error)) < 0) {
            g_free(error_message);
            error_message = g_strdup(error->message);
            g_debug("channel open unix socket: %s", error_message);
        }
        g_clear_error(&error);
    }

    if (fd < 0) {
        virt_viewer_app_simple_message_dialog(self, _("Can't connect to channel: %s"),
                                              (error_message != NULL) ? error_message :
                                              _("only SSH or UNIX socket connection supported."));
        g_free(error_message);
        return;
    }

    if (!virt_viewer_session_channel_open_fd(session, channel, fd)) {
        // in case of error, close the file descriptor to prevent a leak
        // NOTE: as VNC doesn't support channel_open, this function will always return false for this protocol.
        close(fd);
    }
}
#else
static void
virt_viewer_app_channel_open(VirtViewerSession *session G_GNUC_UNUSED,
                             VirtViewerSessionChannel *channel G_GNUC_UNUSED,
                             VirtViewerApp *self)
{
    virt_viewer_app_simple_message_dialog(self, _("Connect to channel unsupported."));
}
#endif

static gboolean
virt_viewer_app_default_activate(VirtViewerApp *self, GError **error)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    int fd = -1;

    if (!virt_viewer_app_open_connection(self, &fd))
        return FALSE;

    g_debug("After open connection callback fd=%d", fd);

#ifndef G_OS_WIN32
    if (priv->transport &&
        g_ascii_strcasecmp(priv->transport, "ssh") == 0 &&
        !priv->direct &&
        fd == -1) {
        gchar *p = NULL;

        if (priv->gport) {
            virt_viewer_app_trace(self, "Opening indirect TCP connection to display at %s:%s",
                                  priv->ghost, priv->gport);
        } else {
            virt_viewer_app_trace(self, "Opening indirect UNIX connection to display at %s",
                                  priv->unixsock);
        }
        if (priv->port)
            p = g_strdup_printf(":%d", priv->port);

        virt_viewer_app_trace(self, "Setting up SSH tunnel via %s%s%s%s",
                              priv->user ? priv->user : "",
                              priv->user ? "@" : "",
                              priv->host, p ? p : "");
        g_free(p);

        if ((fd = virt_viewer_app_open_tunnel_ssh(priv->host, priv->port,
                                                  priv->user, priv->ghost,
                                                  priv->gport, priv->unixsock)) < 0)
            return FALSE;
    } else if (priv->unixsock && fd == -1) {
        virt_viewer_app_trace(self, "Opening direct UNIX connection to display at %s",
                              priv->unixsock);
        if ((fd = virt_viewer_app_open_unix_sock(priv->unixsock, error)) < 0)
            return FALSE;
    }
#endif

    if (fd >= 0) {
        gboolean ret = virt_viewer_session_open_fd(VIRT_VIEWER_SESSION(priv->session), fd);
        if (!ret)
            close (fd);
        return ret ;
    } else if (priv->guri) {
        virt_viewer_app_trace(self, "Opening connection to display at %s", priv->guri);
        return virt_viewer_session_open_uri(VIRT_VIEWER_SESSION(priv->session), priv->guri, error);
    } else if (priv->ghost) {
        virt_viewer_app_trace(self, "Opening direct TCP connection to display at %s:%s:%s",
                              priv->ghost, priv->gport, priv->gtlsport ? priv->gtlsport : "-1");
        return virt_viewer_session_open_host(VIRT_VIEWER_SESSION(priv->session),
                                             priv->ghost, priv->gport, priv->gtlsport);
    } else {
        g_set_error_literal(error, VIRT_VIEWER_ERROR, VIRT_VIEWER_ERROR_FAILED,
                            _("Display can only be attached through libvirt with --attach"));
   }

    return FALSE;
}

gboolean
virt_viewer_app_activate(VirtViewerApp *self, GError **error)
{
    VirtViewerAppPrivate *priv;
    gboolean ret;

    g_return_val_if_fail(VIRT_VIEWER_IS_APP(self), FALSE);

    priv = virt_viewer_app_get_instance_private(self);
    if (priv->active)
        return FALSE;

    ret = VIRT_VIEWER_APP_GET_CLASS(self)->activate(self, error);

    if (ret == FALSE) {
        if(error != NULL && *error != NULL)
            virt_viewer_app_show_status(self, "%s", (*error)->message);
        priv->connected = FALSE;
    } else {
        virt_viewer_app_show_status(self, _("Connecting to graphic server"));
        priv->cancelled = FALSE;
        priv->active = TRUE;
    }

    priv->grabbed = FALSE;
    virt_viewer_app_update_title(self);

    return ret;
}

/* text was actually requested */
static void
virt_viewer_app_clipboard_copy(GtkClipboard *clipboard G_GNUC_UNUSED,
                               GtkSelectionData *data,
                               guint info G_GNUC_UNUSED,
                               VirtViewerApp *self)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);

    gtk_selection_data_set_text(data, priv->clipboard, -1);
}

static void
virt_viewer_app_server_cut_text(VirtViewerSession *session G_GNUC_UNUSED,
                                const gchar *text,
                                VirtViewerApp *self)
{
    GtkClipboard *cb;
    gsize a, b;
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    GtkTargetEntry targets[] = {
        {g_strdup("UTF8_STRING"), 0, 0},
        {g_strdup("COMPOUND_TEXT"), 0, 0},
        {g_strdup("TEXT"), 0, 0},
        {g_strdup("STRING"), 0, 0},
    };

    if (!text)
        return;

    g_free (priv->clipboard);
    priv->clipboard = g_convert (text, -1, "utf-8", "iso8859-1", &a, &b, NULL);

    if (priv->clipboard) {
        cb = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);

        gtk_clipboard_set_with_owner (cb,
                                      targets,
                                      G_N_ELEMENTS(targets),
                                      (GtkClipboardGetFunc)virt_viewer_app_clipboard_copy,
                                      NULL,
                                      G_OBJECT (self));
    }
}


static void virt_viewer_app_bell(VirtViewerSession *session G_GNUC_UNUSED,
                                 VirtViewerApp *self)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);

    gdk_window_beep(gtk_widget_get_window(GTK_WIDGET(virt_viewer_window_get_window(priv->main_window))));
}


static gboolean
virt_viewer_app_default_initial_connect(VirtViewerApp *self, GError **error)
{
    return virt_viewer_app_activate(self, error);
}

gboolean
virt_viewer_app_initial_connect(VirtViewerApp *self, GError **error)
{
    VirtViewerAppClass *klass;

    g_return_val_if_fail(VIRT_VIEWER_IS_APP(self), FALSE);
    klass = VIRT_VIEWER_APP_GET_CLASS(self);

    return klass->initial_connect(self, error);
}

static gboolean
virt_viewer_app_retryauth(gpointer opaque)
{
    VirtViewerApp *self = opaque;

    virt_viewer_app_initial_connect(self, NULL);

    return FALSE;
}

static void
virt_viewer_app_default_deactivated(VirtViewerApp *self, gboolean connect_error)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);

    if (!connect_error) {
        virt_viewer_app_show_status(self, _("Guest domain has shutdown"));
        virt_viewer_app_trace(self, "Guest %s display has disconnected, shutting down",
                              priv->guest_name);
    }

    if (priv->quit_on_disconnect)
        g_application_quit(G_APPLICATION(self));
}

static void
virt_viewer_app_deactivated(VirtViewerApp *self, gboolean connect_error)
{
    VirtViewerAppClass *klass;
    klass = VIRT_VIEWER_APP_GET_CLASS(self);

    klass->deactivated(self, connect_error);
}

static void
virt_viewer_app_deactivate(VirtViewerApp *self, gboolean connect_error)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);

    if (!priv->active)
        return;

    if (priv->session) {
        virt_viewer_session_close(VIRT_VIEWER_SESSION(priv->session));
    }

    priv->initialized = FALSE;
    priv->connected = FALSE;
    priv->active = FALSE;
    priv->started = FALSE;
#if 0
    g_free(priv->pretty_address);
    priv->pretty_address = NULL;
#endif
    priv->grabbed = FALSE;
    virt_viewer_app_update_title(self);
    virt_viewer_app_set_actions_sensitive(self);

    if (priv->authretry) {
        priv->authretry = FALSE;
        g_idle_add(virt_viewer_app_retryauth, self);
    } else {
        g_clear_object(&priv->session);
        virt_viewer_app_deactivated(self, connect_error);
    }

}

static void
virt_viewer_app_connected(VirtViewerSession *session G_GNUC_UNUSED,
                          VirtViewerApp *self)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);

    priv->connected = TRUE;

    if (priv->kiosk)
        virt_viewer_app_show_status(self, "%s", "");
    else
        virt_viewer_app_show_status(self, _("Connected to graphic server"));

    virt_viewer_app_set_actions_sensitive(self);
}

static void
virt_viewer_app_initialized(VirtViewerSession *session G_GNUC_UNUSED,
                            VirtViewerApp *self)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    priv->initialized = TRUE;
    virt_viewer_app_update_title(self);
}

static void
virt_viewer_app_disconnected(VirtViewerSession *session G_GNUC_UNUSED, const gchar *msg,
                             VirtViewerApp *self)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    gboolean connect_error = !priv->cancelled && !priv->connected;
#ifdef HAVE_GTK_VNC
    if (VIRT_VIEWER_IS_SESSION_VNC(priv->session)) {
        connect_error = !priv->cancelled && !priv->initialized;
    }
#endif

    if (!priv->kiosk)
        virt_viewer_app_hide_all_windows(self);
    else if (priv->cancelled)
        priv->authretry = TRUE;

    if (priv->quitting)
        g_application_quit(G_APPLICATION(self));

    if (connect_error) {
        GtkWidget *dialog = virt_viewer_app_make_message_dialog(self,
            _("Unable to connect to the graphic server %s"), priv->pretty_address);

        g_object_set(dialog, "secondary-text", msg, NULL);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
    virt_viewer_app_set_usb_options_sensitive(self, FALSE);
    virt_viewer_app_set_usb_reset_sensitive(self, FALSE);
    virt_viewer_app_deactivate(self, connect_error);
}

static void virt_viewer_app_cancelled(VirtViewerSession *session,
                                      VirtViewerApp *self)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    priv->cancelled = TRUE;
    virt_viewer_app_disconnected(session, NULL, self);
}

static void virt_viewer_app_auth_refused(VirtViewerSession *session,
                                         const char *msg,
                                         VirtViewerApp *self)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);

    virt_viewer_app_simple_message_dialog(self,
                                          _("Unable to authenticate with remote desktop server at %s: %s\n"),
                                          priv->pretty_address, msg);

    /* if the session implementation cannot retry auth automatically, the
     * VirtViewerApp needs to schedule a new connection to retry */
    priv->authretry = (!virt_viewer_session_can_retry_auth(session) &&
                       !virt_viewer_session_get_file(session));

    /* don't display another dialog in virt_viewer_app_disconnected when using VNC */
    priv->initialized = TRUE;
}

static void virt_viewer_app_auth_unsupported(VirtViewerSession *session G_GNUC_UNUSED,
                                        const char *msg,
                                        VirtViewerApp *self)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    virt_viewer_app_simple_message_dialog(self,
                                          _("Unable to authenticate with remote desktop server: %s"),
                                          msg);

    /* don't display another dialog in virt_viewer_app_disconnected when using VNC */
    priv->initialized = TRUE;
}

static void virt_viewer_app_usb_failed(VirtViewerSession *session G_GNUC_UNUSED,
                                       const gchar *msg,
                                       VirtViewerApp *self)
{
    virt_viewer_app_simple_message_dialog(self, _("USB redirection error: %s"), msg);
}

static void
virt_viewer_app_set_kiosk(VirtViewerApp *self, gboolean enabled)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    int i;
    GList *l;

    priv->kiosk = enabled;
    if (!enabled)
        return;

    virt_viewer_app_set_fullscreen(self, enabled);

    /* create windows for each client monitor */
    for (i = g_list_length(priv->windows);
         i < get_n_client_monitors(); i++) {
        virt_viewer_app_window_new(self, i);
    }

    for (l = priv->windows; l != NULL; l = l ->next) {
        VirtViewerWindow *win = l->data;

        virt_viewer_window_show(win);
        virt_viewer_window_set_kiosk(win, enabled);
    }
}


static void
virt_viewer_app_get_property (GObject *object, guint property_id,
                              GValue *value G_GNUC_UNUSED, GParamSpec *pspec)
{
    g_return_if_fail(VIRT_VIEWER_IS_APP(object));
    VirtViewerApp *self = VIRT_VIEWER_APP(object);
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);

    switch (property_id) {
    case PROP_VERBOSE:
        g_value_set_boolean(value, priv->verbose);
        break;

    case PROP_SESSION:
        g_value_set_object(value, priv->session);
        break;

    case PROP_GUEST_NAME:
        g_value_set_string(value, priv->guest_name);
        break;

    case PROP_GURI:
        g_value_set_string(value, priv->guri);
        break;

    case PROP_FULLSCREEN:
        g_value_set_boolean(value, priv->fullscreen);
        break;

    case PROP_TITLE:
        g_value_set_string(value, virt_viewer_app_get_title(self));
        break;

    case PROP_RELEASE_CURSOR_DISPLAY_HOTKEY:
        g_value_set_string(value, virt_viewer_app_get_release_cursor_display_hotkey(self));
        break;

    case PROP_KIOSK:
        g_value_set_boolean(value, priv->kiosk);
        break;

    case PROP_QUIT_ON_DISCONNECT:
        g_value_set_boolean(value, priv->quit_on_disconnect);
        break;

    case PROP_UUID:
        g_value_set_string(value, priv->uuid);
        break;

    case PROP_VM_UI:
        g_value_set_boolean(value, priv->vm_ui);
        break;

    case PROP_VM_RUNNING:
        g_value_set_boolean(value, priv->vm_running);
        break;

    case PROP_CONFIG_SHARE_CLIPBOARD:
        g_value_set_boolean(value, virt_viewer_app_get_config_share_clipboard(self));
        break;

    case PROP_SUPPORTS_SHARE_CLIPBOARD:
        g_value_set_boolean(value, virt_viewer_app_get_supports_share_clipboard(self));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
virt_viewer_app_set_property (GObject *object, guint property_id,
                              const GValue *value G_GNUC_UNUSED, GParamSpec *pspec)
{
    g_return_if_fail(VIRT_VIEWER_IS_APP(object));
    VirtViewerApp *self = VIRT_VIEWER_APP(object);
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    GAction *action;

    switch (property_id) {
    case PROP_VERBOSE:
        priv->verbose = g_value_get_boolean(value);
        break;

    case PROP_GUEST_NAME:
        g_free(priv->guest_name);
        priv->guest_name = g_value_dup_string(value);
        break;

    case PROP_GURI:
        g_free(priv->guri);
        priv->guri = g_value_dup_string(value);
        virt_viewer_app_update_pretty_address(self);
        break;

    case PROP_FULLSCREEN:
        virt_viewer_app_set_fullscreen(self, g_value_get_boolean(value));
        break;

    case PROP_TITLE:
        g_free(priv->title);
        priv->title = g_value_dup_string(value);
        break;

    case PROP_RELEASE_CURSOR_DISPLAY_HOTKEY:
        virt_viewer_app_set_release_cursor_display_hotkey(self, g_value_dup_string(value));
        break;

    case PROP_KIOSK:
        virt_viewer_app_set_kiosk(self, g_value_get_boolean(value));
        break;

    case PROP_QUIT_ON_DISCONNECT:
        priv->quit_on_disconnect = g_value_get_boolean(value);
        break;

    case PROP_UUID:
        virt_viewer_app_set_uuid_string(self, g_value_get_string(value));
        break;

    case PROP_VM_UI:
        priv->vm_ui = g_value_get_boolean(value);

        virt_viewer_app_set_actions_sensitive(self);
        break;

    case PROP_VM_RUNNING:
        priv->vm_running = g_value_get_boolean(value);

        action = g_action_map_lookup_action(G_ACTION_MAP(self), "machine-pause");
        g_simple_action_set_state(G_SIMPLE_ACTION(action),
                                  g_variant_new_boolean(priv->vm_running));
        break;

    case PROP_CONFIG_SHARE_CLIPBOARD:
        virt_viewer_app_set_config_share_clipboard(self, g_value_get_boolean(value));
        break;

    case PROP_SUPPORTS_SHARE_CLIPBOARD:
        virt_viewer_app_set_supports_share_clipboard(self, g_value_get_boolean(value));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
virt_viewer_app_dispose (GObject *object)
{
    VirtViewerApp *self = VIRT_VIEWER_APP(object);
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);

    if (priv->preferences)
        gtk_widget_destroy(priv->preferences);
    priv->preferences = NULL;

    if (priv->windows) {
        GList *tmp = priv->windows;
        /* null-ify before unrefing, because we need
         * to prevent callbacks using priv->windows
         * while it is being disposed off. */
        priv->windows = NULL;
        priv->main_window = NULL;
        g_list_free_full(tmp, g_object_unref);
    }

    if (priv->displays) {
        GHashTable *tmp = priv->displays;
        /* null-ify before unrefing, because we need
         * to prevent callbacks using priv->displays
         * while it is being disposed of. */
        priv->displays = NULL;
        g_hash_table_unref(tmp);
    }

    priv->resource = NULL;
    g_clear_object(&priv->session);
    g_free(priv->title);
    priv->title = NULL;
    g_free(priv->guest_name);
    priv->guest_name = NULL;
    g_free(priv->pretty_address);
    priv->pretty_address = NULL;
    g_free(priv->guri);
    priv->guri = NULL;
    g_free(priv->title);
    priv->title = NULL;
    g_free(priv->uuid);
    priv->uuid = NULL;
    g_free(priv->config_file);
    priv->config_file = NULL;
    g_free(priv->release_cursor_display_hotkey);
    priv->release_cursor_display_hotkey = NULL;
    g_strfreev(priv->insert_smartcard_accel);
    priv->insert_smartcard_accel = NULL;
    g_strfreev(priv->remove_smartcard_accel);
    priv->remove_smartcard_accel = NULL;
    g_strfreev(priv->usb_device_reset_accel);
    priv->usb_device_reset_accel = NULL;
    g_clear_pointer(&priv->config, g_key_file_free);
    g_clear_pointer(&priv->initial_display_map, g_hash_table_unref);

    virt_viewer_app_free_connect_info(self);

    G_OBJECT_CLASS (virt_viewer_app_parent_class)->dispose (object);
}

static gboolean
virt_viewer_app_default_start(VirtViewerApp *self, GError **error G_GNUC_UNUSED)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    virt_viewer_window_show(priv->main_window);
    return TRUE;
}

gboolean virt_viewer_app_start(VirtViewerApp *self, GError **error)
{
    VirtViewerAppClass *klass;

    g_return_val_if_fail(VIRT_VIEWER_IS_APP(self), FALSE);
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    klass = VIRT_VIEWER_APP_GET_CLASS(self);

    g_return_val_if_fail(!priv->started, TRUE);

    priv->started = klass->start(self, error);
    return priv->started;
}

static int opt_zoom = NORMAL_ZOOM_LEVEL;
static gchar *opt_hotkeys = NULL;
static gchar *opt_keymap = NULL;
static gboolean opt_version = FALSE;
static gboolean opt_verbose = FALSE;
static gboolean opt_debug = FALSE;
static gboolean opt_fullscreen = FALSE;
static gboolean opt_kiosk = FALSE;
static gboolean opt_kiosk_quit = FALSE;
static gchar *opt_cursor = NULL;
static gchar *opt_resize = NULL;

#ifndef G_OS_WIN32
static gboolean
sigint_cb(gpointer data)
{
    VirtViewerApp *self = VIRT_VIEWER_APP(data);
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);

    g_debug("got SIGINT, quitting\n");
    if (priv->started)
        virt_viewer_app_quit(self);
    else
        exit(EXIT_SUCCESS);

    return priv->quitting ? G_SOURCE_REMOVE : G_SOURCE_CONTINUE;
}
#endif

static void
title_maybe_changed(VirtViewerApp *self, GParamSpec* pspec G_GNUC_UNUSED, gpointer user_data G_GNUC_UNUSED)
{
    virt_viewer_app_set_all_window_subtitles(self);
}

static void
virt_viewer_update_smartcard_accels(VirtViewerApp *self)
{
    gboolean sw_smartcard;
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);

    if (priv->session != NULL) {
        g_object_get(G_OBJECT(priv->session),
                     "software-smartcard-reader", &sw_smartcard,
                     NULL);
    } else {
        sw_smartcard = FALSE;
    }
    if (sw_smartcard) {
        g_debug("enabling smartcard shortcuts");
        if (priv->insert_smartcard_accel)
            gtk_application_set_accels_for_action(GTK_APPLICATION(self), "app.smartcard-insert",
                                                  (const gchar * const *)priv->insert_smartcard_accel);
        if (priv->remove_smartcard_accel)
            gtk_application_set_accels_for_action(GTK_APPLICATION(self), "app.smartcard-remove",
                                                  (const gchar * const *)priv->remove_smartcard_accel);
    } else {
        g_debug("disabling smartcard shortcuts");
        const gchar *no_accels[] = { NULL };
        gtk_application_set_accels_for_action(GTK_APPLICATION(self), "app.smartcard-insert", no_accels);
        gtk_application_set_accels_for_action(GTK_APPLICATION(self), "app.smartcard-remove", no_accels);
    }
}

static void
virt_viewer_update_usbredir_accels(VirtViewerApp *self)
{
    gboolean has_usbredir;
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);

    if (priv->session != NULL) {
        g_object_get(G_OBJECT(priv->session),
                     "has-usbredir", &has_usbredir, NULL);
    } else {
        has_usbredir = FALSE;
    }

    if (has_usbredir) {
        if (priv->usb_device_reset_accel)
            gtk_application_set_accels_for_action(GTK_APPLICATION(self), "win.usb-device-reset",
                                                  (const gchar * const *)priv->usb_device_reset_accel);
    } else {
        const gchar *no_accels[] = { NULL };
        gtk_application_set_accels_for_action(GTK_APPLICATION(self), "win.usb-device-reset", no_accels);
    }
}

static void
virt_viewer_app_action_machine_reset(GSimpleAction *act G_GNUC_UNUSED,
                                     GVariant *param G_GNUC_UNUSED,
                                     gpointer opaque)
{
    g_return_if_fail(VIRT_VIEWER_IS_APP(opaque));

    VirtViewerApp *self = VIRT_VIEWER_APP(opaque);

    virt_viewer_session_vm_action(virt_viewer_app_get_session(self),
                                  VIRT_VIEWER_SESSION_VM_ACTION_RESET);
}

static void
virt_viewer_app_action_machine_powerdown(GSimpleAction *act G_GNUC_UNUSED,
                                         GVariant *param G_GNUC_UNUSED,
                                         gpointer opaque)
{
    g_return_if_fail(VIRT_VIEWER_IS_APP(opaque));

    VirtViewerApp *self = VIRT_VIEWER_APP(opaque);

    virt_viewer_session_vm_action(virt_viewer_app_get_session(self),
                                  VIRT_VIEWER_SESSION_VM_ACTION_POWER_DOWN);
}

static void
virt_viewer_app_action_machine_pause(GSimpleAction *act,
                                     GVariant *state,
                                     gpointer opaque)
{
    g_return_if_fail(VIRT_VIEWER_IS_APP(opaque));

    VirtViewerApp *self = VIRT_VIEWER_APP(opaque);
    gboolean paused = g_variant_get_boolean(state);
    gint action;

    g_simple_action_set_state(act, g_variant_new_boolean(paused));

    if (paused)
        action = VIRT_VIEWER_SESSION_VM_ACTION_PAUSE;
    else
        action = VIRT_VIEWER_SESSION_VM_ACTION_CONTINUE;

    virt_viewer_session_vm_action(virt_viewer_app_get_session(self), action);
}


static void
virt_viewer_app_action_smartcard_insert(GSimpleAction *act G_GNUC_UNUSED,
                                        GVariant *param G_GNUC_UNUSED,
                                        gpointer opaque)
{
    g_return_if_fail(VIRT_VIEWER_IS_APP(opaque));

    VirtViewerApp *self = VIRT_VIEWER_APP(opaque);

    virt_viewer_session_smartcard_insert(virt_viewer_app_get_session(self));
}

static void
virt_viewer_app_action_smartcard_remove(GSimpleAction *act G_GNUC_UNUSED,
                                        GVariant *param G_GNUC_UNUSED,
                                        gpointer opaque)
{
    g_return_if_fail(VIRT_VIEWER_IS_APP(opaque));

    VirtViewerApp *self = VIRT_VIEWER_APP(opaque);

    virt_viewer_session_smartcard_remove(virt_viewer_app_get_session(self));
}


static void
virt_viewer_app_set_display_auto_resize(VirtViewerApp *self,
                                        VirtViewerDisplay *display)
{
    GAction *action = g_action_map_lookup_action(G_ACTION_MAP(self), "auto-resize");
    GVariant *state = g_action_get_state(action);
    gboolean resize = g_variant_get_boolean(state);
    virt_viewer_display_set_auto_resize(display, resize);
}

static void
set_window_autoresize(gpointer value, gpointer user_data)
{
    VirtViewerApp *self = VIRT_VIEWER_APP(user_data);
    VirtViewerDisplay *display = virt_viewer_window_get_display(VIRT_VIEWER_WINDOW(value));
    virt_viewer_app_set_display_auto_resize(self, display);
}

static void
virt_viewer_app_action_auto_resize(GSimpleAction *act G_GNUC_UNUSED,
                                   GVariant *state,
                                   gpointer opaque)
{
    g_return_if_fail(VIRT_VIEWER_IS_APP(opaque));

    VirtViewerApp *self = VIRT_VIEWER_APP(opaque);
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    gboolean resize = g_variant_get_boolean(state);

    g_simple_action_set_state(act, g_variant_new_boolean(resize));

    g_list_foreach(priv->windows, set_window_autoresize, self);
}

static void
virt_viewer_app_action_window(VirtViewerApp *self,
                              VirtViewerWindow *win,
                              GSimpleAction *act,
                              GVariant *state)
{
    VirtViewerDisplay *display;
    VirtViewerAppPrivate *priv;
    gboolean visible = g_variant_get_boolean(state);

    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(win));

    priv = virt_viewer_app_get_instance_private(self);
    display = virt_viewer_window_get_display(win);

    if (visible) {
        virt_viewer_window_show(win);
    } else {
        if (virt_viewer_app_get_n_windows_visible(self) > 1) {
            virt_viewer_window_hide(win);
        } else {
            virt_viewer_app_maybe_quit(self, win);
            if (!priv->quitting) {
                /* the last item remains active, doesn't matter if we quit */
                visible = TRUE;
                g_action_change_state(G_ACTION(act),
                                      g_variant_new_boolean(visible));
            }
        }
    }

    if (!priv->quitting)
        virt_viewer_session_update_displays_geometry(virt_viewer_display_get_session(display));

    g_simple_action_set_state(act, g_variant_new_boolean(visible));
}


static void
virt_viewer_app_action_monitor(GSimpleAction *act,
                               GVariant *state,
                               gpointer opaque)
{
    VirtViewerApp *self;
    VirtViewerWindow *win;
    VirtViewerDisplay *display;
    VirtViewerAppPrivate *priv;
    int nth;

    g_return_if_fail(VIRT_VIEWER_IS_APP(opaque));
    self = VIRT_VIEWER_APP(opaque);

    priv = virt_viewer_app_get_instance_private(self);
    if (priv->quitting)
        return;

    nth = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(act), "nth"));
    display = VIRT_VIEWER_DISPLAY(g_hash_table_lookup(priv->displays, GINT_TO_POINTER(nth)));
    win = ensure_window_for_display(self, display);

    virt_viewer_app_action_window(self, win, act, state);
}

static void
virt_viewer_app_action_vte(GSimpleAction *act,
                           GVariant *state,
                           gpointer opaque)
{
    VirtViewerApp *self;
    VirtViewerWindow *win;
    VirtViewerAppPrivate *priv;
    const gchar *name;

    g_return_if_fail(VIRT_VIEWER_IS_APP(opaque));
    self = VIRT_VIEWER_APP(opaque);

    priv = virt_viewer_app_get_instance_private(self);
    if (priv->quitting)
        return;

    name = g_object_get_data(G_OBJECT(act), "vte");
    win = virt_viewer_app_get_vte_window(self, name);
    virt_viewer_app_action_window(self, win, act, state);
}

static GActionEntry actions[] = {
    { .name = "machine-reset",
      .activate = virt_viewer_app_action_machine_reset },
    { .name = "machine-powerdown",
      .activate = virt_viewer_app_action_machine_powerdown },
    { .name = "machine-pause",
      .state = "false",
      .change_state = virt_viewer_app_action_machine_pause },
    { .name = "smartcard-insert",
      .activate = virt_viewer_app_action_smartcard_insert },
    { .name = "smartcard-remove",
      .activate = virt_viewer_app_action_smartcard_remove },
    { .name = "auto-resize",
      .state = "true",
      .change_state = virt_viewer_app_action_auto_resize },
};

static const struct {
    const char *name;
    const char *action;
    const gchar *default_accels[3];
} hotkey_defaults[] = {
    { "toggle-fullscreen", "win.fullscreen", {"F11", NULL, NULL} },
    { "zoom-in", "win.zoom-in", { "<Ctrl>plus", "<Ctrl>KP_Add", NULL } },
    { "zoom-out", "win.zoom-out", { "<Ctrl>minus", "<Ctrl>KP_Subtract", NULL } },
    { "zoom-reset", "win.zoom-reset", { "<Ctrl>0", "<Ctrl>KP_0", NULL } },
    { "release-cursor", "win.release-cursor", {"<Shift>F12", NULL, NULL} },
    { "smartcard-insert", "app.smartcard-insert", {"<Shift>F8", NULL, NULL} },
    { "smartcard-remove", "app.smartcard-remove", {"<Shift>F9", NULL, NULL} },
    { "secure-attention", "win.secure-attention", {"<Ctrl><Alt>End", NULL, NULL} },
    { "usb-device-reset", "win.usb-device-reset", {"<Ctrl><Shift>r", NULL, NULL} },
};

static gchar **hotkey_names;

/* g_strdupv() will not take a const gchar** */
static gchar**
g_strdupvc(const gchar* const *str_array)
{
    if (!str_array)
        return NULL;
    gsize i = 0;
    gchar **retval;
    while (str_array[i])
        ++i;
    retval = g_new(gchar*, i + 1);
    i = 0;
    while (str_array[i]) {
        retval[i] = g_strdup(str_array[i]);
        ++i;
    }
    retval[i] = NULL;
    return retval;
}

static void
virt_viewer_app_on_application_startup(GApplication *app)
{
    VirtViewerApp *self = VIRT_VIEWER_APP(app);
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    GError *error = NULL;
    gint i;
#ifndef G_OS_WIN32
    GtkSettings *gtk_settings;
#endif

    G_APPLICATION_CLASS(virt_viewer_app_parent_class)->startup(app);

#ifndef G_OS_WIN32
    gtk_settings = gtk_settings_get_default();
    g_object_set(G_OBJECT(gtk_settings),
                 "gtk-application-prefer-dark-theme",
                 TRUE, NULL);
#endif

    priv->resource = virt_viewer_get_resource();

    virt_viewer_app_set_debug(opt_debug);
    virt_viewer_app_set_fullscreen(self, opt_fullscreen);

    virt_viewer_app_set_keymap(self, opt_keymap);

    priv->verbose = opt_verbose;
    priv->quit_on_disconnect = opt_kiosk ? opt_kiosk_quit : TRUE;

    priv->main_window = virt_viewer_app_window_new(self,
                                                   virt_viewer_app_get_first_monitor(self));
    priv->main_notebook = GTK_WIDGET(virt_viewer_window_get_notebook(priv->main_window));
    priv->initial_display_map = virt_viewer_app_get_monitor_mapping_for_section(self, "fallback");

    virt_viewer_app_set_kiosk(self, opt_kiosk);

    priv->release_cursor_display_hotkey = NULL;
    hotkey_names = g_new(gchar*, G_N_ELEMENTS(hotkey_defaults) + 1);
    for (i = 0 ; i < G_N_ELEMENTS(hotkey_defaults); i++) {
        hotkey_names[i] = g_strdup(hotkey_defaults[i].name);
        if (g_str_equal(hotkey_defaults[i].name, "smartcard-insert")) {
            priv->insert_smartcard_accel = g_strdupvc(hotkey_defaults[i].default_accels);
            continue;
        }
        if (g_str_equal(hotkey_defaults[i].name, "smartcard-remove")) {
            priv->remove_smartcard_accel = g_strdupvc(hotkey_defaults[i].default_accels);
            continue;
        }
        if (g_str_equal(hotkey_defaults[i].name, "usb-device-reset")) {
            priv->usb_device_reset_accel = g_strdupvc(hotkey_defaults[i].default_accels);
            continue;
        }
        gtk_application_set_accels_for_action(GTK_APPLICATION(app),
                                              hotkey_defaults[i].action,
                                              hotkey_defaults[i].default_accels);
    }
    hotkey_names[i] = NULL;

    virt_viewer_app_set_hotkeys(self, opt_hotkeys);

    if (opt_zoom < MIN_ZOOM_LEVEL || opt_zoom > MAX_ZOOM_LEVEL) {
        g_printerr(_("Zoom level must be within %d-%d\n"), MIN_ZOOM_LEVEL, MAX_ZOOM_LEVEL);
        opt_zoom = NORMAL_ZOOM_LEVEL;
    }

    virt_viewer_app_set_actions_sensitive(self);
    virt_viewer_window_set_zoom_level(priv->main_window, opt_zoom);

    // Restore initial state of config-share-clipboard property from config and notify about it
    virt_viewer_app_set_config_share_clipboard(self, virt_viewer_app_get_config_share_clipboard(self));

    if (!virt_viewer_app_start(self, &error)) {
        if (error && !g_error_matches(error, VIRT_VIEWER_ERROR, VIRT_VIEWER_ERROR_CANCELLED))
            virt_viewer_app_simple_message_dialog(self, "%s", error->message);

        g_clear_error(&error);
        g_application_quit(app);
        return;
    }
}

static gboolean
virt_viewer_app_local_command_line (GApplication   *gapp,
                                    gchar        ***args,
                                    int            *status)
{
    VirtViewerApp *self = VIRT_VIEWER_APP(gapp);
    gboolean ret = FALSE;
    GError *error = NULL;
    GOptionContext *context = g_option_context_new(NULL);
    GOptionGroup *group = g_option_group_new("virt-viewer", NULL, NULL, gapp, NULL);

    *status = 0;
    g_option_context_set_main_group(context, group);
    VIRT_VIEWER_APP_GET_CLASS(self)->add_option_entries(self, context, group);

    g_option_context_add_group(context, gtk_get_option_group(FALSE));

#ifdef HAVE_GTK_VNC
    g_option_context_add_group(context, vnc_display_get_option_group());
#endif

#ifdef HAVE_SPICE_GTK
    g_option_context_add_group(context, spice_get_option_group());
#endif

    if (!g_option_context_parse_strv(context, args, &error)) {
        if (error != NULL) {
            g_printerr("%s\n", error->message);
            g_error_free(error);
        }

        *status = 1;
        ret = TRUE;
        goto end;
    }

    if (opt_version) {
        g_print(_("%s version %s"), g_get_prgname(), VERSION BUILDID);
#ifdef REMOTE_VIEWER_OS_ID
        g_print(" (OS ID: %s)", REMOTE_VIEWER_OS_ID);
#endif
        g_print("\n");
        ret = TRUE;
    }

    if (opt_cursor) {
        int cursor = virt_viewer_enum_from_string(VIRT_VIEWER_TYPE_CURSOR,
                                                  opt_cursor);
        if (cursor < 0) {
            g_printerr("unknown value '%s' for --cursor\n", opt_cursor);
            *status = 1;
            ret = TRUE;
            goto end;
        }
        virt_viewer_app_set_cursor(self, cursor);
    }

    if (opt_resize) {
        GAction *resize = g_action_map_lookup_action(G_ACTION_MAP(self),
                                                    "auto-resize");
        gboolean enabled = TRUE;
        if (g_str_equal(opt_resize, "always")) {
            enabled = TRUE;
        } else if (g_str_equal(opt_resize, "never")) {
            enabled = FALSE;
        } else {
            g_printerr("--auto-resize expects 'always' or 'never'\n");
            *status = 1;
            ret = TRUE;
            goto end;
        }
        g_simple_action_set_state(G_SIMPLE_ACTION(resize),
                                  g_variant_new_boolean(enabled));
    }

end:
    g_option_context_free(context);
    return ret;
}

static void
virt_viewer_app_class_init (VirtViewerAppClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GApplicationClass *g_app_class = G_APPLICATION_CLASS(klass);

    object_class->get_property = virt_viewer_app_get_property;
    object_class->set_property = virt_viewer_app_set_property;
    object_class->dispose = virt_viewer_app_dispose;

    g_app_class->local_command_line = virt_viewer_app_local_command_line;
    g_app_class->startup = virt_viewer_app_on_application_startup;
    g_app_class->command_line = NULL; /* inhibit GApplication default handler */

    klass->start = virt_viewer_app_default_start;
    klass->initial_connect = virt_viewer_app_default_initial_connect;
    klass->activate = virt_viewer_app_default_activate;
    klass->deactivated = virt_viewer_app_default_deactivated;
    klass->open_connection = virt_viewer_app_default_open_connection;
    klass->add_option_entries = virt_viewer_app_add_option_entries;

    g_object_class_install_property(object_class,
                                    PROP_VERBOSE,
                                    g_param_spec_boolean("verbose",
                                                         "Verbose",
                                                         "Verbose trace",
                                                         FALSE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_WRITABLE |
                                                         G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(object_class,
                                    PROP_SESSION,
                                    g_param_spec_object("session",
                                                        "Session",
                                                        "ViewerSession",
                                                        VIRT_VIEWER_TYPE_SESSION,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(object_class,
                                    PROP_GUEST_NAME,
                                    g_param_spec_string("guest-name",
                                                        "Guest name",
                                                        "Guest name",
                                                        "",
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(object_class,
                                    PROP_GURI,
                                    g_param_spec_string("guri",
                                                        "guri",
                                                        "Remote graphical URI",
                                                        "",
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(object_class,
                                    PROP_FULLSCREEN,
                                    g_param_spec_boolean("fullscreen",
                                                         "Fullscreen",
                                                         "Fullscreen",
                                                         FALSE,
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(object_class,
                                    PROP_TITLE,
                                    g_param_spec_string("title",
                                                        "Title",
                                                        "Title",
                                                        "",
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(object_class,
                                    PROP_RELEASE_CURSOR_DISPLAY_HOTKEY,
                                    g_param_spec_string("release-cursor-display-hotkey",
                                                        "Release Cursor Display Hotkey",
                                                        "Display-managed hotkey to ungrab keyboard and mouse",
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(object_class,
                                    PROP_KIOSK,
                                    g_param_spec_boolean("kiosk",
                                                         "Kiosk",
                                                         "Kiosk mode",
                                                         FALSE,
                                                         G_PARAM_CONSTRUCT |
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(object_class,
                                    PROP_QUIT_ON_DISCONNECT,
                                    g_param_spec_boolean("quit-on-disconnect",
                                                         "Quit on disconnect",
                                                         "Quit on disconnect",
                                                         TRUE,
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(object_class,
                                    PROP_UUID,
                                    g_param_spec_string("uuid",
                                                        "uuid",
                                                        "uuid",
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(object_class,
                                    PROP_VM_UI,
                                    g_param_spec_boolean("vm-ui",
                                                         "VM UI",
                                                         "QEMU UI & behaviour",
                                                         FALSE,
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(object_class,
                                    PROP_VM_RUNNING,
                                    g_param_spec_boolean("vm-running",
                                                         "VM running",
                                                         "VM running",
                                                         FALSE,
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(object_class,
                                    PROP_CONFIG_SHARE_CLIPBOARD,
                                    g_param_spec_boolean("config-share-clipboard",
                                                         "Share clipboard",
                                                         "Indicates whether to share clipboard",
                                                         TRUE, /* backwards-compatible default value */
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(object_class,
                                    PROP_SUPPORTS_SHARE_CLIPBOARD,
                                    g_param_spec_boolean("supports-share-clipboard",
                                                         "Support for share clipboard",
                                                         "Indicates whether to support for clipboard sharing is available",
                                                         FALSE,
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_STRINGS));
}

static void
virt_viewer_app_init(VirtViewerApp *self)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    GError *error = NULL;
    priv = virt_viewer_app_get_instance_private(self);

    gtk_window_set_default_icon_name("virt-viewer");

#ifndef G_OS_WIN32
    g_unix_signal_add (SIGINT, sigint_cb, self);
#endif

    priv->displays = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_object_unref);
    priv->config = g_key_file_new();
    priv->config_file = g_build_filename(g_get_user_config_dir(),
                                               "virt-viewer", "settings", NULL);
    g_key_file_load_from_file(priv->config, priv->config_file,
                    G_KEY_FILE_KEEP_COMMENTS|G_KEY_FILE_KEEP_TRANSLATIONS, &error);

    if (g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        g_debug("No configuration file %s", priv->config_file);
    else if (error)
        g_warning("Couldn't load configuration: %s", error->message);

    g_clear_error(&error);

    g_signal_connect(self, "notify::guest-name", G_CALLBACK(title_maybe_changed), NULL);
    g_signal_connect(self, "notify::title", G_CALLBACK(title_maybe_changed), NULL);
    g_signal_connect(self, "notify::guri", G_CALLBACK(title_maybe_changed), NULL);

    g_action_map_add_action_entries(G_ACTION_MAP(self),
                                    actions,
                                    G_N_ELEMENTS(actions),
                                    self);
}

void
virt_viewer_app_set_direct(VirtViewerApp *self, gboolean direct)
{
    g_return_if_fail(VIRT_VIEWER_IS_APP(self));

    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    priv->direct = direct;
}

gboolean virt_viewer_app_get_direct(VirtViewerApp *self)
{
    g_return_val_if_fail(VIRT_VIEWER_IS_APP(self), FALSE);

    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    return priv->direct;
}

gchar*
virt_viewer_app_get_release_cursor_display_hotkey(VirtViewerApp *self)
{
    g_return_val_if_fail(VIRT_VIEWER_IS_APP(self), NULL);

    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    return priv->release_cursor_display_hotkey;
}

void
virt_viewer_app_set_release_cursor_display_hotkey(VirtViewerApp *self, const gchar *hotkey)
{
    g_return_if_fail(VIRT_VIEWER_IS_APP(self));

    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    g_free(priv->release_cursor_display_hotkey);
    priv->release_cursor_display_hotkey = g_strdup(hotkey);
    g_object_notify(G_OBJECT(self), "release-cursor-display-hotkey");
}

gchar**
virt_viewer_app_get_hotkey_names(void)
{
    return hotkey_names;
}

void
virt_viewer_app_clear_hotkeys(VirtViewerApp *self)
{
    gint i;
    const gchar *no_accels[] = { NULL };
    for (i = 0 ; i < G_N_ELEMENTS(hotkey_defaults); i++) {
        gtk_application_set_accels_for_action(GTK_APPLICATION(self),
                                              hotkey_defaults[i].action,
                                              no_accels);
    }

    g_return_if_fail(VIRT_VIEWER_IS_APP(self));
    virt_viewer_app_set_release_cursor_display_hotkey(self, "Control_L+Alt_L");
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    g_strfreev(priv->insert_smartcard_accel);
    priv->insert_smartcard_accel = NULL;
    g_strfreev(priv->remove_smartcard_accel);
    priv->remove_smartcard_accel = NULL;
    g_strfreev(priv->usb_device_reset_accel);
    priv->usb_device_reset_accel = NULL;
}

void
virt_viewer_app_set_hotkey(VirtViewerApp *self, const gchar *hotkey_name,
                           const gchar *hotkey)
{
    g_return_if_fail(VIRT_VIEWER_IS_APP(self));
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);

    const gchar *action = NULL;
    int i;
    for (i = 0; i < G_N_ELEMENTS(hotkey_defaults); i++) {
        if (g_str_equal(hotkey_name, hotkey_defaults[i].name)) {
            action = hotkey_defaults[i].action;
            break;
        }
    }
    if (action == NULL) {
        g_warning("Unknown hotkey name %s", hotkey_name);
        return;
    }

    gchar *accel = spice_hotkey_to_gtk_accelerator(hotkey);
    const gchar *accels[] = { accel, NULL };
    guint accel_key;
    GdkModifierType accel_mods;
    /*
     * First try the spice translated accel.
     * Works for basic modifiers and single letters/numbers
     * where forced uppercasing matches GTK key names
     */
    gtk_accelerator_parse(accels[0], &accel_key, &accel_mods);
    if (accel_key == 0 && accel_mods == 0) {
        /* Fallback to native GTK accels to cope with
         * case sensitive accels
         */
        accels[0] = hotkey;
        gtk_accelerator_parse(accels[0], &accel_key, &accel_mods);
    }
    if (g_str_equal(hotkey_name, "release-cursor")) {
        if (accel_key == 0) {
            /* GTK does not support using modifiers as hotkeys without any non-modifiers
             * (eg. CTRL+ALT), however the displays do support this for the grab sequence.
             */
            virt_viewer_app_set_release_cursor_display_hotkey(self, hotkey);
            g_free(accel);
            return;
        }
        virt_viewer_app_set_release_cursor_display_hotkey(self, NULL);
    }
    if (accel_key == 0) {
        g_warning("Invalid hotkey '%s' for '%s'", hotkey, hotkey_name);
        g_free(accel);
        return;
    }

    if (g_str_equal(hotkey_name, "smartcard-insert")) {
        g_strfreev(priv->insert_smartcard_accel);
        priv->insert_smartcard_accel = g_strdupvc(accels);
        g_free(accel);
        virt_viewer_update_smartcard_accels(self);
        return;
    }
    if (g_str_equal(hotkey_name, "smartcard-remove")) {
        g_strfreev(priv->remove_smartcard_accel);
        priv->remove_smartcard_accel = g_strdupvc(accels);
        g_free(accel);
        virt_viewer_update_smartcard_accels(self);
        return;
    }
    if (g_str_equal(hotkey_name, "usb-device-reset")) {
        g_strfreev(priv->usb_device_reset_accel);
        priv->usb_device_reset_accel = g_strdupvc(accels);
        g_free(accel);
        virt_viewer_update_usbredir_accels(self);
        return;
    }

    gtk_application_set_accels_for_action(GTK_APPLICATION(self), action, accels);
    g_free(accel);
}

void
virt_viewer_app_set_hotkeys(VirtViewerApp *self, const gchar *hotkeys_str)
{
    gchar **hotkey, **hotkeys = NULL;

    g_return_if_fail(VIRT_VIEWER_IS_APP(self));

    if (hotkeys_str)
        hotkeys = g_strsplit(hotkeys_str, ",", -1);

    if (!hotkeys || g_strv_length(hotkeys) == 0) {
        g_strfreev(hotkeys);
        return;
    }

    virt_viewer_app_clear_hotkeys(self);

    for (hotkey = hotkeys; *hotkey != NULL; hotkey++) {
        gchar *eq = strstr(*hotkey, "=");
        const gchar *value = (eq == NULL) ? NULL : (*eq = '\0', eq + 1);
        if (value == NULL || *value == '\0') {
            g_warning("Missing value for hotkey '%s'", *hotkey);
            continue;
        }

        virt_viewer_app_set_hotkey(self, *hotkey, value);
    }
    g_strfreev(hotkeys);
}

void
virt_viewer_app_set_attach(VirtViewerApp *self, gboolean attach)
{
    g_return_if_fail(VIRT_VIEWER_IS_APP(self));

    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    priv->attach = attach;
}

gboolean
virt_viewer_app_get_attach(VirtViewerApp *self)
{
    g_return_val_if_fail(VIRT_VIEWER_IS_APP(self), FALSE);

    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    return priv->attach;
}

void
virt_viewer_app_set_shared(VirtViewerApp *self, gboolean shared)
{
    g_return_if_fail(VIRT_VIEWER_IS_APP(self));

    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    priv->shared = shared;
}

gboolean
virt_viewer_app_get_shared(VirtViewerApp *self)
{
    g_return_val_if_fail(VIRT_VIEWER_IS_APP(self), FALSE);

    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    return priv->shared;
}

void virt_viewer_app_set_cursor(VirtViewerApp *self, VirtViewerCursor cursor)
{
    g_return_if_fail(VIRT_VIEWER_IS_APP(self));

    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    priv->cursor = cursor;
}

VirtViewerCursor virt_viewer_app_get_cursor(VirtViewerApp *self)
{
    g_return_val_if_fail(VIRT_VIEWER_IS_APP(self), FALSE);

    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    return priv->cursor;
}

gboolean
virt_viewer_app_is_active(VirtViewerApp *self)
{
    g_return_val_if_fail(VIRT_VIEWER_IS_APP(self), FALSE);

    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    return priv->active;
}

gboolean
virt_viewer_app_has_session(VirtViewerApp *self)
{
    g_return_val_if_fail(VIRT_VIEWER_IS_APP(self), FALSE);

    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    return priv->session != NULL;
}

static void
virt_viewer_app_update_pretty_address(VirtViewerApp *self)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    g_free(priv->pretty_address);
    priv->pretty_address = NULL;
    if (priv->guri)
        priv->pretty_address = g_strdup(priv->guri);
    else if (priv->gport)
        priv->pretty_address = g_strdup_printf("%s:%s", priv->ghost, priv->gport);
    else if (priv->host && priv->unixsock)
        priv->pretty_address = g_strdup_printf("%s:%s", priv->host, priv->unixsock);
}

typedef struct {
    VirtViewerApp *app;
    gboolean fullscreen;
} FullscreenOptions;

static void fullscreen_cb(gpointer value,
                          gpointer user_data)
{
    FullscreenOptions *options = (FullscreenOptions *)user_data;
    gint nth = 0;
    VirtViewerWindow *vwin = VIRT_VIEWER_WINDOW(value);
    VirtViewerDisplay *display = virt_viewer_window_get_display(vwin);

    /* At startup, the main window will not yet have an associated display, so
     * assume that it's the first display */
    if (display)
        nth = virt_viewer_display_get_nth(display);
    g_debug("fullscreen display %d: %d", nth, options->fullscreen);

    if (options->fullscreen)
        app_window_try_fullscreen(options->app, vwin, nth);
    else
        virt_viewer_window_leave_fullscreen(vwin);
}

gboolean
virt_viewer_app_get_fullscreen(VirtViewerApp *self)
{
    g_return_val_if_fail(VIRT_VIEWER_IS_APP(self), FALSE);

    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    return priv->fullscreen;
}

static void
virt_viewer_app_set_fullscreen(VirtViewerApp *self, gboolean fullscreen)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    FullscreenOptions options  = {
        .app = self,
        .fullscreen = fullscreen,
    };

    /* we iterate unconditionally, even if it was set before to update new windows */
    priv->fullscreen = fullscreen;
    g_list_foreach(priv->windows, fullscreen_cb, &options);

    g_object_notify(G_OBJECT(self), "fullscreen");
}


static gint
update_menu_displays_sort(gconstpointer a, gconstpointer b)
{
    const int ai = GPOINTER_TO_INT(a);
    const int bi = GPOINTER_TO_INT(b);

    if (ai > bi)
        return 1;
    else if (ai < bi)
        return -1;
    else
        return 0;
}


static void window_code_change_cb(GSimpleAction *action G_GNUC_UNUSED,
                                  GVariant *value,
                                  gpointer user_data) {
    VirtViewerDisplay *display = (VirtViewerDisplay*) user_data;
    virt_viewer_display_session_request_codec(display, g_variant_get_int32(value));
}

void window_menu_set_current_codec( VirtViewerApp *self, GVariant *value){
    GAction * action = g_action_map_lookup_action(G_ACTION_MAP(self),"video-codec");
    g_simple_action_set_state(G_SIMPLE_ACTION(action), value);
}

static void
window_update_menu_video_codecs(gpointer value,
                               gpointer user_data)
{
    VirtViewerApp *self = VIRT_VIEWER_APP(user_data);
    VirtViewerWindow *window = VIRT_VIEWER_WINDOW(value);
    GMenuModel *menu;
    menu = virt_viewer_window_get_menu(window, "header-video-settings");
    g_menu_remove_all(G_MENU(menu));
    VirtViewerDisplay *display = virt_viewer_window_get_display(VIRT_VIEWER_WINDOW(window));
    
    GArray * codecs = virt_viewer_display_session_supported_codecs(display);
    if(codecs == NULL){
        return;
    }
    GSimpleAction * action = g_simple_action_new_stateful("video-codec",
                                              G_VARIANT_TYPE_INT32,
                                              g_variant_new_int32(1));

    g_signal_connect(action, "change-state",
                         G_CALLBACK(window_code_change_cb),
                         display);
    g_action_map_add_action(G_ACTION_MAP(self), G_ACTION(action));

    for (int i= 0; i< codecs->len; i++){
        VirtViewerVideoCodec * codec = &g_array_index(codecs, VirtViewerVideoCodec, i);
        GMenuItem* item = g_menu_item_new(codec->name, NULL);
        g_menu_item_set_action_and_target(item,"app.video-codec","i",codec->id);
        g_menu_append_item(G_MENU(menu), item);
    }    
    g_array_free(codecs,TRUE);
}

static void
window_update_menu_displays_cb(gpointer value,
                               gpointer user_data)
{
    VirtViewerApp *self = VIRT_VIEWER_APP(user_data);
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    VirtViewerWindow *window = VIRT_VIEWER_WINDOW(value);
    GMenuModel *menu;
    GList *keys = g_hash_table_get_keys(priv->displays);
    GList *tmp;
    int nth;

    keys = g_list_sort(keys, update_menu_displays_sort);

    menu = virt_viewer_window_get_menu(window, "header-machine");
    g_menu_remove_all(G_MENU(menu));

    tmp = keys;
    while (tmp) {
        GMenuItem *item;
        gchar *label;
        gchar *actionname;

        nth = GPOINTER_TO_INT(tmp->data);
        actionname = g_strdup_printf("app.monitor-%d", nth);
        label = g_strdup_printf(_("Display _%d"), nth + 1);
        item = g_menu_item_new(label, actionname);

        g_menu_append_item(G_MENU(menu), item);

        g_free(label);
        g_free(actionname);

        tmp = tmp->next;
    }

    for (tmp = priv->windows, nth = 0; tmp; tmp = tmp->next, nth++) {
        VirtViewerWindow *win = VIRT_VIEWER_WINDOW(tmp->data);
        VirtViewerDisplay *display = virt_viewer_window_get_display(win);

        if (VIRT_VIEWER_IS_DISPLAY_VTE(display)) {
            gchar *name = NULL;
            GMenuItem *item;
            gchar *actionname;

            g_object_get(display, "name", &name, NULL);
            actionname = g_strdup_printf("app.vte-%d", nth);

            item = g_menu_item_new(name, actionname);

            g_menu_append_item(G_MENU(menu), item);

            g_free(actionname);
            g_free(name);
        }
    }

    g_list_free(keys);
}

static void
virt_viewer_app_clear_window_actions(VirtViewerApp *self)
{
    gchar **oldactions = g_action_group_list_actions(G_ACTION_GROUP(self));
    int i;

    for (i = 0; oldactions && oldactions[i] != NULL; i++) {
        if (g_str_has_prefix(oldactions[i], "monitor-") ||
            g_str_has_prefix(oldactions[i], "vte-") ||
            g_str_has_prefix(oldactions[i], "video-codec")) {
            g_action_map_remove_action(G_ACTION_MAP(self), oldactions[i]);
        }
    }

    g_strfreev(oldactions);
}


static void
virt_viewer_app_create_window_actions(VirtViewerApp *self)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    GList *keys = g_hash_table_get_keys(priv->displays);
    GList *tmp;
    gboolean sensitive;
    gboolean visible;
    GSimpleAction *action;
    gchar *actionname;
    int nth;

    tmp = keys;
    while (tmp) {
        VirtViewerWindow *vwin;
        VirtViewerDisplay *display;

        nth = GPOINTER_TO_INT(tmp->data);
        actionname = g_strdup_printf("monitor-%d", nth);

        vwin = virt_viewer_app_get_nth_window(self, nth);
        display = VIRT_VIEWER_DISPLAY(g_hash_table_lookup(priv->displays, tmp->data));
        visible = vwin && gtk_widget_get_visible(GTK_WIDGET(virt_viewer_window_get_window(vwin)));

        sensitive = visible;
        if (display) {
            guint hint = virt_viewer_display_get_show_hint(display);

            if (hint & VIRT_VIEWER_DISPLAY_SHOW_HINT_READY)
                sensitive = TRUE;

            if (virt_viewer_display_get_selectable(display))
                sensitive = TRUE;
        }

        action = g_simple_action_new_stateful(actionname,
                                              NULL,
                                              g_variant_new_boolean(visible));
        g_object_set_data(G_OBJECT(action), "nth", GINT_TO_POINTER(nth));
        g_simple_action_set_enabled(action,
                                    sensitive);

        g_signal_connect(action, "change-state",
                         G_CALLBACK(virt_viewer_app_action_monitor),
                         self);

        g_action_map_add_action(G_ACTION_MAP(self), G_ACTION(action));


        g_free(actionname);

        tmp = tmp->next;
    }

    for (tmp = priv->windows, nth = 0; tmp; tmp = tmp->next, nth++) {
        VirtViewerWindow *win = VIRT_VIEWER_WINDOW(tmp->data);
        VirtViewerDisplay *display = virt_viewer_window_get_display(win);
        gchar *name;

        if (!VIRT_VIEWER_IS_DISPLAY_VTE(display)) {
            continue;
        }

        g_object_get(display, "name", &name, NULL);
        actionname = g_strdup_printf("vte-%d", nth);

        visible = gtk_widget_get_visible(GTK_WIDGET(virt_viewer_window_get_window(win)));

        action = g_simple_action_new_stateful(actionname,
                                              NULL,
                                              g_variant_new_boolean(visible));
        g_object_set_data_full(G_OBJECT(action), "vte", g_strdup(name), g_free);
        g_simple_action_set_enabled(G_SIMPLE_ACTION(action),
                                    TRUE);

        g_signal_connect(action, "change-state",
                         G_CALLBACK(virt_viewer_app_action_vte),
                         self);

        g_action_map_add_action(G_ACTION_MAP(self), G_ACTION(action));

        g_free(actionname);
        g_free(name);
    }

    g_list_free(keys);
}

static void
virt_viewer_app_update_menu_displays(VirtViewerApp *self)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    if (!priv->windows)
        return;
    virt_viewer_app_clear_window_actions(self);
    virt_viewer_app_create_window_actions(self);
    g_list_foreach(priv->windows, window_update_menu_displays_cb, self);
    g_list_foreach(priv->windows, window_update_menu_video_codecs, self);
}

void
virt_viewer_app_set_connect_info(VirtViewerApp *self,
                                 const gchar *host,
                                 const gchar *ghost,
                                 const gchar *gport,
                                 const gchar *gtlsport,
                                 const gchar *transport,
                                 const gchar *unixsock,
                                 const gchar *user,
                                 gint port,
                                 const gchar *guri)
{
    g_return_if_fail(VIRT_VIEWER_IS_APP(self));
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);

    g_debug("Set connect info: %s,%s,%s,%s,%s,%s,%s,%d",
              host, ghost, gport ? gport : "-1", gtlsport ? gtlsport : "-1", transport, unixsock, user, port);

    g_free(priv->host);
    g_free(priv->ghost);
    g_free(priv->gport);
    g_free(priv->gtlsport);
    g_free(priv->transport);
    g_free(priv->unixsock);
    g_free(priv->user);
    g_free(priv->guri);

    priv->host = g_strdup(host);
    priv->ghost = g_strdup(ghost);
    priv->gport = g_strdup(gport);
    priv->gtlsport = g_strdup(gtlsport);
    priv->transport = g_strdup(transport);
    priv->unixsock = g_strdup(unixsock);
    priv->user = g_strdup(user);
    priv->guri = g_strdup(guri);
    priv->port = port;

    virt_viewer_app_update_pretty_address(self);
}

void
virt_viewer_app_free_connect_info(VirtViewerApp *self)
{
    g_return_if_fail(VIRT_VIEWER_IS_APP(self));

    virt_viewer_app_set_connect_info(self, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0, NULL);
}

VirtViewerWindow*
virt_viewer_app_get_main_window(VirtViewerApp *self)
{
    g_return_val_if_fail(VIRT_VIEWER_IS_APP(self), NULL);

    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    return priv->main_window;
}

static void
show_status_cb(gpointer value,
               gpointer user_data)
{
    VirtViewerNotebook *nb = virt_viewer_window_get_notebook(VIRT_VIEWER_WINDOW(value));
    gchar *text = (gchar*)user_data;

    virt_viewer_notebook_show_status(nb, "%s", text);
}

void
virt_viewer_app_show_status(VirtViewerApp *self, const gchar *fmt, ...)
{
    va_list args;
    gchar *text;

    g_return_if_fail(VIRT_VIEWER_IS_APP(self));
    g_return_if_fail(fmt != NULL);

    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);

    va_start(args, fmt);
    text = g_strdup_vprintf(fmt, args);
    va_end(args);

    g_list_foreach(priv->windows, show_status_cb, text);
    g_free(text);
}

static void
show_display_cb(gpointer value,
                gpointer user_data G_GNUC_UNUSED)
{
    VirtViewerNotebook *nb = virt_viewer_window_get_notebook(VIRT_VIEWER_WINDOW(value));

    virt_viewer_notebook_show_display(nb);
}

void
virt_viewer_app_show_display(VirtViewerApp *self)
{
    g_return_if_fail(VIRT_VIEWER_IS_APP(self));
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    g_list_foreach(priv->windows, show_display_cb, self);
}

VirtViewerSession*
virt_viewer_app_get_session(VirtViewerApp *self)
{
    g_return_val_if_fail(VIRT_VIEWER_IS_APP(self), FALSE);

    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    return priv->session;
}

GList*
virt_viewer_app_get_windows(VirtViewerApp *self)
{
    g_return_val_if_fail(VIRT_VIEWER_IS_APP(self), NULL);
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    return priv->windows;
}

static void
share_folder_changed(VirtViewerApp *self)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    gchar *folder;

    folder = gtk_file_chooser_get_filename(priv->preferences_shared_folder);

    g_object_set(virt_viewer_app_get_session(self),
                 "shared-folder", folder, NULL);

    g_free(folder);
}

static GtkWidget *
virt_viewer_app_get_preferences(VirtViewerApp *self)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    VirtViewerSession *session = virt_viewer_app_get_session(self);
    GtkBuilder *builder = virt_viewer_util_load_ui("virt-viewer-preferences.ui");
    gboolean can_share_folder = virt_viewer_session_can_share_folder(session);
    GtkWidget *preferences = priv->preferences;
    gchar *path;

    if (preferences)
        goto end;

    gtk_builder_connect_signals(builder, self);

    preferences = GTK_WIDGET(gtk_builder_get_object(builder, "preferences"));
    priv->preferences = preferences;

    g_object_bind_property(self,
                           "config-share-clipboard",
                           gtk_builder_get_object(builder, "cbshareclipboard"),
                           "active",
                           G_BINDING_BIDIRECTIONAL|G_BINDING_SYNC_CREATE);

    g_object_set (gtk_builder_get_object(builder, "cbshareclipboard"),
                  "sensitive", virt_viewer_app_get_supports_share_clipboard(self), NULL);
    g_object_set (gtk_builder_get_object(builder, "cbsharefolder"),
                  "sensitive", can_share_folder, NULL);
    g_object_set (gtk_builder_get_object(builder, "cbsharefolderro"),
                  "sensitive", can_share_folder, NULL);
    g_object_set (gtk_builder_get_object(builder, "fcsharefolder"),
                  "sensitive", can_share_folder, NULL);

    if (!can_share_folder)
        goto end;

    g_object_bind_property(virt_viewer_app_get_session(self),
                           "share-folder",
                           gtk_builder_get_object(builder, "cbsharefolder"),
                           "active",
                           G_BINDING_BIDIRECTIONAL|G_BINDING_SYNC_CREATE);

    g_object_bind_property(virt_viewer_app_get_session(self),
                           "share-folder-ro",
                           gtk_builder_get_object(builder, "cbsharefolderro"),
                           "active",
                           G_BINDING_BIDIRECTIONAL|G_BINDING_SYNC_CREATE);

    priv->preferences_shared_folder =
        GTK_FILE_CHOOSER(gtk_builder_get_object(builder, "fcsharefolder"));

    g_object_get(virt_viewer_app_get_session(self),
                 "shared-folder", &path, NULL);

    gtk_file_chooser_set_filename(priv->preferences_shared_folder, path);
    g_free(path);

    virt_viewer_signal_connect_object(priv->preferences_shared_folder,
                                      "file-set",
                                      G_CALLBACK(share_folder_changed), self,
                                      G_CONNECT_SWAPPED);

end:
    g_object_unref(builder);

    return preferences;
}

void
virt_viewer_app_show_preferences(VirtViewerApp *self, GtkWidget *parent)
{
    GtkWidget *preferences = virt_viewer_app_get_preferences(self);

    gtk_window_set_transient_for(GTK_WINDOW(preferences),
                                 GTK_WINDOW(parent));

    gtk_window_present(GTK_WINDOW(preferences));
}

static gboolean
option_kiosk_quit(G_GNUC_UNUSED const gchar *option_name,
                  const gchar *value,
                  G_GNUC_UNUSED gpointer data, GError **error)
{
    if (g_str_equal(value, "never")) {
        opt_kiosk_quit = FALSE;
        return TRUE;
    }
    if (g_str_equal(value, "on-disconnect")) {
        opt_kiosk_quit = TRUE;
        return TRUE;
    }

    g_set_error(error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED, _("Invalid kiosk-quit argument: %s"), value);
    return FALSE;
}

static void
virt_viewer_app_add_option_entries(G_GNUC_UNUSED VirtViewerApp *self,
                                   G_GNUC_UNUSED GOptionContext *context,
                                   GOptionGroup *group)
{
    static const GOptionEntry options [] = {
        { "version", 'V', 0, G_OPTION_ARG_NONE, &opt_version,
          N_("Display version information"), NULL },
        { "zoom", 'z', 0, G_OPTION_ARG_INT, &opt_zoom,
          N_("Zoom level of window, in percentage"), "ZOOM" },
        { "full-screen", 'f', 0, G_OPTION_ARG_NONE, &opt_fullscreen,
          N_("Open in full screen mode (adjusts guest resolution to fit the client)"), NULL },
        { "hotkeys", 'H', 0, G_OPTION_ARG_STRING, &opt_hotkeys,
          N_("Customise hotkeys"), NULL },
        { "auto-resize", '\0', 0, G_OPTION_ARG_STRING, &opt_resize,
          N_("Automatically resize remote framebuffer"), N_("<never|always>") },
        { "keymap", 'K', 0, G_OPTION_ARG_STRING, &opt_keymap,
          N_("Remap keys format key=keymod+key e.g. F1=SHIFT+CTRL+F1,1=SHIFT+F1,ALT_L=Void"), NULL },
        { "cursor", '\0', 0, G_OPTION_ARG_STRING, &opt_cursor,
          N_("Cursor display mode: 'local' or 'auto'"), "MODE" },
        { "kiosk", 'k', 0, G_OPTION_ARG_NONE, &opt_kiosk,
          N_("Enable kiosk mode"), NULL },
        { "kiosk-quit", '\0', 0, G_OPTION_ARG_CALLBACK, option_kiosk_quit,
          N_("Quit on given condition in kiosk mode"), N_("<never|on-disconnect>") },
        { "verbose", 'v', 0, G_OPTION_ARG_NONE, &opt_verbose,
          N_("Display verbose information"), NULL },
        { "debug", '\0', 0, G_OPTION_ARG_NONE, &opt_debug,
          N_("Display debugging information"), NULL },
        { NULL, 0, 0, G_OPTION_ARG_NONE, NULL, NULL, NULL }
    };

    g_option_group_add_entries(group, options);
}

gboolean virt_viewer_app_get_session_cancelled(VirtViewerApp *self)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    return priv->cancelled;
}

gboolean virt_viewer_app_get_config_share_clipboard(VirtViewerApp *self)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);

    GError *error = NULL;
    gboolean share_clipboard;

    share_clipboard = g_key_file_get_boolean(priv->config,
                                             "virt-viewer", "share-clipboard", &error);

    if (error) {
        share_clipboard = TRUE; /* backwards-compatible default value */
        g_clear_error(&error);
    }

    return share_clipboard;
}

void virt_viewer_app_set_config_share_clipboard(VirtViewerApp *self, gboolean enable)
{
    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);

    g_key_file_set_boolean(priv->config,
                           "virt-viewer", "share-clipboard", enable);
    g_object_notify(G_OBJECT(self), "config-share-clipboard");
}

gboolean virt_viewer_app_get_supports_share_clipboard(VirtViewerApp *self)
{
    g_return_val_if_fail(VIRT_VIEWER_IS_APP(self), FALSE);

    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);

    return priv->supports_share_clipboard;
}

void virt_viewer_app_set_supports_share_clipboard(VirtViewerApp *self, gboolean enable)
{
    g_return_if_fail(VIRT_VIEWER_IS_APP(self));

    VirtViewerAppPrivate *priv = virt_viewer_app_get_instance_private(self);
    if (priv->supports_share_clipboard == enable)
        return;

    priv->supports_share_clipboard = enable;
    g_object_notify(G_OBJECT(self), "supports-share-clipboard");
}
