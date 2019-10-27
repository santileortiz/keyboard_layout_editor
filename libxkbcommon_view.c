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
#include "gdk_keysym_names.h"

struct interactive_debug_app_t {
    mem_pool_t pool;

    char *repr_path;
    char *settings_file_path;

    GtkWidget *window;
    GtkWidget *keymap_test_button;
    GtkWidget *header_bar;
    GtkWidget *headerbar_buttons;
    struct keyboard_view_t *keyboard_view;

    int num_mods;
    const char **mod_names;

    // Stores if the key at the index has been pressed before.
    bool key_pressed[KEY_CNT];

    struct gsettings_layout_t original_active_layout;
};

struct interactive_debug_app_t app = {0};

static gint on_gdk_key_event (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    struct interactive_debug_app_t *app = (struct interactive_debug_app_t*)user_data;

    // Detect if the event is a key press repetition and update the state in the
    // pressed array.
    bool ignore_keypress_repetition = false;
    if (app->key_pressed[event->hardware_keycode]) {
        if (event->type == GDK_KEY_PRESS) {
            ignore_keypress_repetition = true;
        } else if (event->type == GDK_KEY_RELEASE) {
            app->key_pressed[event->hardware_keycode] = false;
        }

    } else {
        if (event->type == GDK_KEY_PRESS) {
            app->key_pressed[event->hardware_keycode] = true;

        } else if (event->type == GDK_KEY_RELEASE) {
            // As far as I know there is no way this can happen unless something
            // weird happens.
            invalid_code_path;
        }
    }

    // Don't continue if this is a key repetitions.
    // TODO: We may want to make this optional if we get serious about
    // supporting the repeat setting of the XKB file format.
    if (!ignore_keypress_repetition) {
        struct keyboard_view_t *kv = app->keyboard_view;
        struct xkb_state *state = kv->xkb_state;
        string_t info_str = {0};
        xkb_keycode_t keycode = event->hardware_keycode;
        enum xkb_state_component changed;

        str_cat_c (&info_str, "-------\n");
        str_cat_c (&info_str, "GDK/GTK\n");
        str_cat_c (&info_str, "-------\n");
        str_cat_c (&info_str, "type: ");
        if (event->type == GDK_KEY_PRESS) {
            str_cat_c (&info_str, "KEY_PRESS\n");
            changed = xkb_state_update_key(state, keycode, XKB_KEY_DOWN);

        } else if (event->type == GDK_KEY_RELEASE) {
            str_cat_c (&info_str, "KEY_RELEASE\n");
            changed = xkb_state_update_key(state, keycode, XKB_KEY_UP);
        }

        str_cat_c (&info_str, "send_event: ");
        if (event->send_event == TRUE) {
            str_cat_c (&info_str, "TRUE\n");
        } else {
            str_cat_c (&info_str, "FALSE\n");
        }

        str_cat_printf (&info_str, "time: %d\n", event->time);

        str_cat_c (&info_str, "state: ");
        bool is_first = true;
        for (uint32_t mask = event->state;
             mask;
             mask = mask & (mask-1)) {
            uint32_t next_bit_mask = mask & -mask;

            if (!is_first) {
                str_cat_c (&info_str, ", ");
            }
            is_first = false;
            str_cat_printf (&info_str, "%s", gdk_modifier_names[bit_pos(next_bit_mask)]);
        }
        str_cat_c (&info_str, "\n");

        // TODO: Don't do linear search here.
        const char *keyval_name = NULL;
        for (int i = 0; keyval_name == NULL && i<ARRAY_SIZE(gdk_keysym_names); i++) {
            if (gdk_keysym_names[i].val == event->keyval) {
                keyval_name = gdk_keysym_names[i].name;
            }
        }
        str_cat_printf (&info_str, "keyval: %s\n", keyval_name);
        str_cat_printf (&info_str, "length: %i\n", event->length);
        str_cat_printf (&info_str, "string: %s\n", event->string);
        str_cat_printf (&info_str, "hardware_keycode: %d\n", event->hardware_keycode);
        str_cat_printf (&info_str, "group: %d\n", event->group);
        str_cat_printf (&info_str, "is_modifier: %d\n", event->is_modifier);

        str_cat_c (&info_str, "------------\n");
        str_cat_c (&info_str, "LIBXKBCOMMON\n");
        str_cat_c (&info_str, "------------\n");
        str_cat_printf (&info_str, "Changed: %x\n", changed);

        xkb_keysym_t keysym = xkb_state_key_get_one_sym(state, event->hardware_keycode);

        char keysym_name[64];
        xkb_keysym_get_name(keysym, keysym_name, sizeof(keysym_name));

        str_cat_printf (&info_str, "keysym: %s\n", keysym_name);

        str_cat_c (&info_str, "UTF-8: ");
        int size = xkb_state_key_get_utf8(state, keycode, NULL, 0) + 1;
        if (size > 1) {
            char *buffer = malloc (size);
            xkb_state_key_get_utf8(state, keycode, buffer, size);
            str_cat_printf (&info_str, "%s\n", buffer);
        } else {
            str_cat_c (&info_str, "(none)\n");
        }

        str_cat_c (&info_str, "Effective Mods: ");
        for (int i=0; i<app->num_mods; i++) {
            if (xkb_state_mod_index_is_active(state, i, XKB_STATE_MODS_EFFECTIVE)) {
                str_cat_printf (&info_str, "%s ", app->mod_names[i]);
            }
        }
        str_cat_c (&info_str, "\n");

        str_cat_c (&info_str, "Consumed Mods (XKB): ");
        for (int i=0; i<app->num_mods; i++) {
            if (xkb_state_mod_index_is_consumed2 (state, keycode, i, XKB_CONSUMED_MODE_XKB)) {
                str_cat_printf (&info_str, "%s ", app->mod_names[i]);
            }
        }
        str_cat_c (&info_str, "\n");

        str_cat_c (&info_str, "Consumed Mods (GTK): ");
        for (int i=0; i<app->num_mods; i++) {
            if (xkb_state_mod_index_is_consumed2 (state, keycode, i, XKB_CONSUMED_MODE_GTK)) {
                str_cat_printf (&info_str, "%s ", app->mod_names[i]);
            }
        }
        str_cat_c (&info_str, "\n\n");

        printf ("%s", str_data (&info_str));

        str_free (&info_str);
    }

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

    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size (GTK_WINDOW(app.window), 1200, 540);
    g_signal_connect (G_OBJECT(app.window), "delete-event", G_CALLBACK (window_delete_handler), NULL);
    gtk_window_set_position(GTK_WINDOW(app.window), GTK_WIN_POS_CENTER);
    gtk_window_set_gravity (GTK_WINDOW(app.window), GDK_GRAVITY_CENTER);

    app.header_bar = gtk_header_bar_new ();
    gtk_header_bar_set_title (GTK_HEADER_BAR(app.header_bar), "Keys");
    gtk_header_bar_set_show_close_button (GTK_HEADER_BAR(app.header_bar), TRUE);
    app.headerbar_buttons = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_header_bar_pack_start (GTK_HEADER_BAR(app.header_bar), app.headerbar_buttons);
    gtk_widget_show_all (app.header_bar);
    gtk_window_set_titlebar (GTK_WINDOW(app.window), app.header_bar);

    app.keymap_test_button = new_keymap_test_button ();
    gtk_container_add (GTK_CONTAINER (app.headerbar_buttons), app.keymap_test_button);

    app.repr_path = sh_expand (REPRESENTATIONS_DIR_PATH, &app.pool);
    app.settings_file_path = sh_expand (SETTINGS_FILE_PATH, &app.pool);
    app.keyboard_view = keyboard_view_new_with_gui (app.window,
                                                    app.repr_path, NULL,
                                                    app.settings_file_path);
    gtk_container_add(GTK_CONTAINER(app.window), wrap_gtk_widget(app.keyboard_view->widget));

    mem_pool_t tmp = {0};

    char *absolute_path = sh_expand (argv[1], &tmp);
    char *file_content;
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

        g_signal_connect (app.window, "key-press-event", G_CALLBACK (on_gdk_key_event), &app);
        g_signal_connect (app.window, "key-release-event", G_CALLBACK (on_gdk_key_event), &app);
    }

    if (keyboard_view_set_keymap (app.keyboard_view, file_content)) {
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

        gtk_widget_show_all (app.window);

        gtk_main();
    }

    if (keymap_installed) {
        xkb_keymap_remove_from_gsettings (info.name);
        xkb_keymap_set_active_full (app.original_active_layout.type, app.original_active_layout.name);
        xkb_keymap_uninstall (info.name);
    }

    mem_pool_destroy (&tmp);
    keyboard_view_destroy (app.keyboard_view);

    mem_pool_destroy (&app.pool);
}
