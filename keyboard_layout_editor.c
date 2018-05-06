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

// The following are wrapper functions that use Polkit's pkexec to call the
// layout installation API. If we ever get to install layout locally for a user,
// then this should not be necessary.
//
// @Polkit_wrapper
//
// TODO: Redirect stderr so we don't cluttter the user's output, when they press
// the cancel button.
// TODO: Internationalization of the authentication dialog has to be done
// through a .policy file (see man pkexec). There is not a lot of control over
// the buttons or message in the dialog.
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
                printf ("Could not call pkexec. %i\n", retval);
            }
            str_free (&command);
        } else {
            success = false;
        }
    }
    return success;
}

// @Polkit_wrapper
bool unprivileged_xkb_keymap_uninstall (const char *layout_name)
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

// @Polkit_wrapper
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

GtkWidget* intro_button_new (char *icon_name, char *title, char *subtitle)
{
    GtkWidget *new_button = gtk_button_new ();
    add_css_class (new_button, "flat");
    GtkWidget *grid = gtk_grid_new ();

    GtkWidget *title_label = gtk_label_new (title);
    add_css_class (title_label, "h3");
    gtk_widget_set_halign (title_label, GTK_ALIGN_START);
    gtk_grid_attach (GTK_GRID(grid), title_label, 1, 0, 1, 1);

    GtkWidget *subtitle_label = gtk_label_new (subtitle);
    add_css_class (subtitle_label, "dim-label");
    gtk_widget_set_halign (subtitle_label, GTK_ALIGN_START);
    gtk_grid_attach (GTK_GRID(grid), subtitle_label, 1, 1, 1, 1);

    GtkWidget *image = gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_DIALOG);
    gtk_grid_attach (GTK_GRID(grid), image, 0, 0, 1, 2);

    gtk_container_add (GTK_CONTAINER(new_button), grid);
    gtk_widget_show_all (new_button);
    return new_button;
}

GtkWidget *window;
GtkWidget *custom_layout_list;

// This is done from a callback queued by the button handler to let the main
// loop destroy the GtkFileChooserDialog before asking for authentication. If
// authentication was not required then this should not be necessary.
gboolean install_layout_callback (gpointer layout_path)
{
    unprivileged_xkb_keymap_install (layout_path);
    g_free (layout_path);
    return G_SOURCE_REMOVE;
}

void install_layout_handler (GtkButton *button, gpointer   user_data)
{
    GtkWidget *dialog =
        gtk_file_chooser_dialog_new ("Install Layout",
                                     GTK_WINDOW(window),
                                     GTK_FILE_CHOOSER_ACTION_OPEN,
                                     "_Cancel",
                                     GTK_RESPONSE_CANCEL,
                                     "_Install",
                                     GTK_RESPONSE_ACCEPT,
                                     NULL);

    char *fname;
    gint result = gtk_dialog_run (GTK_DIALOG (dialog));
    if (result == GTK_RESPONSE_ACCEPT) {
        fname = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER(dialog));
        g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, install_layout_callback, fname, NULL);
    }
    gtk_widget_destroy (dialog);
}

