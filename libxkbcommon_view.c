/*
 * Copiright (C) 2019 Santiago León O.
 */

#define _GNU_SOURCE // Used to enable strcasestr()
#include "common.h"
#include "bit_operations.c"
#include "scanner.c"
#include "status.c"
#include "binary_tree.c"

#include <xkbcommon/xkbcommon.h>
#include <linux/input-event-codes.h>
#include "xkb_keycode_names.h"
#include "kernel_keycode_names.h"
#include "keysym_names.h"

#include <gtk/gtk.h>
#include "gresource.c"
#include "gtk_utils.c"
#include "fk_popover.c"
#include "fk_searchable_list.c"

#include "keyboard_view.h"
#include "keyboard_view_builder.c"
#include "keyboard_view_as_string.c"
#include "keyboard_view_repr_store.c"
#include "keyboard_view.c"

#include "keyboard_layout.c"
#include "xkb_file_backend.c"
#include "xkb_keymap_installer.c"

#include "settings.h"
#include "gdk_modifier_names.h"

struct interactive_debug_app_t {
    mem_pool_t pool;

    char *repr_path;
    char *settings_file_path;

    GtkWidget *header_bar;
    GtkWidget *headerbar_buttons;
    struct keyboard_view_t *keyboard_view;

    int num_mods;
    const char **mod_names;

    struct gsettings_layout_t original_active_layout;
};

struct interactive_debug_app_t app = {0};

static gint on_gdk_key_event (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    struct interactive_debug_app_t *app = (struct interactive_debug_app_t*)user_data;
    struct keyboard_view_t *kv = app->keyboard_view;
    struct xkb_state *state = kv->xkb_state;

    xkb_keycode_t keycode = event->hardware_keycode;
    enum xkb_state_component changed;
    printf ("type: ");
    if (event->type == GDK_KEY_PRESS) {
        printf ("KEY_PRESS\n");
        changed = xkb_state_update_key(state, keycode, XKB_KEY_DOWN);
    } else if (event->type == GDK_KEY_RELEASE) {
        printf ("KEY_RELEASE\n");
        changed = xkb_state_update_key(state, keycode, XKB_KEY_UP);
    } else {
        invalid_code_path;
    }

    printf ("state: ");
    bool is_first = true;
    for (uint32_t mask = event->state;
         mask;
         mask = mask & (mask-1)) {
        uint32_t next_bit_mask = mask & -mask;

        if (!is_first) {
            printf (", ");
        }
        is_first = false;
        printf ("%s", gdk_modifier_names[bit_pos(next_bit_mask)]);
    }
    printf ("\n");

    printf ("Changed: %x\n", changed);

    xkb_keysym_t keysym = xkb_state_key_get_one_sym(state, event->hardware_keycode);

    char keysym_name[64];
    xkb_keysym_get_name(keysym, keysym_name, sizeof(keysym_name));

    printf ("keysym: %s\n", keysym_name);

    int size = xkb_state_key_get_utf8(state, keycode, NULL, 0) + 1;
    if (size > 1) {
        char *buffer = malloc (size);
        xkb_state_key_get_utf8(state, keycode, buffer, size);
        printf ("UTF-8: %s\n", buffer);
    }

    // Print modifier information
    {
        printf ("Effective Mods: ");
        for (int i=0; i<app->num_mods; i++) {
            if (xkb_state_mod_index_is_active(state, i, XKB_STATE_MODS_EFFECTIVE)) {
                printf ("%s ", app->mod_names[i]);
            }
        }
        printf ("\n");

        printf ("Consumed Mods (XKB): ");
        for (int i=0; i<app->num_mods; i++) {
            if (xkb_state_mod_index_is_consumed2 (state, keycode, i, XKB_CONSUMED_MODE_XKB)) {
                printf ("%s ", app->mod_names[i]);
            }
        }
        printf ("\n");

        printf ("Consumed Mods (GTK): ");
        for (int i=0; i<app->num_mods; i++) {
            if (xkb_state_mod_index_is_consumed2 (state, keycode, i, XKB_CONSUMED_MODE_GTK)) {
                printf ("%s ", app->mod_names[i]);
            }
        }
        printf ("\n");
    }

    printf ("\n");

    return FALSE;
}

gboolean window_delete_handler (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    gtk_main_quit ();
    return FALSE;
}

GtkWidget *new_keymap_stop_test_button ();
void on_grab_input_button (GtkButton *button, gpointer user_data)
{
    if (grab_input (app.window)) {
        replace_wrapped_widget (&app.keymap_test_button, new_keymap_stop_test_button ());
    }
}

