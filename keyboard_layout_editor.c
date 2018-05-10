/*
 * Copiright (C) 2018 Santiago León O.
 */

#include "common.h"
#include "xkb_keymap_installer.c"
#include "xkb_keymap_loader.c"
#include <xkbcommon/xkbcommon.h>
#include <linux/input-event-codes.h>

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

struct xkb_context *xkb_ctx = NULL;
struct xkb_keymap *xkb_keymap = NULL;
struct xkb_state *xkb_state = NULL;

GtkWidget *window = NULL;
GtkWidget *keyboard = NULL;
GtkWidget *custom_layout_list = NULL;

struct key_t {
    int kc; //keycode
    float width; // normalized to default_key_size
    struct key_t *next_key;
};

struct row_t {
    float height; // normalized to default_key_size
    struct row_t *next_row;

    struct key_t *first_key;
    struct key_t *last_key;
};

struct keyboard_t {
    float default_key_size; // In pixels
    struct row_t *first_row;
    struct row_t *last_row;
};

#define kbd_new_row(pool,kbd) kbd_new_row_h(pool,kbd,1)
void kbd_new_row_h (mem_pool_t *pool, struct keyboard_t *kbd, float height)
{
    struct row_t *new_row = (struct row_t*)pom_push_struct (pool, struct row_t);
    *new_row = (struct row_t){0};
    new_row->height = height;

    if (kbd->last_row != NULL) {
        kbd->last_row->next_row = new_row;
        kbd->last_row = new_row;

    } else {
        kbd->first_row = new_row;
        kbd->last_row = new_row;
    }
}

#define kbd_add_key(pool,kbd,keycode) kbd_add_key_w(pool,kbd,keycode,1)
void kbd_add_key_w (mem_pool_t *pool, struct keyboard_t *kbd, int keycode, float width)
{
    struct key_t *new_key = (struct key_t*)pom_push_struct (pool, struct key_t);
    *new_key = (struct key_t){0};
    new_key->width = width;
    new_key->kc = keycode;

    struct row_t *curr_row = kbd->last_row;
    assert (curr_row != NULL && "Must create a row before adding a key.");

    if (curr_row->last_key != NULL) {
        curr_row->last_key->next_key = new_key;
        curr_row->last_key = new_key;

    } else {
        curr_row->first_key = new_key;
        curr_row->last_key = new_key;
    }
}

// Simple keyboard geometry.
// NOTE: Keycodes are used as defined in the linux kernel. To translate them
// into X11 keycodes offset them by 8 (x11_kc = kc+8).
struct keyboard_t* build_keyboard (mem_pool_t *pool)
{
    struct keyboard_t *result = (struct keyboard_t*)pom_push_struct (pool, struct keyboard_t);
    *result = (struct keyboard_t){0};
    result->default_key_size = 60;
    kbd_new_row (pool, result);
    kbd_add_key (pool, result, KEY_ESC);
    kbd_add_key (pool, result, KEY_F1);
    kbd_add_key (pool, result, KEY_F2);
    kbd_add_key (pool, result, KEY_F3);
    kbd_add_key (pool, result, KEY_F4);
    kbd_add_key (pool, result, KEY_F5);
    kbd_add_key (pool, result, KEY_F6);
    kbd_add_key (pool, result, KEY_F7);
    kbd_add_key (pool, result, KEY_F8);
    kbd_add_key (pool, result, KEY_F9);
    kbd_add_key (pool, result, KEY_F10);
    kbd_add_key (pool, result, KEY_F11);
    kbd_add_key (pool, result, KEY_F12);
    kbd_add_key (pool, result, KEY_NUMLOCK);
    kbd_add_key (pool, result, KEY_SCROLLLOCK);
    kbd_add_key (pool, result, KEY_INSERT);

    kbd_new_row (pool, result);
    kbd_add_key (pool, result, KEY_GRAVE);
    kbd_add_key (pool, result, KEY_1);
    kbd_add_key (pool, result, KEY_2);
    kbd_add_key (pool, result, KEY_3);
    kbd_add_key (pool, result, KEY_4);
    kbd_add_key (pool, result, KEY_5);
    kbd_add_key (pool, result, KEY_6);
    kbd_add_key (pool, result, KEY_7);
    kbd_add_key (pool, result, KEY_8);
    kbd_add_key (pool, result, KEY_9);
    kbd_add_key (pool, result, KEY_0);
    kbd_add_key (pool, result, KEY_MINUS);
    kbd_add_key (pool, result, KEY_EQUAL);
    kbd_add_key_w (pool, result, KEY_BACKSPACE, 2);
    kbd_add_key (pool, result, KEY_HOME);

