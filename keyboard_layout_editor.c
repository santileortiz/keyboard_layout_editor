/*
 * Copiright (C) 2018 Santiago León O.
 */

#include "common.h"
#include "xkb_keymap_installer.c"

#include <gtk/gtk.h>

char* argv_0;

static inline
void str_cat_full_path (string_t *str, char *path)
{
    char *expanded_path = sh_expand (path, NULL); // maybe substitute ~
    char *abs_path = realpath (expanded_path, NULL); // get an absolute path
    str_cat_c (str, abs_path);
    free (expanded_path);
    free (abs_path);
}

bool unprivileged_xkb_keymap_install (char *keymap_path)
{
    bool success = true;
    if (!xkb_keymap_install (keymap_path)) {
        if (errno == EACCES) {
            string_t command = str_new ("pkexec ");
            str_cat_full_path (&command, argv_0);
            str_cat_c (&command, " --install ");
            str_cat_full_path (&command, keymap_path);

            int retval = system (str_data (&command));
            if (!WIFEXITED (retval)) {
                printf ("Could not call pkexec\n");
            }
            str_free (&command);
        } else {
            success = false;
        }
    }
    return success;
}

bool unprivileged_xkb_keymap_uninstall (char *layout_name)
{
    bool success = true;
    if (!xkb_keymap_uninstall (layout_name)) {
        if (errno == EACCES) {
            string_t command = str_new ("pkexec ");
            str_cat_full_path (&command, argv_0);
            str_cat_c (&command, " --uninstall ");
            str_cat_c (&command, layout_name);

            int retval = system (str_data (&command));
            if (!WIFEXITED (retval)) {
                printf ("Could not call pkexec.\n");
            }
            str_free (&command);
        } else {
            success = false;
        }
    }
    return success;
}

bool unprivileged_xkb_keymap_uninstall_everything ()
{
    bool success = true;
    if (!xkb_keymap_uninstall_everything ()) {
        if (errno == EACCES) {
            string_t command = str_new ("pkexec ");
            str_cat_full_path (&command, argv_0);
            str_cat_c (&command, " --uninstall-everything");

            int retval = system (str_data (&command));
            if (!WIFEXITED (retval)) {
                printf ("Could not call pkexec.\n");
            }
            str_free (&command);
        } else {
            success = false;
        }
    }
    return success;
}