GtkWidget *new_keymap_test_button ();
void on_ungrab_input_button (GtkButton *button, gpointer user_data)
{
    ungrab_input ();

    replace_wrapped_widget (&app.keymap_test_button, new_keymap_test_button ());
}

GtkWidget *new_keymap_test_button ()
{
    return new_icon_button ("process-completed", "Test layout", G_CALLBACK(on_grab_input_button), NULL);
}

GtkWidget *new_keymap_stop_test_button ()
{
    return new_icon_button ("media-playback-stop", "Stop testing layout",
                            G_CALLBACK(on_ungrab_input_button), NULL);
}

int main (int argc, char *argv[])
{
    if (argc <= 1) {
        printf ("Usage: xkbcommon-view [XKB_FILE]\n");
        return 0;
    }

    init_kernel_keycode_names ();
    init_xkb_keycode_names ();
    gdk_modifier_names_init ();

    gtk_init(&argc, &argv);

    gtk_icon_theme_add_resource_path (gtk_icon_theme_get_default (),
                                      "/com/github/santileortiz/iconoscope/icons");

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size (GTK_WINDOW(window), 1200, 540);
    g_signal_connect (G_OBJECT(window), "delete-event", G_CALLBACK (window_delete_handler), NULL);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_window_set_gravity (GTK_WINDOW(window), GDK_GRAVITY_CENTER);

    app.header_bar = gtk_header_bar_new ();
    gtk_header_bar_set_title (GTK_HEADER_BAR(app.header_bar), "Keys");
    gtk_header_bar_set_show_close_button (GTK_HEADER_BAR(app.header_bar), TRUE);
    app.headerbar_buttons = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_header_bar_pack_start (GTK_HEADER_BAR(app.header_bar), app.headerbar_buttons);
    gtk_widget_show_all (app.header_bar);
    gtk_window_set_titlebar (GTK_WINDOW(window), app.header_bar);

    app.keymap_test_button = new_keymap_test_button ();
    gtk_container_add (GTK_CONTAINER (app.headerbar_buttons), app.keymap_test_button);

    app.repr_path = sh_expand (REPRESENTATIONS_DIR_PATH, &app.pool);
    app.settings_file_path = sh_expand (SETTINGS_FILE_PATH, &app.pool);
    app.keyboard_view = keyboard_view_new_with_gui (window,
                                                    app.repr_path, NULL,
                                                    app.settings_file_path);
    gtk_container_add(GTK_CONTAINER(window), wrap_gtk_widget(app.keyboard_view->widget));

    mem_pool_t tmp = {0};

    char *absolute_path = sh_expand (argv[1], &tmp);
    char *name, *file_content;
    path_split (&tmp, argv[1], NULL, &name);
    file_content = full_file_read (&tmp, argv[1]);

    struct keyboard_layout_info_t info = {0};
    info.name = "TEST_keyboard_view_test_installation";
    bool keymap_installed = false;;

    if (!xkb_keymap_install (absolute_path, &info)) {
        printf ("WARN: To install the layout and get GTK event info run with sudo.\n");

    } else if(xkb_keymap_get_active (&app.pool, &app.original_active_layout)) {
        // TODO: Changing gsettings as sudo seems to be different than changing
        // it without. Implement event capturing and see if the layout is
        // acttually changing or not.
        xkb_keymap_add_to_gsettings (info.name);
        if (!xkb_keymap_set_active (info.name)) {
            printf ("Failed to set the input layout as active.\n");
        } else {
            keymap_installed = true;
        }

        g_settings_sync();

        g_signal_connect (window, "key-press-event", G_CALLBACK (on_gdk_key_event), &app);
        g_signal_connect (window, "key-release-event", G_CALLBACK (on_gdk_key_event), &app);
    }

    if (keyboard_view_set_keymap (app.keyboard_view, name, file_content)) {
        // Compute the modifier name array for easy access
        {
            // TODO: I think we have better implementations for this in our xkb
            // tests.
            app.num_mods = xkb_keymap_num_mods (app.keyboard_view->xkb_keymap);
            app.mod_names = mem_pool_push_size (&app.pool, sizeof(char *)*app.num_mods);

            for (int i=0; i<app.num_mods; i++) {
                app.mod_names[i] = xkb_keymap_mod_get_name(app.keyboard_view->xkb_keymap, i);
            }
        }

        gtk_widget_show_all (window);

        gtk_main();
    }

    if (keymap_installed) {
        xkb_keymap_set_active_full (app.original_active_layout.type, app.original_active_layout.name);
        xkb_keymap_uninstall (info.name);
    }

    mem_pool_destroy (&tmp);
    keyboard_view_destroy (app.keyboard_view);

    mem_pool_destroy (&app.pool);
}