    kbd_new_row (pool, result);
    kbd_add_key_w (pool, result, KEY_TAB, 1.5);
    kbd_add_key (pool, result, KEY_Q);
    kbd_add_key (pool, result, KEY_W);
    kbd_add_key (pool, result, KEY_E);
    kbd_add_key (pool, result, KEY_R);
    kbd_add_key (pool, result, KEY_T);
    kbd_add_key (pool, result, KEY_Y);
    kbd_add_key (pool, result, KEY_U);
    kbd_add_key (pool, result, KEY_I);
    kbd_add_key (pool, result, KEY_O);
    kbd_add_key (pool, result, KEY_P);
    kbd_add_key (pool, result, KEY_LEFTBRACE);
    kbd_add_key (pool, result, KEY_RIGHTBRACE);
    kbd_add_key_w (pool, result, KEY_BACKSLASH, 1.5);
    kbd_add_key (pool, result, KEY_PAGEUP);

    kbd_new_row (pool, result);
    kbd_add_key_w (pool, result, KEY_CAPSLOCK, 1.75);
    kbd_add_key (pool, result, KEY_A);
    kbd_add_key (pool, result, KEY_S);
    kbd_add_key (pool, result, KEY_D);
    kbd_add_key (pool, result, KEY_F);
    kbd_add_key (pool, result, KEY_G);
    kbd_add_key (pool, result, KEY_H);
    kbd_add_key (pool, result, KEY_J);
    kbd_add_key (pool, result, KEY_K);
    kbd_add_key (pool, result, KEY_L);
    kbd_add_key (pool, result, KEY_SEMICOLON);
    kbd_add_key (pool, result, KEY_APOSTROPHE);
    kbd_add_key_w (pool, result, KEY_ENTER, 2.25);
    kbd_add_key (pool, result, KEY_PAGEDOWN);

    kbd_new_row (pool, result);
    kbd_add_key_w (pool, result, KEY_LEFTSHIFT, 2.25);
    kbd_add_key (pool, result, KEY_Z);
    kbd_add_key (pool, result, KEY_X);
    kbd_add_key (pool, result, KEY_C);
    kbd_add_key (pool, result, KEY_V);
    kbd_add_key (pool, result, KEY_B);
    kbd_add_key (pool, result, KEY_N);
    kbd_add_key (pool, result, KEY_M);
    kbd_add_key (pool, result, KEY_COMMA);
    kbd_add_key (pool, result, KEY_DOT);
    kbd_add_key (pool, result, KEY_SLASH);
    kbd_add_key_w (pool, result, KEY_RIGHTSHIFT, 1.75);
    kbd_add_key (pool, result, KEY_UP);
    kbd_add_key (pool, result, KEY_END);

    kbd_new_row (pool, result);
    kbd_add_key_w (pool, result, KEY_LEFTCTRL, 1.25);
    kbd_add_key_w (pool, result, KEY_LEFTMETA, 1.25);
    kbd_add_key_w (pool, result, KEY_LEFTALT, 1.25);
    kbd_add_key_w (pool, result, KEY_SPACE, 6.25);
    kbd_add_key (pool, result, KEY_RIGHTALT);
    // NOTE: Fn key should be handled carefuly, most of the times it won't produce any keycode.
    kbd_add_key (pool, result, KEY_FN);
    kbd_add_key (pool, result, KEY_RIGHTCTRL);
    kbd_add_key (pool, result, KEY_LEFT);
    kbd_add_key (pool, result, KEY_DOWN);
    kbd_add_key (pool, result, KEY_RIGHT);
    return result;
}

