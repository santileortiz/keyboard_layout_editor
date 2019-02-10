/*
 * Copiright (C) 2018 Santiago León O.
 */

#define _GNU_SOURCE // Used to enable strcasestr()
#include "common.h"
#include "xkb_keymap_installer.c"
#include "xkb_keymap_loader.c"
#include <xkbcommon/xkbcommon.h>
#include <linux/input-event-codes.h>
#include "keycode_names.h"
#include "keysym_names.h"

#include <locale.h>

#include <gtk/gtk.h>
#include "gresource.c"
#include "gtk_utils.c"
#include "fk_popover.c"

#include "keyboard_layout_editor.h"
struct kle_app_t app;

#include "keyboard_view_repr_store.h"
#include "keyboard_view_as_string.h"
#include "keyboard_view.c"
#include "keyboard_view_repr_store.c"
#include "keyboard_view_as_string.c"

#include "keyboard_layout.c"

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
            str_cat_full_path (&command, app.argv_0);
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
            str_cat_full_path (&command, app.argv_0);
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
            str_cat_full_path (&command, app.argv_0);
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

    GtkWidget *label = gtk_bin_get_child (GTK_BIN(row));
    const gchar *curr_layout = gtk_label_get_text (GTK_LABEL (label));
    keyboard_view_set_keymap (app.keyboard_view, curr_layout);
}

GtkWidget* new_custom_layout_list (char **custom_layouts, int num_custom_layouts)
{
    GtkWidget *custom_layout_list = gtk_list_box_new ();
    gtk_widget_set_vexpand (custom_layout_list, TRUE);
    gtk_widget_set_hexpand (custom_layout_list, TRUE);
    g_signal_connect (G_OBJECT(custom_layout_list), "row-selected", G_CALLBACK (on_custom_layout_selected), NULL);

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

    // Select first row
    GtkListBoxRow *first_row = gtk_list_box_get_row_at_index (GTK_LIST_BOX(custom_layout_list), 0);
    gtk_list_box_select_row (GTK_LIST_BOX(custom_layout_list), GTK_LIST_BOX_ROW(first_row));

    return custom_layout_list;
}

void transition_to_welcome_with_custom_layouts (char **custom_layouts, int num_custom_layouts);
void transition_to_welcome_with_no_custom_layouts (void);

// This is done from a callback queued by the button handler to let the main
// loop destroy the GtkFileChooserDialog before asking for authentication. If
// authentication was not required then this should not be necessary.
gboolean install_layout_callback (gpointer layout_path)
{
    bool success = unprivileged_xkb_keymap_install (layout_path);

    if (success) {
        mem_pool_t tmp = {0};
        char **custom_layouts;
        int num_custom_layouts;
        xkb_keymap_list (&tmp, &custom_layouts, &num_custom_layouts);

        // If installataion was successful then num_custom_layouts > 0.
        if (app.no_custom_layouts_welcome_view) {
            transition_to_welcome_with_custom_layouts (custom_layouts, num_custom_layouts);
        } else {
            GtkWidget *new_list = new_custom_layout_list (custom_layouts, num_custom_layouts);
            replace_wrapped_widget (&app.custom_layout_list, new_list);
        }

        mem_pool_destroy (&tmp);
    }
    g_free (layout_path);
    return G_SOURCE_REMOVE;
}