gboolean delete_layout_handler (GtkButton *button, gpointer user_data)
{
    GtkListBoxRow *row = gtk_list_box_get_selected_row (GTK_LIST_BOX(custom_layout_list));
    GtkWidget *label = gtk_bin_get_child (GTK_BIN(row));
    const gchar *curr_layout = gtk_label_get_text (GTK_LABEL (label));
    unprivileged_xkb_keymap_uninstall (curr_layout);
    return G_SOURCE_REMOVE;
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
        gtk_init(&argc, &argv);

        window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_resize (GTK_WINDOW(window), 1120, 510);
        gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
        g_signal_connect (G_OBJECT(window), "delete-event", G_CALLBACK (delete_callback), NULL);
        gtk_widget_show (window);

        GtkWidget *header_bar = gtk_header_bar_new ();
        gtk_header_bar_set_title (GTK_HEADER_BAR(header_bar), "Keyboard Editor");
        gtk_header_bar_set_show_close_button (GTK_HEADER_BAR(header_bar), TRUE);

        // TODO: For some reason the highlight circle on this button button has
        // height 32px but width 30px. Code's circle on header buttons is 30px by 30px.
        GtkWidget *delete_layout_button = gtk_button_new_from_icon_name ("list-remove", GTK_ICON_SIZE_LARGE_TOOLBAR);
        g_signal_connect (delete_layout_button, "clicked", G_CALLBACK (delete_layout_handler), NULL);
        gtk_widget_set_halign (delete_layout_button, GTK_ALIGN_FILL);
        gtk_widget_set_valign (delete_layout_button, GTK_ALIGN_FILL);
        gtk_widget_show (delete_layout_button);
        gtk_header_bar_pack_start (GTK_HEADER_BAR(header_bar), delete_layout_button);

        gtk_window_set_titlebar (GTK_WINDOW(window), header_bar);
        gtk_widget_show (header_bar);

        GtkWidget *keyboard = gtk_drawing_area_new ();
        gtk_widget_set_vexpand (keyboard, TRUE);
        gtk_widget_set_hexpand (keyboard, TRUE);
        g_signal_connect (G_OBJECT (keyboard), "draw", G_CALLBACK (render_keyboard), NULL);
        gtk_widget_show (keyboard);

        GtkWidget *scrolled_custom_layout_list;
        {
            custom_layout_list = gtk_list_box_new ();
            gtk_widget_set_vexpand (custom_layout_list, TRUE);
            gtk_widget_set_hexpand (custom_layout_list, TRUE);
            //g_signal_connect (G_OBJECT(custom_layout_list), "row-selected", G_CALLBACK (on_custom_layout_selected), NULL);

            mem_pool_t tmp = {0};
            char **custom_layouts;
            int num_custom_layouts;
            xkb_keymap_list (&tmp, &custom_layouts, &num_custom_layouts);

            // Create rows
            int i;
            for (i=0; i < num_custom_layouts; i++) {
                GtkWidget *row = gtk_label_new (custom_layouts[i]);
                gtk_container_add (GTK_CONTAINER(custom_layout_list), row);
                gtk_widget_set_halign (row, GTK_ALIGN_START);

                gtk_widget_set_margin_start (row, 6);
                gtk_widget_set_margin_end (row, 6);
                gtk_widget_set_margin_top (row, 3);
                gtk_widget_set_margin_bottom (row, 3);
                gtk_widget_show (row);
            }
            gtk_widget_show (custom_layout_list);
            mem_pool_destroy (&tmp);

            // Select first row
            GtkListBoxRow *first_row = gtk_list_box_get_row_at_index (GTK_LIST_BOX(custom_layout_list), 0);
            gtk_list_box_select_row (GTK_LIST_BOX(custom_layout_list), GTK_LIST_BOX_ROW(first_row));

            scrolled_custom_layout_list = gtk_scrolled_window_new (NULL, NULL);
            gtk_container_add (GTK_CONTAINER (scrolled_custom_layout_list), custom_layout_list);
        }
        gtk_widget_show (scrolled_custom_layout_list);

        GtkWidget *new_layout_button =
            intro_button_new ("document-new", "New Layout", "Create a layout based on an existing one.");
        GtkWidget *open_layout_button =
            intro_button_new ("document-open", "Open Layout", "Open an existing .xkb file.");
        GtkWidget *install_layout_button =
            intro_button_new ("document-save", "Install Layout", "Install an .xkb file into the system.");
        g_signal_connect (G_OBJECT(install_layout_button), "clicked", G_CALLBACK (install_layout_handler), NULL);

        GtkWidget *sidebar = gtk_grid_new ();
        gtk_grid_set_row_spacing (GTK_GRID(sidebar), 12);
        add_custom_css (sidebar, ".grid, grid { margin: 12px; }");
        gtk_grid_attach (GTK_GRID(sidebar), scrolled_custom_layout_list, 0, 0, 1, 1);
        gtk_grid_attach (GTK_GRID(sidebar), new_layout_button, 0, 1, 1, 1);
        gtk_grid_attach (GTK_GRID(sidebar), open_layout_button, 0, 2, 1, 1);
        gtk_grid_attach (GTK_GRID(sidebar), install_layout_button, 0, 3, 1, 1);
        gtk_widget_show (sidebar);

        GtkWidget *paned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
        // FIXME: The following CSS is used to work around 2 issues with
        // GtkPaned. One is a failed assert which is a bug in GTK (see
        // https://github.com/elementary/stylesheet/issues/328). The other
        // one is the vanishing of the separator, which seems to be related
        // to elementary OS using a negative margin in the separator.
        add_custom_css (paned, "paned > separator {"
                               "    margin-right: 0;"
                               "    min-width: 2px;"
                               "    min-height: 2px;"
                               "}");
        gtk_paned_pack1 (GTK_PANED(paned), sidebar, FALSE, FALSE);
        gtk_paned_pack2 (GTK_PANED(paned), keyboard, TRUE, TRUE);
        gtk_container_add(GTK_CONTAINER(window), paned);
        gtk_widget_show (paned);

        gtk_main();
    }

    xmlCleanupParser();

    return !success;
}