gboolean render_keyboard (GtkWidget *widget, cairo_t *cr, gpointer data)
{
    cairo_set_source_rgba (cr, 1, 1, 1, 1);
    cairo_paint(cr);

    cairo_set_line_width (cr, 2);
    cairo_set_source_rgb (cr, 0, 0, 0);
    mem_pool_t pool = {0};
    struct keyboard_t *kbd = build_keyboard (&pool);
    struct row_t *curr_row = kbd->first_row;

    float y_pos = 1;
    while (curr_row != NULL) {
        struct key_t *curr_key = curr_row->first_key;
        float x_pos = 1;
        while (curr_key != NULL) {
            cairo_rectangle (cr, x_pos, y_pos,
                             curr_key->width*kbd->default_key_size,
                             curr_row->height*kbd->default_key_size);
            cairo_stroke (cr);
            x_pos += curr_key->width*kbd->default_key_size;
            curr_key = curr_key->next_key;
        }

        y_pos += curr_row->height*kbd->default_key_size;
        curr_row = curr_row->next_row;
    }

    xkb_keysym_t keysym = xkb_state_key_get_one_sym(xkb_state, 66);
    char keysym_name[64];
    xkb_keysym_get_name(keysym, keysym_name, sizeof(keysym_name));
    printf ("%.*s\n", (int)sizeof(keysym_name), keysym_name);

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

void on_custom_layout_selected (GtkListBox *box, GtkListBoxRow *row, gpointer user_data)
{
    if (row == NULL) {
        return;
    }

    if (xkb_ctx != NULL) {
        xkb_context_unref(xkb_ctx);
    }

    if (xkb_keymap != NULL) {
        xkb_keymap_unref(xkb_keymap);
    }

    if (xkb_state != NULL) {
        xkb_state_unref(xkb_state);
    }

    mem_pool_t pool = {0};
    GtkWidget *label = gtk_bin_get_child (GTK_BIN(row));
    const gchar *curr_layout = gtk_label_get_text (GTK_LABEL (label));
    char *keymap_str = reconstruct_installed_custom_layout (&pool, curr_layout);

    xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!xkb_ctx) {
        printf ("Error creating xkb_context.\n");
    }

    xkb_keymap = xkb_keymap_new_from_string (xkb_ctx, keymap_str,
                                             XKB_KEYMAP_FORMAT_TEXT_V1,
                                             XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!xkb_keymap) {
        printf ("Error creating xkb_keymap.\n");
    }

    xkb_state = xkb_state_new(xkb_keymap);
    if (!xkb_state) {
        printf ("Error creating xkb_state.\n");
    }

    gtk_widget_queue_draw (keyboard);

    mem_pool_destroy (&pool);
}

void set_custom_layouts_list (GtkWidget **custom_layout_list)
{
    GtkWidget *parent = NULL;
    if (*custom_layout_list != NULL) {
        parent = gtk_widget_get_parent (*custom_layout_list);
        gtk_container_remove (GTK_CONTAINER(parent), *custom_layout_list);
    }

    *custom_layout_list = gtk_list_box_new ();
    gtk_widget_set_vexpand (*custom_layout_list, TRUE);
    gtk_widget_set_hexpand (*custom_layout_list, TRUE);
    g_signal_connect (G_OBJECT(*custom_layout_list), "row-selected", G_CALLBACK (on_custom_layout_selected), NULL);

    mem_pool_t tmp = {0};
    char **custom_layouts;
    int num_custom_layouts;
    xkb_keymap_list (&tmp, &custom_layouts, &num_custom_layouts);

    // Create rows
    int i;
    for (i=0; i < num_custom_layouts; i++) {
        GtkWidget *row = gtk_label_new (custom_layouts[i]);
        gtk_container_add (GTK_CONTAINER(*custom_layout_list), row);
        gtk_widget_set_halign (row, GTK_ALIGN_START);

        gtk_widget_set_margin_start (row, 6);
        gtk_widget_set_margin_end (row, 6);
        gtk_widget_set_margin_top (row, 3);
        gtk_widget_set_margin_bottom (row, 3);
        gtk_widget_show (row);
    }
    gtk_widget_show (*custom_layout_list);
    mem_pool_destroy (&tmp);

    // Select first row
    GtkListBoxRow *first_row = gtk_list_box_get_row_at_index (GTK_LIST_BOX(*custom_layout_list), 0);
    gtk_list_box_select_row (GTK_LIST_BOX(*custom_layout_list), GTK_LIST_BOX_ROW(first_row));

    // If we are updating the list then reparent ourselves
    if (parent != NULL) {
        gtk_container_add (GTK_CONTAINER(parent), *custom_layout_list);
    }
}

// This is done from a callback queued by the button handler to let the main
// loop destroy the GtkFileChooserDialog before asking for authentication. If
// authentication was not required then this should not be necessary.
gboolean install_layout_callback (gpointer layout_path)
{
    unprivileged_xkb_keymap_install (layout_path);
    g_free (layout_path);

    set_custom_layouts_list (&custom_layout_list);
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
    set_custom_layouts_list (&custom_layout_list);
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
        gtk_window_resize (GTK_WINDOW(window), 1320, 570);
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

        keyboard = gtk_drawing_area_new ();
        gtk_widget_set_vexpand (keyboard, TRUE);
        gtk_widget_set_hexpand (keyboard, TRUE);
        g_signal_connect (G_OBJECT (keyboard), "draw", G_CALLBACK (render_keyboard), NULL);
        gtk_widget_show (keyboard);

        GtkWidget *scrolled_custom_layout_list = gtk_scrolled_window_new (NULL, NULL);
        set_custom_layouts_list (&custom_layout_list);
        gtk_container_add (GTK_CONTAINER (scrolled_custom_layout_list), custom_layout_list);
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
