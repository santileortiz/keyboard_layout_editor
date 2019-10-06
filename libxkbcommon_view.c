/*
 * Copiright (C) 2019 Santiago León O.
 */

#define _GNU_SOURCE // Used to enable strcasestr()
#include "common.h"
#include "scanner.c"

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

#include "settings.h"

gboolean window_delete_handler (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    gtk_main_quit ();
    return FALSE;
}

struct interactive_debug_app_t {
    mem_pool_t pool;

    char *repr_path;
    char *settings_file_path;

    GtkWidget *header_bar;
    GtkWidget *headerbar_buttons;
    struct keyboard_view_t *keyboard_view;
};

int main (int argc, char *argv[])
{
    struct interactive_debug_app_t app = {0};

    if (argc <= 1) {
        printf ("Usage: xkbcommon-view [XKB_FILE]\n");
        return 0;
    }

    init_kernel_keycode_names ();
    init_xkb_keycode_names ();

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
    app.headerbar_buttons = gtk_grid_new ();
    gtk_header_bar_pack_start (GTK_HEADER_BAR(app.header_bar), wrap_gtk_widget (app.headerbar_buttons));
    gtk_widget_show_all (app.header_bar);
    gtk_window_set_titlebar (GTK_WINDOW(window), app.header_bar);

    app.repr_path = sh_expand (REPRESENTATIONS_DIR_PATH, &app.pool);
    app.settings_file_path = sh_expand (SETTINGS_FILE_PATH, &app.pool);
    app.keyboard_view = keyboard_view_new_with_gui (window,
                                                    app.repr_path, NULL,
                                                    app.settings_file_path);
    gtk_container_add(GTK_CONTAINER(window), wrap_gtk_widget(app.keyboard_view->widget));

    mem_pool_t tmp = {0};
    char *name, *file_content;
    path_split (&tmp, argv[1], NULL, &name);
    file_content = full_file_read (&tmp, argv[1]);

    if (keyboard_view_set_keymap (app.keyboard_view, name, file_content)) {
        gtk_widget_show_all (window);

        gtk_main();
    }

    mem_pool_destroy (&tmp);
    keyboard_view_destroy (app.keyboard_view);

    mem_pool_destroy (&app.pool);
}