void install_layout_handler (GtkButton *button, gpointer   user_data)
{
    GtkWidget *dialog =
        gtk_file_chooser_dialog_new ("Install Layout",
                                     GTK_WINDOW(app.window),
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
    GtkListBoxRow *row = gtk_list_box_get_selected_row (GTK_LIST_BOX(app.custom_layout_list));
    GtkWidget *label = gtk_bin_get_child (GTK_BIN(row));
    const gchar *curr_layout = gtk_label_get_text (GTK_LABEL (label));

    if (unprivileged_xkb_keymap_uninstall (curr_layout)) {

        mem_pool_t tmp = {0};
        char **custom_layouts;
        int num_custom_layouts;
        xkb_keymap_list (&tmp, &custom_layouts, &num_custom_layouts);
        if (num_custom_layouts > 0) {
            GtkWidget *new_list = new_custom_layout_list (custom_layouts, num_custom_layouts);
            replace_wrapped_widget (&app.custom_layout_list, new_list);
        } else {
            transition_to_welcome_with_no_custom_layouts ();
        }
        mem_pool_destroy (&tmp);
    }
    return G_SOURCE_REMOVE;
}

FK_POPOVER_BUTTON_PRESSED_CB (set_key_symbol_handler)
{
    //TODO: Implement this!!!
}

void edit_symbol_popup_handler (GtkButton *button, gpointer user_data)
{
    GtkWidget *entry = gtk_search_entry_new ();
    gtk_widget_set_margins (entry, 6);
    gtk_entry_set_placeholder_text (GTK_ENTRY(entry), "Search keysym by name");

    GtkWidget *list = gtk_list_box_new ();
    gtk_widget_set_vexpand (list, TRUE);
    gtk_widget_set_hexpand (list, TRUE);
    //gtk_list_box_set_filter_func (GTK_LIST_BOX(list), search_filter, kv, NULL);

    bool first = true;
    for (int i=0; i < ARRAY_SIZE(keysym_names); i++)
    {
        if (keysym_names[i].name != NULL) {
            GtkWidget *row = gtk_label_new (keysym_names[i].name);
            gtk_container_add (GTK_CONTAINER(list), row);
            gtk_widget_set_halign (row, GTK_ALIGN_START);

            if (first) {
                first = false;
                GtkWidget *r = gtk_widget_get_parent (row);
                gtk_list_box_select_row (GTK_LIST_BOX(list), GTK_LIST_BOX_ROW(r));
            }

            gtk_widget_set_margin_start (row, 6);
            gtk_widget_set_margin_end (row, 6);
            gtk_widget_set_margin_top (row, 3);
            gtk_widget_set_margin_bottom (row, 3);
        }
    }
    GtkWidget *scrolled_list = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_disable_hscroll (GTK_SCROLLED_WINDOW(scrolled_list));
    gtk_scrolled_window_set_min_content_width (GTK_SCROLLED_WINDOW(scrolled_list), 200);
    gtk_scrolled_window_set_min_content_height (GTK_SCROLLED_WINDOW(scrolled_list), 100);
    gtk_container_add (GTK_CONTAINER (scrolled_list), list);
    GtkWidget *frame = gtk_frame_new (NULL);
    gtk_widget_set_margins (frame, 6);
    gtk_container_add (GTK_CONTAINER(frame), scrolled_list);

    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add (GTK_CONTAINER(box), entry);
    gtk_container_add (GTK_CONTAINER(box), frame);

    fk_popover_init (&app.edit_symbol_popover,
                     GTK_WIDGET(button), NULL,
                     NULL, box,
                     "Set", set_key_symbol_handler,
                     NULL);
}

GtkWidget* app_keys_sidebar_new (struct kle_app_t *app, int kc)
{
    mem_pool_t pool_l = {0};

    GtkWidget *grid = gtk_grid_new ();
    gtk_widget_set_size_request (grid, app->sidebar_min_width, 0);
    char *keycode_str = pprintf (&pool_l, "%d (%s)", kc, keycode_names[kc]);
    labeled_text_new_in_grid (GTK_GRID(grid), "Keycode:", keycode_str, 0, 0);

    GtkWidget *types_combobox;
    labeled_combobox_new_in_grid (GTK_GRID(grid), "Type:", 0, 1, &types_combobox);

    struct key_type_t *curr_type = app->keymap->types;
    while (curr_type != NULL) {
        combo_box_text_append_text_with_id (GTK_COMBO_BOX_TEXT(types_combobox), curr_type->name);
        curr_type = curr_type->next;
    }
    combo_box_text_append_text_with_id (GTK_COMBO_BOX_TEXT(types_combobox), "None");

    struct key_t *key = app->keymap->keys[kc];
    if (key != NULL && key->type != NULL) {
        gtk_combo_box_set_active_id (GTK_COMBO_BOX(types_combobox), key->type->name);

        GtkWidget *per_level_data = gtk_grid_new ();
        gtk_widget_set_halign (per_level_data, GTK_ALIGN_CENTER);
        gtk_widget_set_margins (per_level_data, 6);
        GtkWidget *symbol_title = title_label_new ("Symbol");
        gtk_widget_set_halign (symbol_title, GTK_ALIGN_CENTER);
        gtk_widget_set_margins (symbol_title, 6);
        GtkWidget *action_title = title_label_new ("Action");
        gtk_widget_set_halign (action_title, GTK_ALIGN_CENTER);
        gtk_widget_set_margins (action_title, 6);
        gtk_grid_attach (GTK_GRID(per_level_data), symbol_title, 1, 0, 1, 1);
        gtk_grid_attach (GTK_GRID(per_level_data), action_title, 2, 0, 1, 1);

        char keysym_name[64];
        for (int i=0; i<key->type->num_levels; i++) {
            char *level_str = pprintf (&pool_l, "Level %i", i+1);
            GtkWidget *level_label = title_label_new (level_str);
            gtk_widget_set_margins (level_label, 6);

            xkb_keysym_get_name (key->levels[i].keysym, keysym_name, ARRAY_SIZE(keysym_name));
            GtkWidget *symbol_name = gtk_label_new (keysym_name);
            gtk_widget_set_halign (symbol_name, GTK_ALIGN_CENTER);
            gtk_widget_set_hexpand (symbol_name, TRUE);
            gtk_label_set_ellipsize (GTK_LABEL(symbol_name), PANGO_ELLIPSIZE_END);
            GtkWidget *symbol_edit_button =
                icon_button_new ("edit-symbolic",
                                 "Modify assigned symbol",
                                 G_CALLBACK(edit_symbol_popup_handler), NULL);
            GtkWidget *symbol_wdgt = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
            gtk_widget_set_halign (symbol_wdgt, GTK_ALIGN_END);
            gtk_container_add (GTK_CONTAINER(symbol_wdgt), symbol_name);
            gtk_container_add (GTK_CONTAINER(symbol_wdgt), symbol_edit_button);
            gtk_widget_set_margins (symbol_wdgt, 6);
            gtk_widget_set_size_request (symbol_wdgt, 120, 0);

            GtkWidget *action_entry = gtk_entry_new ();
            gtk_entry_set_width_chars (GTK_ENTRY(action_entry), 10);
            gtk_widget_set_margins (action_entry, 6);
            gtk_grid_attach (GTK_GRID(per_level_data), level_label, 0, i+1, 1, 1);
            gtk_grid_attach (GTK_GRID(per_level_data), symbol_wdgt, 1, i+1, 1, 1);
            gtk_grid_attach (GTK_GRID(per_level_data), action_entry, 2, i+1, 1, 1);
        }

        gtk_grid_attach (GTK_GRID(grid), per_level_data, 0, 2, 2, 1);

    } else {
        gtk_combo_box_set_active_id (GTK_COMBO_BOX(types_combobox), "None");
    }

    mem_pool_destroy (&pool_l);

    return grid;
}

void build_welcome_screen_custom_layouts (char **custom_layouts, int num_custom_layouts);
void build_welcome_screen_no_custom_layouts ();
void return_to_welcome_handler (GtkButton *button, gpointer   user_data)
{
    mem_pool_t tmp = {0};
    char **custom_layouts;
    int num_custom_layouts = 0;
    xkb_keymap_list (&tmp, &custom_layouts, &num_custom_layouts);

    if (num_custom_layouts > 0) {
        gtk_window_resize (GTK_WINDOW(app.window), 1430, 570);
        build_welcome_screen_custom_layouts (custom_layouts, num_custom_layouts);
    } else {
        gtk_window_resize (GTK_WINDOW(app.window), 900, 570);
        build_welcome_screen_no_custom_layouts ();
    }
    mem_pool_destroy (&tmp);
}

void edit_layout_handler (GtkButton *button, gpointer user_data)
{
    GtkWidget *header_bar = gtk_header_bar_new ();
    gtk_header_bar_set_title (GTK_HEADER_BAR(header_bar), "Keyboard Editor");
    gtk_header_bar_set_show_close_button (GTK_HEADER_BAR(header_bar), TRUE);

    GtkWidget *return_to_welcome_button = gtk_button_new_with_label ("Go Back");
    gtk_widget_set_valign (return_to_welcome_button, GTK_ALIGN_CENTER);
    add_css_class (return_to_welcome_button, "back-button");
    g_signal_connect (return_to_welcome_button, "clicked", G_CALLBACK (return_to_welcome_handler), NULL);
    gtk_header_bar_pack_start (GTK_HEADER_BAR(header_bar), return_to_welcome_button);

    app.keyboard_grabbing_button = new_icon_button ("process-completed", G_CALLBACK(grab_input));
    gtk_header_bar_pack_start (GTK_HEADER_BAR(header_bar), app.keyboard_grabbing_button);

    gtk_window_set_titlebar (GTK_WINDOW(app.window), header_bar);
    gtk_widget_show_all (header_bar);

    GtkWidget *stack = gtk_stack_new ();
    gtk_widget_set_halign (stack, GTK_ALIGN_CENTER);
    {
        app.keymap = keyboard_layout_new_default ();

        app.keyboard_view->preview_mode = KV_PREVIEW_KEYS;
        app.keyboard_view->last_clicked_key = app.keyboard_view->first_row->first_key;
        gtk_widget_queue_draw (app.keyboard_view->widget);

        app.keys_sidebar = app_keys_sidebar_new (&app, app.keyboard_view->last_clicked_key->kc);
        gtk_stack_add_titled (GTK_STACK(stack), wrap_gtk_widget(app.keys_sidebar), "keys", "Keys");
    }

    {
        GtkWidget *types_stack = gtk_label_new ("Types");
        gtk_stack_add_titled (GTK_STACK(stack), types_stack, "types", "Types");
    }

    GtkWidget *stack_buttons = gtk_stack_switcher_new ();
    gtk_widget_set_halign (stack_buttons, GTK_ALIGN_CENTER);
    gtk_widget_set_margins (stack_buttons, 12);
    gtk_stack_switcher_set_stack (GTK_STACK_SWITCHER(stack_buttons), GTK_STACK(stack));

    GtkWidget *grid = gtk_grid_new ();
    gtk_widget_set_halign (grid, GTK_ALIGN_CENTER);
    gtk_grid_attach (GTK_GRID(grid), stack_buttons, 0, 0, 1, 1);
    gtk_grid_attach (GTK_GRID(grid), stack, 0, 1, 1, 1);
    replace_wrapped_widget (&app.sidebar, grid);
}

gboolean window_delete_handler (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    gtk_main_quit ();
    return FALSE;
}

// TODO: @requires:GTK_3.20
// TODO: Get better icon for this. I'm thinking a gripper grabbing/ungrabbing a
// key.
//#define DISABLE_GRABS
gboolean grab_input (GtkButton *button, gpointer user_data)
{
#ifndef DISABLE_GRABS
    GdkDisplay *disp = gdk_display_get_default ();
    app.gdk_seat = gdk_display_get_default_seat (disp);
    GdkWindow *gdk_window = gtk_widget_get_window (app.window);
    GdkGrabStatus status = gdk_seat_grab (app.gdk_seat,
                                          gdk_window,
                                          GDK_SEAT_CAPABILITY_ALL, // See @why_not_GDK_SEAT_CAPABILITY_KEYBOARD
                                          TRUE, // If this is FALSE we don't get any pointer events, why?
                                          NULL, NULL, NULL, NULL);
    if (status == GDK_GRAB_SUCCESS) {
        set_header_icon_button (&app.keyboard_grabbing_button, "media-playback-stop", G_CALLBACK(ungrab_input));
    }
#endif
    return G_SOURCE_REMOVE;
}

// TODO: @requires:GTK_3.20
gboolean ungrab_input (GtkButton *button, gpointer user_data)
{
#ifndef DISABLE_GRABS
    set_header_icon_button (&app.keyboard_grabbing_button, "process-completed", G_CALLBACK(grab_input));
    gdk_seat_ungrab (app.gdk_seat);
    app.gdk_seat = NULL;
#endif
    return G_SOURCE_REMOVE;
}

// NOTE: There can only be one Gdk event handler at a time. Currently we ony use
// it to detect GdkEventGrabBroken events and show the initial button.
void handle_grab_broken (GdkEvent *event, gpointer data)
{
    // NOTE: When debugging grabbing events we may completely freeze the system.
    // Better enable the following code to be able to get out pressing ESC in
    // case things go wrong.
#if 0
    // TODO: @requires:GTK_3.20
    if (event->type == GDK_KEY_PRESS ) {
        if (((GdkEventKey*)event)->keyval == GDK_KEY_Escape) {
            gdk_seat_ungrab (app.gdk_seat);
            app.gdk_seat = NULL;
            set_header_icon_button (&app.keyboard_grabbing_button, "process-completed", G_CALLBACK(grab_input));
        }
    }
#endif

    // FIXME: I don't know when these events are sent. I have not ever seen one
    // being received, so this codepath is untested (do we need to propagate
    // GdkGrabBroken to gtk?). I tried running 2 instances of the application,
    // taking a grab in one, and then taking it in the other. This hid the
    // instance that attempted to steal the grab, while it's process kept
    // running and had to be terminated with Ctrl+C. The grab made by the other
    // instance wasn't broken.
    //
    // @why_not_GDK_SEAT_CAPABILITY_KEYBOARD
    // If the grab is made for GDK_SEAT_CAPABILITY_KEYBOARD, then the user can
    // move the window by dragging the header bar. Doing this breaks the grab
    // but we don't get a GdkGrabBroken event, making impossible to update the
    // button with set_header_icon_button(). This is the reason why we grab
    // GDK_SEAT_CAPABILITY_ALL, freezing the window in place and not allowing
    // the grab to be broken. Also, from a UX perspective GDK_SEAT_CAPABILITY_ALL
    // may be the right choice to communicte to the user what a grab is.
    //
    // All tests were made in elementary OS Juno. It's possible the problem is
    // in Gala, maybe in Mutter, or I'm doing something wrong (I thought I was
    // missing an event mask but there is no mask for GdkGrabBroken). I need to
    // test in GNOME.
    if (event->type == GDK_GRAB_BROKEN) {
        set_header_icon_button (&app.keyboard_grabbing_button, "process-completed", G_CALLBACK(grab_input));
    } else {
        gtk_main_do_event (event);
    }
}

void on_sidebar_allocated (GtkWidget *widget, GdkRectangle *allocation, gpointer user_data)
{
    app.sidebar_min_width = allocation->width;
}

// Build a welcome screen that shows installed layouts and a preview when
// selected from a list.
void build_welcome_screen_custom_layouts (char **custom_layouts, int num_custom_layouts)
{
    app.no_custom_layouts_welcome_view = false;
    gdk_event_handler_set (handle_grab_broken, NULL, NULL);

    GtkWidget *header_bar = gtk_header_bar_new ();
    gtk_header_bar_set_title (GTK_HEADER_BAR(header_bar), "Keyboard Editor");
    gtk_header_bar_set_show_close_button (GTK_HEADER_BAR(header_bar), TRUE);

    app.keyboard_grabbing_button = new_icon_button ("process-completed", G_CALLBACK(grab_input));
    gtk_header_bar_pack_start (GTK_HEADER_BAR(header_bar), app.keyboard_grabbing_button);

    gtk_window_set_titlebar (GTK_WINDOW(app.window), header_bar);
    gtk_widget_show (header_bar);

    app.keyboard_view = keyboard_view_new_with_gui (app.window);

    GtkWidget *layout_list = gtk_frame_new (NULL);
    {
        GtkWidget *scrolled_custom_layout_list = gtk_scrolled_window_new (NULL, NULL);
        GtkWidget *new_list = new_custom_layout_list (custom_layouts, num_custom_layouts);
        app.custom_layout_list = new_list;
        gtk_container_add (GTK_CONTAINER (scrolled_custom_layout_list), wrap_gtk_widget(app.custom_layout_list));

        GtkWidget *remove_layout_button =
            gtk_button_new_from_icon_name ("list-remove-symbolic",
                                           GTK_ICON_SIZE_SMALL_TOOLBAR);
        gtk_widget_set_tooltip_text (remove_layout_button, "Uninstall the selected layout from the system");
        g_signal_connect (G_OBJECT(remove_layout_button), "clicked", G_CALLBACK(delete_layout_handler), NULL);

        GtkWidget *install_layout_button =
            gtk_button_new_from_icon_name ("list-add-symbolic",
                                           GTK_ICON_SIZE_SMALL_TOOLBAR);
        gtk_widget_set_tooltip_text (install_layout_button, "Install a custom layout from an .xkb file");
        g_signal_connect (G_OBJECT(install_layout_button), "clicked", G_CALLBACK(install_layout_handler), NULL);

        GtkWidget *edit_layout_button =
            gtk_button_new_from_icon_name ("edit-symbolic",
                                           GTK_ICON_SIZE_SMALL_TOOLBAR);
        gtk_widget_set_tooltip_text (edit_layout_button, "Edit the selected layout");
        g_signal_connect (G_OBJECT(edit_layout_button), "clicked", G_CALLBACK(edit_layout_handler), NULL);

        // TODO: Add an "update" button that reinstalls a layout if there are
        // changes between it's source file and the installed version.

        GtkWidget *bar = gtk_action_bar_new ();
        add_css_class (bar, "inline-toolbar");
        gtk_action_bar_pack_start (GTK_ACTION_BAR(bar), install_layout_button);
        gtk_action_bar_pack_start (GTK_ACTION_BAR(bar), remove_layout_button);
        gtk_action_bar_pack_end (GTK_ACTION_BAR(bar), edit_layout_button);

        GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
        gtk_container_add (GTK_CONTAINER(box), scrolled_custom_layout_list);
        gtk_container_add (GTK_CONTAINER(box), bar);

        gtk_container_add (GTK_CONTAINER(layout_list), box);
        gtk_widget_show_all (layout_list);
    }

    GtkWidget *new_layout_button =
        intro_button_new ("document-new", "New Layout", "Create a layout based on an existing one.");
    GtkWidget *open_layout_button =
        intro_button_new ("document-open", "Open Layout", "Open an existing .xkb file.");

    app.sidebar = gtk_grid_new ();
    g_signal_connect (app.sidebar, "size-allocate", G_CALLBACK(on_sidebar_allocated), NULL);
    gtk_grid_set_row_spacing (GTK_GRID(app.sidebar), 12);
    add_custom_css (app.sidebar, ".grid, grid { margin: 12px; }");
    gtk_grid_attach (GTK_GRID(app.sidebar), layout_list, 0, 0, 1, 1);
    gtk_grid_attach (GTK_GRID(app.sidebar), new_layout_button, 0, 1, 1, 1);
    gtk_grid_attach (GTK_GRID(app.sidebar), open_layout_button, 0, 2, 1, 1);

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
    gtk_paned_pack1 (GTK_PANED(paned), wrap_gtk_widget(app.sidebar), FALSE, FALSE);
    gtk_paned_pack2 (GTK_PANED(paned), app.keyboard_view->widget, TRUE, TRUE);

    GtkWidget *child = gtk_bin_get_child (GTK_BIN(app.window));
    if (child != NULL) {
        gtk_container_remove (GTK_CONTAINER(app.window), child);
    }
    gtk_container_add(GTK_CONTAINER(app.window), paned);
    gtk_widget_show_all (paned);
}

// Build a welcome screen with only introductory buttons and no list or preview
// of keyboards.
void build_welcome_screen_no_custom_layouts ()
{
    app.no_custom_layouts_welcome_view = true;

    GtkWidget *header_bar = gtk_header_bar_new ();
    gtk_header_bar_set_title (GTK_HEADER_BAR(header_bar), "Keyboard Editor");
    gtk_header_bar_set_show_close_button (GTK_HEADER_BAR(header_bar), TRUE);
    gtk_window_set_titlebar (GTK_WINDOW(app.window), header_bar);
    gtk_widget_show (header_bar);

    GtkWidget *no_custom_layouts_message;
    {
        no_custom_layouts_message = gtk_grid_new ();
        GtkWidget *title_label = gtk_label_new ("No Custom Keymaps");
        add_css_class (title_label, "h1");
        gtk_widget_set_halign (title_label, GTK_ALIGN_CENTER);
        gtk_grid_attach (GTK_GRID(no_custom_layouts_message),
                         title_label, 1, 0, 1, 1);

        GtkWidget *subtitle_label = gtk_label_new ("Open an .xkb file to edit it.");
        add_css_class (subtitle_label, "h2");
        add_css_class (subtitle_label, "dim-label");
        gtk_widget_set_halign (subtitle_label, GTK_ALIGN_CENTER);
        gtk_grid_attach (GTK_GRID(no_custom_layouts_message),
                         subtitle_label, 1, 1, 1, 1);
        gtk_widget_show_all (no_custom_layouts_message);
    }

    GtkWidget *new_layout_button =
        intro_button_new ("document-new", "New Layout", "Create a layout based on an existing one.");
    GtkWidget *open_layout_button =
        intro_button_new ("document-open", "Open Layout", "Open an existing .xkb file.");
    GtkWidget *install_layout_button =
        intro_button_new ("document-save", "Install Layout", "Install an .xkb file into the system.");
    g_signal_connect (G_OBJECT(install_layout_button), "clicked", G_CALLBACK (install_layout_handler), NULL);

    GtkWidget *sidebar = gtk_grid_new ();
    gtk_widget_set_halign (sidebar, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (sidebar, GTK_ALIGN_CENTER);
    gtk_grid_set_row_spacing (GTK_GRID(sidebar), 12);
    add_custom_css (sidebar, ".grid, grid { margin: 12px; }");
    gtk_grid_attach (GTK_GRID(sidebar), no_custom_layouts_message, 0, 0, 1, 1);
    gtk_grid_attach (GTK_GRID(sidebar), new_layout_button, 0, 1, 1, 1);
    gtk_grid_attach (GTK_GRID(sidebar), open_layout_button, 0, 2, 1, 1);
    gtk_grid_attach (GTK_GRID(sidebar), install_layout_button, 0, 3, 1, 1);
    gtk_widget_show (sidebar);

    GtkWidget *welcome_view = gtk_event_box_new ();
    add_css_class (welcome_view, "view");
    add_css_class (welcome_view, "welcome");
    gtk_widget_set_halign (welcome_view, GTK_ALIGN_FILL);
    gtk_widget_set_valign (welcome_view, GTK_ALIGN_FILL);
    gtk_container_add (GTK_CONTAINER(welcome_view), sidebar);
    gtk_widget_show (welcome_view);

    gtk_container_add(GTK_CONTAINER(app.window), welcome_view);
}

// There are two welcome screens, one for the case where there are already
// custom layouts installed, another for when there are not.
//
// TODO: Transitioning between both welcome screens is currently done by
// resetting everything an building the other view. It would be better to have a
// unique build_welcome_view() function that transitions nicely between both,
// maybe using animations.
void transition_to_welcome_with_custom_layouts (char **custom_layouts, int num_custom_layouts)
{
    assert (num_custom_layouts > 0);
    GtkWidget *child = gtk_bin_get_child (GTK_BIN (app.window));
    gtk_widget_destroy (child);
    window_resize_centered (app.window, 1430, 570);
    build_welcome_screen_custom_layouts (custom_layouts, num_custom_layouts);
}

void transition_to_welcome_with_no_custom_layouts (void)
{
    GtkWidget *child = gtk_bin_get_child (GTK_BIN (app.window));
    GtkWidget *header_bar = gtk_window_get_titlebar (GTK_WINDOW(app.window));
    gtk_container_foreach (GTK_CONTAINER(header_bar),
                           destroy_children_callback,
                           NULL);
    gtk_widget_destroy (child);
    window_resize_centered (app.window, 900, 570);
    build_welcome_screen_no_custom_layouts ();
}

string_t app_get_repr_path (struct kle_app_t *app)
{
    string_t path = str_new (app->user_dir);
    str_cat_c (&path, "/repr/");
    ensure_dir_exists (str_data(&path));
    return path;
}

int main (int argc, char *argv[])
{
    app = ZERO_INIT(struct kle_app_t);

    bool success = true;
    app.argv_0 = argv[0];
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
        init_keycode_names ();
        gtk_init(&argc, &argv);

        app.gresource = gresource_get_resource ();
        gtk_icon_theme_add_resource_path (gtk_icon_theme_get_default (),
                                          "/com/github/santileortiz/iconoscope/icons");

        // TODO: When we get a pool at the app level, put this path there,
        // currently it will leak, but I don't care ATM because it's small and
        // only allocated once. Will make Valgrind complain though.
        // @small_leaks
        app.user_dir = sh_expand ("~/.keys-data", NULL);
        ensure_dir_exists (app.user_dir);

        // TODO: Currently the only setting we have is the last used
        // representation name. So for now that's the only content of the
        // settings file. When we get more settings either start using a
        // .desktop formatted file, or use gsettings.
        // Also, the representation name will also leak ATM. @small_leaks
        string_t settings_file_path = str_new(app.user_dir);
        str_cat_c (&settings_file_path, "/settings");
        if (path_exists(str_data(&settings_file_path))) {
            app.selected_repr = full_file_read (NULL, str_data(&settings_file_path));
            // Make the string end at the first line break
            char *c = app.selected_repr;
            while (*c) {
                if (*c == '\n') {
                    *c = '\0';
                    break;
                }
                c++;
            }
        }
        str_free (&settings_file_path);

        mem_pool_t tmp = {0};
        char **custom_layouts;
        int num_custom_layouts = 0;
        xkb_keymap_list (&tmp, &custom_layouts, &num_custom_layouts);

        app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        g_signal_connect (G_OBJECT(app.window), "delete-event", G_CALLBACK (window_delete_handler), NULL);
        gtk_window_set_position(GTK_WINDOW(app.window), GTK_WIN_POS_CENTER);
        gtk_window_set_gravity (GTK_WINDOW(app.window), GDK_GRAVITY_CENTER);

        if (num_custom_layouts > 0) {
            gtk_window_resize (GTK_WINDOW(app.window), 1430, 570);
            build_welcome_screen_custom_layouts (custom_layouts, num_custom_layouts);
        } else {
            gtk_window_resize (GTK_WINDOW(app.window), 900, 570);
            build_welcome_screen_no_custom_layouts ();
        }
        gtk_widget_show (app.window);
        mem_pool_destroy (&tmp);

        // Custom CSS
        // TODO: How can I set this just to the combobox widget and not
        // globally? I tried but failed, I don't think I know my CSS selectors
        // well enough.
        add_global_css (".flat-combobox button {"
                        "   padding: 1px 1px;"
                        "   border-width: 0px;"
                        "   border-radius: 2.5px;"
                        "   background-color: @base_color;"
                        "   background-image: none;"
                        "   box-shadow: none;"
                        "}");

        add_global_css (".flat-combobox menu {"
                        "   padding: 1px 1px;"
                        "   border-width: 0px;"
                        "   border-radius: 2.5px;"
                        "   background-color: white;"
                        "   background-image: none;"
                        "   box-shadow: none;"
                        "}");

        gtk_main();

        if (app.keyboard_view != NULL) {
            keyboard_view_destroy (app.keyboard_view);
        }

#ifndef NDEBUG
        // When debugging the keyboard view, we usually create a test case that
        // fails, we execute an action and see how it behaves, then after
        // closing, changing code and compiling, we want to repeat the process
        // again. To make this test cycle faster, debug builds delete all
        // autosaves when the application is closed.
        //
        // TODO: Maybe only delete autosaves created in the current session, and
        // leave preexisting autosaves untouched.
        {
            string_t repr_path = app_get_repr_path (&app);
            size_t repr_path_len = str_len (&repr_path);

            DIR *d = opendir (str_data(&repr_path));
            if (d != NULL) {
                struct dirent *entry_info;
                while (read_dir (d, &entry_info)) {
                    if (entry_info->d_name[0] != '.' &&
                        g_str_has_suffix (entry_info->d_name, ".autosave.lrep")) {

                        str_put_c (&repr_path, repr_path_len, entry_info->d_name);
                        if (unlink (str_data (&repr_path)) != 0) {
                            printf ("Error deleting autosave: %s\n", strerror(errno));
                        }
                    }
                }

                closedir (d);

            } else {
                printf ("Error opening %s: %s\n", str_data(&repr_path), strerror(errno));
            }

            str_free (&repr_path);
        }
#endif

    }

    xmlCleanupParser();

    return !success;
}
