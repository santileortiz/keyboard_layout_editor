/*
 * Copiright (C) 2019 Santiago León O.
 */

#define _GNU_SOURCE // Used to enable strcasestr()
#include "common.h"
#include <xkbcommon/xkbcommon.h>
#include <linux/input-event-codes.h>
#include "keycode_names.h"
#include "keysym_names.h"

#include <locale.h>

#include <gtk/gtk.h>
#include "gresource.c"
#include "gtk_utils.c"
#include "fk_popover.c"
#include "fk_searchable_list.c"

#include "keyboard_layout_editor.h"
struct kle_app_t app;

#include "keyboard_view_repr_store.h"
#include "keyboard_view_as_string.h"
#include "keyboard_view.c"
#include "keyboard_view_repr_store.c"
#include "keyboard_view_as_string.c"

string_t app_get_repr_path (struct kle_app_t *app)
{
    string_t empty = {0};
    return empty;
}

gboolean window_delete_handler (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    gtk_main_quit ();
    return FALSE;
}

GtkWidget* app_keys_sidebar_new (struct kle_app_t *app, int kc)
{
    return NULL;
}

void grab_input (GtkButton *button, gpointer user_data)
{
}

void ungrab_input (GtkButton *button, gpointer user_data)
{
}

int main (int argc, char *argv[])
{
    if (argc <= 1) {
        return 0;
    }

    app = ZERO_INIT(struct kle_app_t);

    gtk_init(&argc, &argv);

    app.gresource = gresource_get_resource ();
    gtk_icon_theme_add_resource_path (gtk_icon_theme_get_default (),
                                      "/com/github/santileortiz/iconoscope/icons");

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
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

    struct keyboard_view_t *kv = keyboard_view_new_with_gui (window);
    gtk_container_add(GTK_CONTAINER(window), wrap_gtk_widget(kv->widget));
    gtk_widget_show_all (window);

    mem_pool_t tmp = {0};
    char *name, *file_content;
    path_split (&tmp, argv[1], NULL, &name);
    file_content = full_file_read (&tmp, argv[1]);

    keyboard_view_set_keymap (kv, name, file_content);

    gtk_main();

    mem_pool_destroy (&tmp);
    keyboard_view_destroy (kv);
}