void add_custom_css (GtkWidget *widget, gchar *css_data)
{
    GtkStyleContext *style_context = gtk_widget_get_style_context (widget);
    GtkCssProvider *css_provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_data (css_provider, css_data, -1, NULL);
    gtk_style_context_add_provider (style_context,
                                    GTK_STYLE_PROVIDER(css_provider),
                                    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

void add_css_class (GtkWidget *widget, char *class)
{
    GtkStyleContext *ctx = gtk_widget_get_style_context (widget);
    gtk_style_context_add_class (ctx, class);
}

gboolean delete_callback (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    gtk_main_quit ();
    return FALSE;
}

gboolean render_keyboard (GtkWidget *widget, cairo_t *cr, gpointer data)
{
    cairo_set_source_rgba (cr, 1, 1, 1, 1);
    cairo_paint(cr);
    return FALSE;
}

int main (int argc, char *argv[])
{
    bool success = true;
    argv_0 = argv[0];
    if (argc > 1) {
        if (strcmp (argv[1], "--install") == 0) {
            if (argc == 2) {
                printf ("Expected a keymap file to install.\n");
            } else {
                success = unprivileged_xkb_keymap_install (argv[2]);
            }
        } else if (strcmp (argv[1], "--uninstall") == 0) {
            if (argc == 2) {
                printf ("Expected a keymap name to uninstall.\n");
            } else {
                success = unprivileged_xkb_keymap_uninstall (argv[2]);
            }
        } else if (strcmp (argv[1], "--uninstall-everything") == 0) {
            success = unprivileged_xkb_keymap_uninstall_everything ();
        }
    } else {
        GtkWidget *window;

        gtk_init(&argc, &argv);

        window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_resize (GTK_WINDOW(window), 970, 650);
        gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
        GtkWidget *header_bar = gtk_header_bar_new ();
        gtk_header_bar_set_title (GTK_HEADER_BAR(header_bar), "Keyboard Editor");
        gtk_header_bar_set_show_close_button (GTK_HEADER_BAR(header_bar), TRUE);
        gtk_window_set_titlebar (GTK_WINDOW(window), header_bar);

        g_signal_connect (G_OBJECT(window), "delete-event", G_CALLBACK (delete_callback), NULL);

        GtkWidget *keyboard = gtk_drawing_area_new ();
        gtk_widget_set_size_request (keyboard, 100, 100);
        g_signal_connect (G_OBJECT (keyboard), "draw", G_CALLBACK (render_keyboard), NULL);

        char *custom_layouts[] = {"my_layout", "my_other_layout"};
        GtkWidget *custom_layout_list;
        {
            GtkWidget *list = gtk_list_box_new ();
            gtk_widget_set_vexpand (list, TRUE);
            gtk_widget_set_hexpand (list, TRUE);
            //g_signal_connect (G_OBJECT(list), "row-selected", G_CALLBACK (on_custom_layout_selected), NULL);

            // Create rows
            int i;
            bool first = true;
            for (i=0; i < ARRAY_SIZE(custom_layouts); i++) {
                GtkWidget *row = gtk_label_new (custom_layouts[i]);
                gtk_container_add (GTK_CONTAINER(list), row);
                gtk_widget_set_halign (row, GTK_ALIGN_START);

                // Select the first entry
                if (first) {
                    first = true;
                    GtkWidget *r = gtk_widget_get_parent (row);
                    gtk_list_box_select_row (GTK_LIST_BOX(list), GTK_LIST_BOX_ROW(r));
                }

                gtk_widget_set_margin_start (row, 6);
                gtk_widget_set_margin_end (row, 6);
                gtk_widget_set_margin_top (row, 3);
                gtk_widget_set_margin_bottom (row, 3);
            }

            custom_layout_list = gtk_scrolled_window_new (NULL, NULL);
            gtk_container_add (GTK_CONTAINER (custom_layout_list), list);
        }

        GtkWidget *new_layout_button = gtk_button_new ();
        {
            new_layout_button = gtk_button_new ();
            add_css_class (new_layout_button, "flat");
            GtkWidget *grid = gtk_grid_new ();

            GtkWidget *title = gtk_label_new ("New Layout");
            add_css_class (title, "h3");
            gtk_widget_set_halign (title, GTK_ALIGN_START);
            gtk_grid_attach (GTK_GRID(grid), title, 1, 0, 1, 1);

            GtkWidget *subtitle = gtk_label_new ("Create a layout based on an existing one.");
            add_css_class (subtitle, "dim-label");
            gtk_widget_set_halign (subtitle, GTK_ALIGN_START);
            gtk_grid_attach (GTK_GRID(grid), subtitle, 1, 1, 1, 1);

            GtkWidget *image = gtk_image_new_from_icon_name ("document-new", GTK_ICON_SIZE_DIALOG);
            gtk_grid_attach (GTK_GRID(grid), image, 0, 0, 1, 2);

            gtk_container_add (GTK_CONTAINER(new_layout_button), grid);
        }

        GtkWidget *sidebar = gtk_grid_new ();
        gtk_grid_set_row_spacing (GTK_GRID(sidebar), 12);
        add_custom_css (sidebar, ".grid, grid { margin: 12px; }");
        gtk_grid_attach (GTK_GRID(sidebar), custom_layout_list, 0, 0, 1, 1);
        gtk_grid_attach (GTK_GRID(sidebar), new_layout_button, 0, 1, 1, 1);

        // FIXME: Using panned here causes a lot of assertion fails inside the
        // function _gtk_widget_get_preferred_size_for_size(). The issue seems
        // to be inside GtkPaned for the following reasons:
        //
        //   Stops the failed assert:
        //     - Not attaching custom_layout_list to sidebar.
        //     - Using a less wide button for new_layout_button.
        //     - Using a GtkGrid instead of GtkPaned.
        //
        //   Does not stop the assert from failing:
        //    - Creating custom_layout_list as GtkListBox (don't wrap it inside
        //      a GtkScrolledWindow), although it reduces the number of errors
        //      by half.
        //    - Wrapping GtkListBox inside a GtkBox. This also reduces the
        //      number of errors by half.
        //
        //  Also seems to be related to elementary OS as in Fedora 27 the issue
        //  does not happen.
#if 1
        GtkWidget *paned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
        gtk_paned_pack1 (GTK_PANED(paned), sidebar, FALSE, FALSE);
        gtk_paned_pack2 (GTK_PANED(paned), keyboard, TRUE, TRUE);
        gtk_paned_set_position (GTK_PANED(paned), 200);
        gtk_container_add(GTK_CONTAINER(window), paned);
#else
        GtkWidget *no_warn = gtk_grid_new();
        gtk_grid_attach (GTK_GRID(no_warn), sidebar, 0, 0, 1, 1);
        gtk_grid_attach (GTK_GRID(no_warn), keyboard, 1, 0, 1, 1);
        gtk_container_add(GTK_CONTAINER(window), no_warn);
#endif

        gtk_widget_show_all(window);

        gtk_main();
    }

    xmlCleanupParser();

    return !success;
}
