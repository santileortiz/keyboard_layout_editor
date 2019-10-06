/*
 * Copiright (C) 2018 Santiago León O.
 */

#define _GNU_SOURCE // Used to enable strcasestr()
#include "common.h"
#include "bit_operations.c"
#include "status.c"
#include "scanner.c"
#include "cli_parser.c"
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
#include "xkb_keymap_loader.c"

#include "settings.h"

struct kle_app_t {
    mem_pool_t pool;

    int argc;
    char** argv;

    GtkWidget *window;
    struct keyboard_view_t *keyboard_view;

    // These are not string_t because they are not supposed to change at
    // runtime. We only set them at startup. Also, we just don't use hardcoded
    // macros because we want to compute absolute paths at startup.
    char *user_dir;
    char *repr_path;
    char *settings_file_path;
    char *selected_repr;

    struct keyboard_layout_t *keymap;

    string_t curr_keymap_name;
    string_t curr_xkb_str;

    int sidebar_min_width;

    // TODO: This will become an enum when we implement different states like
    // EDIT_KEYS, EDIT_TYPES, etc.
    bool is_edit_mode;

    // UI widgets that change
    GtkWidget *header_bar;
    GtkWidget *headerbar_buttons;
    GtkWidget *keymap_test_button;
    GtkWidget *window_content;
    GtkWidget *custom_layout_list;
    GtkWidget *sidebar;
    GtkWidget *keys_sidebar;
    struct fk_popover_t edit_symbol_popover;
    struct fk_searchable_list_t keysym_lookup_ui;
};

struct kle_app_t app;

// It seems like when calling pkexec we must always use absolute paths. We use
// this function to get full paths from whatever the user passes in the CLI.
// Maybe this is a configuration settiong in the policy file for pkexec, related
// to the place the binary will be executed in.
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
bool unprivileged_xkb_keymap_install (char *keymap_path, struct keyboard_layout_info_t *info)
{
    bool success = true;
    if (!xkb_keymap_install (keymap_path, info)) {
        if (errno == EACCES) {
            string_t command = str_new ("pkexec ");
            str_cat_full_path (&command, app.argv[0]);
            str_cat_c (&command, " --install ");
            str_cat_full_path (&command, keymap_path);

            // Some option arguments must be quoted to support spaces in them,
            // like the description field for example. This code assumes all
            // following options have arguments and automatically quote the
            // arguments.
            for (int i=3; i<app.argc; i+=2) {
                str_cat_printf (&command, " %s", app.argv[i]);

                if (i+1 < app.argc) {
                    str_cat_printf (&command, " \"%s\"", app.argv[i+1]);
                }
            }

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
bool unprivileged_xkb_keymap_uninstall (char *layout_name)
{
    bool success = true;
    if (!xkb_keymap_uninstall (layout_name)) {
        if (errno == EACCES) {
            string_t command = str_new ("pkexec ");
            str_cat_full_path (&command, app.argv[0]);
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
            str_cat_full_path (&command, app.argv[0]);
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

    string_t xkb_str = reconstruct_installed_custom_layout_str (curr_layout);

    if (keyboard_view_set_keymap (app.keyboard_view, curr_layout, str_data(&xkb_str))) {
        str_free (&app.curr_xkb_str);
        app.curr_xkb_str = xkb_str;

        str_set (&app.curr_keymap_name, curr_layout);
    }
}

GtkWidget* new_custom_layout_list (struct keyboard_layout_info_t *custom_layouts, int num_custom_layouts)
{
    GtkWidget *custom_layout_list = gtk_list_box_new ();
    gtk_widget_set_vexpand (custom_layout_list, TRUE);
    gtk_widget_set_hexpand (custom_layout_list, TRUE);

    // Create rows
    int i;
    for (i=0; i < num_custom_layouts; i++) {
        GtkWidget *row = gtk_label_new (custom_layouts[i].name);
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
    // NOTE: I previously selected the row before connecting to the row-selected
    // event, because for some reason the call to gtk_widget_show_all() on
    // startup, triggers the row-selected event. I wanted to avoid the callback
    // being called here once and then twice there. The problem is this fails
    // to trigger the row-selected event when the user edits a layout and then
    // goes back to the installed layout list.
    //
    // It would be nice not to trigger this twice on startup and not break
    // things somewhere else. I don't have time to fight GTK so it will be this
    // way for now.
    g_signal_connect (G_OBJECT(custom_layout_list), "row-selected", G_CALLBACK (on_custom_layout_selected), NULL);
    GtkListBoxRow *first_row = gtk_list_box_get_row_at_index (GTK_LIST_BOX(custom_layout_list), 0);
    gtk_list_box_select_row (GTK_LIST_BOX(custom_layout_list), GTK_LIST_BOX_ROW(first_row));

    return custom_layout_list;
}

// This is done from a callback queued by the button handler to let the main
// loop destroy the GtkFileChooserDialog before asking for authentication. If
// authentication was not required then this should not be necessary.
GtkWidget* new_welcome_screen_custom_layouts (struct keyboard_layout_info_t *custom_layouts, int num_custom_layouts);
gboolean install_layout_callback (gpointer layout_path)
{
    // TODO: If the file to be installed doesn't have layout information as a
    // comment at the start we should open a dialog so the user can pass this
    // data. Really, what needs to happen is that we install directly from our
    // IR and not from a file path.
    bool success = unprivileged_xkb_keymap_install (layout_path, NULL);

    if (success) {
        mem_pool_t tmp = {0};
        struct keyboard_layout_info_t *custom_layouts;
        int num_custom_layouts;
        xkb_keymap_list (&tmp, &custom_layouts, &num_custom_layouts);

        // There are two welcome screens, one for the case where there are already
        // custom layouts installed, another for when there are not.
        //
        // TODO: Maybe animate this?
        if (num_custom_layouts == 1) {
            GtkWidget *welcome_screen = new_welcome_screen_custom_layouts (custom_layouts, num_custom_layouts);
            replace_wrapped_widget (&app.window_content, welcome_screen);
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

GtkWidget* new_welcome_screen_no_custom_layouts ();
gboolean delete_layout_handler (GtkButton *button, gpointer user_data)
{
    GtkListBoxRow *row = gtk_list_box_get_selected_row (GTK_LIST_BOX(app.custom_layout_list));
    GtkWidget *label = gtk_bin_get_child (GTK_BIN(row));
    gchar *curr_layout = (gchar*)gtk_label_get_text (GTK_LABEL (label));

    if (unprivileged_xkb_keymap_uninstall (curr_layout)) {

        mem_pool_t tmp = {0};
        struct keyboard_layout_info_t *custom_layouts;
        int num_custom_layouts;
        xkb_keymap_list (&tmp, &custom_layouts, &num_custom_layouts);
        if (num_custom_layouts > 0) {
            GtkWidget *new_list = new_custom_layout_list (custom_layouts, num_custom_layouts);
            replace_wrapped_widget (&app.custom_layout_list, new_list);
        } else {
            GtkWidget *header_bar = gtk_window_get_titlebar (GTK_WINDOW(app.window));
            gtk_container_foreach (GTK_CONTAINER(header_bar),
                                   destroy_children_callback,
                                   NULL);

            GtkWidget *welcome_screen = new_welcome_screen_no_custom_layouts ();
            replace_wrapped_widget (&app.window_content, welcome_screen);
        }
        mem_pool_destroy (&tmp);
    }
    return G_SOURCE_REMOVE;
}

GtkWidget* app_keys_sidebar_new (struct kle_app_t *app, int kc);
FK_POPOVER_BUTTON_PRESSED_CB (set_key_symbol_handler)
{
    bool keysym_set = false;
    GtkListBoxRow *row = gtk_list_box_get_selected_row (GTK_LIST_BOX(app.keysym_lookup_ui.list));

    // NOTE: Calling gtk_widget_is_visible() on the row does not work. Looks
    // like GtkListBox stores visibility in their own private property, not the
    // one inherited from GtkWidget. :GtkListBoxHasPrivateVisibility
    if (gtk_widget_get_child_visible (GTK_WIDGET(row))) {
        GtkWidget *row_label = gtk_bin_get_child (GTK_BIN(row));
        const char *keysym_name = gtk_label_get_text (GTK_LABEL(row_label));

        struct key_level_t *level = (struct key_level_t*)user_data;
        level->keysym = xkb_keysym_from_name(keysym_name, XKB_KEYSYM_NO_FLAGS);
        keysym_set = true;

    } else {
        const char *search = gtk_entry_get_text (GTK_ENTRY(app.keysym_lookup_ui.search_entry));

        uint32_t cp;
        xkb_keysym_t keysym;
        if (parse_unicode_str (search, &cp) && codepoint_to_xkb_keysym (cp, &keysym)) {
            struct key_level_t *level = (struct key_level_t*)user_data;
            level->keysym = keysym;
            keysym_set = true;
        }
    }

    if (keysym_set)  {
        GtkWidget *keys_sidebar = app_keys_sidebar_new (&app, app.keyboard_view->preview_keys_selection->kc);
        replace_wrapped_widget_deferred (&app.keys_sidebar, keys_sidebar);
    }
}

void edit_symbol_popup_handler (GtkButton *button, gpointer user_data)
{
    GtkWidget *search_entry, *list;
    fk_searchable_list_init (&app.keysym_lookup_ui,
                             "Search keysym by name",
                             &search_entry, &list);
    fk_populate_list (&app.keysym_lookup_ui, keysym_names[i].name, ARRAY_SIZE(keysym_names));

    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add (GTK_CONTAINER(box), search_entry);
    gtk_container_add (GTK_CONTAINER(box), list);

    fk_popover_init (&app.edit_symbol_popover,
                     GTK_WIDGET(button), NULL,
                     NULL, box,
                     "Set", set_key_symbol_handler,
                     user_data);
}

void on_key_type_changed (GtkComboBox *themes_combobox, gpointer user_data)
{
    const char* type_name = gtk_combo_box_get_active_id (themes_combobox);

    struct key_type_t *curr_type = app.keymap->types;
    while (curr_type != NULL) {
        if (strcmp(str_data(&curr_type->name), type_name) == 0) {
            break;
        }

        curr_type = curr_type->next;
    }
    // NOTE: curr_type can be NULL, this means we assigned the "None" type, or
    // rather, we unassigned the type for this key.
    // TODO: Maybe this is not a good convention and we should have a proper
    // "None" type?

    // Update the type in the selected key, or create a key in the keymap if
    // there is none assigned yet.
    int kc = app.keyboard_view->preview_keys_selection->kc;
    struct key_t *key = app.keymap->keys[kc];
    if (key != NULL) {
        key->type = curr_type;

    } else {
        struct key_t *new_key = keyboard_layout_new_key (app.keymap, kc, curr_type);
        app.keymap->keys[kc] = new_key;
    }

    GtkWidget *keys_sidebar = app_keys_sidebar_new (&app, app.keyboard_view->preview_keys_selection->kc);
    replace_wrapped_widget_deferred (&app.keys_sidebar, keys_sidebar);
}

GtkWidget* app_keys_sidebar_new (struct kle_app_t *app, int kc)
{
    mem_pool_t pool_l = {0};

    GtkWidget *grid = gtk_grid_new ();
    gtk_widget_set_size_request (grid, app->sidebar_min_width, 0);
    char *keycode_str = pprintf (&pool_l, "%d (%s)", kc, kernel_keycode_names[kc]);
    labeled_text_new_in_grid (GTK_GRID(grid), "Keycode:", keycode_str, 0, 0);

    GtkWidget *types_combobox;
    labeled_combobox_new_in_grid (GTK_GRID(grid), "Type:", 0, 1, &types_combobox);

    struct key_type_t *curr_type = app->keymap->types;
    while (curr_type != NULL) {
        combo_box_text_append_text_with_id (GTK_COMBO_BOX_TEXT(types_combobox), str_data(&curr_type->name));
        curr_type = curr_type->next;
    }
    combo_box_text_append_text_with_id (GTK_COMBO_BOX_TEXT(types_combobox), "None");

    struct key_t *key = app->keymap->keys[kc];
    if (key != NULL && key->type != NULL) {
        gtk_combo_box_set_active_id (GTK_COMBO_BOX(types_combobox), str_data(&key->type->name));

        GtkWidget *per_level_data = gtk_grid_new ();
        gtk_widget_set_halign (per_level_data, GTK_ALIGN_CENTER);
        gtk_widget_set_margins (per_level_data, 6);
        GtkWidget *symbol_title = title_label_new ("Symbol");
        gtk_widget_set_halign (symbol_title, GTK_ALIGN_CENTER);
        gtk_widget_set_margins (symbol_title, 6);
        gtk_grid_attach (GTK_GRID(per_level_data), symbol_title, 1, 0, 1, 1);

        int num_levels = keyboard_layout_type_get_num_levels (key->type);
        char keysym_name[64];
        for (int i=0; i<num_levels; i++) {
            char *level_str = pprintf (&pool_l, "Level %i", i+1);
            GtkWidget *level_label = title_label_new (level_str);
            gtk_widget_set_margins (level_label, 6);

            xkb_keysym_get_name (key->levels[i].keysym, keysym_name, ARRAY_SIZE(keysym_name));
            GtkWidget *symbol_name = gtk_label_new (keysym_name);
            gtk_widget_set_halign (symbol_name, GTK_ALIGN_CENTER);
            gtk_widget_set_hexpand (symbol_name, TRUE);
            gtk_label_set_ellipsize (GTK_LABEL(symbol_name), PANGO_ELLIPSIZE_END);
            GtkWidget *symbol_edit_button =
                small_icon_button_new ("edit-symbolic",
                                       "Modify assigned symbol",
                                       G_CALLBACK(edit_symbol_popup_handler), &key->levels[i]);
            GtkWidget *symbol_wdgt = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
            gtk_widget_set_halign (symbol_wdgt, GTK_ALIGN_END);
            gtk_container_add (GTK_CONTAINER(symbol_wdgt), symbol_name);
            gtk_container_add (GTK_CONTAINER(symbol_wdgt), symbol_edit_button);
            gtk_widget_set_margins (symbol_wdgt, 6);
            gtk_widget_set_size_request (symbol_wdgt, 120, 0);

            gtk_grid_attach (GTK_GRID(per_level_data), level_label, 0, i+1, 1, 1);
            gtk_grid_attach (GTK_GRID(per_level_data), symbol_wdgt, 1, i+1, 1, 1);
        }

        gtk_grid_attach (GTK_GRID(grid), per_level_data, 0, 2, 2, 1);

    } else {
        gtk_combo_box_set_active_id (GTK_COMBO_BOX(types_combobox), "None");
    }

    // NOTE: This comes after gtk_combo_box_set_active_id() so we don't call the
    // handler unnecesarily. And avoid a infinite loop because
    // on_key_type_changed() also calls app_keys_sidebar_new().
    g_signal_connect (G_OBJECT(types_combobox), "changed", G_CALLBACK (on_key_type_changed), NULL);

    mem_pool_destroy (&pool_l);

    return grid;
}

/////////////////////////////
// keyboard_view callbacks
GtkWidget *new_keymap_stop_test_button ();
KV_GRAB_NOTIFY_CB(show_grabbed_input_state)
{
    replace_wrapped_widget (&app.keymap_test_button, new_keymap_stop_test_button ());
}

GtkWidget *new_keymap_test_button ();
KV_GRAB_NOTIFY_CB(show_ungrabbed_input_state)
{
    replace_wrapped_widget (&app.keymap_test_button, new_keymap_test_button ());
}

KV_SELECT_KEY_CHANGE_NOTIFY_CB(on_selected_key_change)
{
    GtkWidget *keys_sidebar = app_keys_sidebar_new (&app, kc);
    replace_wrapped_widget (&app.keys_sidebar, keys_sidebar);
}
/////////////////////////////

// TODO: Get better icon for this. I'm thinking a gripper grabbing/ungrabbing a
// key.
void on_grab_input_button (GtkButton *button, gpointer user_data)
{
    if (grab_input (app.window)) {
        show_grabbed_input_state (&app);
    }

    if (app.is_edit_mode == true) {
        kv_set_preview_test (app.keyboard_view);
    }
}

void on_ungrab_input_button (GtkButton *button, gpointer user_data)
{
    ungrab_input ();

    show_ungrabbed_input_state (&app);

    if (app.is_edit_mode == true) {
        kv_set_preview_keys (app.keyboard_view);
    }
}

// TODO: Almost everywhere we call new_keymap_test_button() we need to do:
//
//      app.keymap_test_button = new_keymap_test_button ();
//
// It may look sensible to set app.keymap_test_button inside
// new_keymap_test_button() but it does not work, because when coming back to
// the default state, we do:
//
//      replace_wrapped_widget (&app.keymap_test_button, new_keymap_test_button ());
//
// If we set app.keymap_test_button inside new_keymap_test_button() then we will
// try to unparent a widget that has not yet been parented (the new button).
//
// This is kind of confusing, so maybe it's worth it to create an abstraction
// arround it. Something like fk_two_state_button(), that wraps this up, I'm
// just not sure if it's worthwhile, or how less confusing it would get, maybe
// it gets worse?.
//
// FIXME: There is an issue with this button where the first click after the
// icon changes will be ignored unless the mouse is moved before clicking again,
// this seems to be a GTK issue and I really don't have time to dig deeper.
GtkWidget *new_keymap_test_button ()
{
    return new_icon_button ("process-completed", "Test layout", G_CALLBACK(on_grab_input_button));
}

GtkWidget *new_keymap_stop_test_button ()
{
    return new_icon_button ("media-playback-stop", "Stop testing layout", G_CALLBACK(on_ungrab_input_button));
}

GtkWidget* new_welcome_sidebar (struct keyboard_layout_info_t *custom_layouts, int num_custom_layouts);
void return_to_welcome_handler (GtkButton *button, gpointer   user_data)
{
    app.is_edit_mode = false;

    mem_pool_t tmp = {0};
    struct keyboard_layout_info_t *custom_layouts;
    int num_custom_layouts = 0;
    xkb_keymap_list (&tmp, &custom_layouts, &num_custom_layouts);

    if (num_custom_layouts > 0) {
        GtkWidget *welcome_sidebar = new_welcome_sidebar (custom_layouts, num_custom_layouts);
        replace_wrapped_widget (&app.sidebar, welcome_sidebar);
        app.keymap_test_button = new_keymap_test_button ();
        replace_wrapped_widget (&app.headerbar_buttons, app.keymap_test_button);
        gtk_header_bar_set_title (GTK_HEADER_BAR(app.header_bar), "Keys");
        kv_set_preview_test (app.keyboard_view);

    } else {
        GtkWidget *welcome_screen = new_welcome_screen_no_custom_layouts ();
        replace_wrapped_widget (&app.window_content, welcome_screen);
    }

    keyboard_layout_destroy (app.keymap);
    mem_pool_destroy (&tmp);
}

bool edit_xkb_str (struct kle_app_t *app, char *keymap_name, char *xkb_str)
{
    bool success = true;

    struct keyboard_layout_t *new_layout = keyboard_layout_new_from_xkb (xkb_str);

    if (new_layout != NULL) {
        app->keymap = new_layout;

        app->is_edit_mode = true;

        gtk_header_bar_set_title (GTK_HEADER_BAR(app->header_bar), keymap_name);

        // Set the headerbar buttons
        {
            GtkWidget *return_to_welcome_button = gtk_button_new_with_label ("Go Back");
            gtk_widget_set_valign (return_to_welcome_button, GTK_ALIGN_CENTER);
            add_css_class (return_to_welcome_button, "back-button");
            g_signal_connect (return_to_welcome_button, "clicked", G_CALLBACK (return_to_welcome_handler), NULL);

            GtkWidget *headerbar_buttons = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
            gtk_container_add (GTK_CONTAINER (headerbar_buttons), return_to_welcome_button);
            app->keymap_test_button = new_keymap_test_button();
            gtk_container_add (GTK_CONTAINER (headerbar_buttons), app->keymap_test_button);
            replace_wrapped_widget (&app->headerbar_buttons, headerbar_buttons);
            gtk_widget_show_all (headerbar_buttons);
        }

        GtkWidget *stack = gtk_stack_new ();
        gtk_widget_set_halign (stack, GTK_ALIGN_CENTER);
        {
            kv_set_preview_keys (app->keyboard_view);
            app->keys_sidebar = app_keys_sidebar_new (app, app->keyboard_view->preview_keys_selection->kc);
            gtk_stack_add_titled (GTK_STACK(stack), wrap_gtk_widget(app->keys_sidebar), "keys", "Keys");
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
        replace_wrapped_widget (&app->sidebar, grid);

    } else {
        // TODO: xkb file parsing failed, show an error message.
        success = false;
    }

    return success;
}

void edit_layout_handler (GtkButton *button, gpointer user_data)
{
    edit_xkb_str (&app, str_data(&app.curr_keymap_name), str_data(&app.curr_xkb_str));
}

// TODO: Do we want to add opened xkb files to the layout list?. It can be
// useful so it's easy to open a file we recently worked on. The problem is it
// becomes confusing what the layout list contains. How do we distinguish
// between installed layouts and opened layouts that are not installed? Maybe
// we can add a "Install" button to rows of uninstalled layouts.
//
// There are some things left to be thought about (and implemented) regarding
// the life cycle of a layout. We should track source files for installed
// layouts, but not fail if the source file of an installed layout is deleted.
// If the source file is available user saves will write to it and internally we
// will constantly create autosaves. If the source file is missing then we will
// have an internal source file where user saves are written and we will also
// hace internal autosaves. Whenever there are updates in a source file we
// prompt the user with a button to update the layout installation. This button
// can be located in the row of the layout list.
//
// Going back to adding opened (but not installed) layouts to the list, note
// that things look complex enough just for installed layouts. Probably the best
// approach is to leave the layout list just for installed layouts. If a layout
// is opened and not installed, the user will have to open it again.
void open_xkb_file_handler (GtkButton *button, gpointer user_data)
{
    GtkWidget *dialog =
        gtk_file_chooser_dialog_new ("Open Layout",
                                     GTK_WINDOW(app.window),
                                     GTK_FILE_CHOOSER_ACTION_OPEN,
                                     "_Cancel",
                                     GTK_RESPONSE_CANCEL,
                                     "_Open",
                                     GTK_RESPONSE_ACCEPT,
                                     NULL);

    char *fname = NULL;
    gint result = gtk_dialog_run (GTK_DIALOG (dialog));
    if (result == GTK_RESPONSE_ACCEPT) {
        fname = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER(dialog));

        mem_pool_t tmp = {0};
        char *file_content = full_file_read (&tmp, fname);

        char *name;
        path_split (&tmp, fname, NULL, &name);

        // We only set curr_xkb_str and curr_keymap_name if parsing of the file is
        // successful. Both, from our parser, and from libxkbcommon's parser for
        // the view's state.
        // TODO: data is originally stored in a temporary pool and then maybe copied
        // into strings here. Is it useful for common.h to have functions that write
        // directly to strings?, then here we would just free the old ones and
        // replace them with the new ones. This won't necessarily be faster, we are
        // comparing the speed of copying the full file content, with the speed of
        // freeing the old one. My thinking is a free 'should' be faster than a
        // copy, but who knows. I won't think much about this for now.
        // @performance
        if (edit_xkb_str (&app, name, file_content) &&
            keyboard_view_set_keymap (app.keyboard_view, name, file_content)) {
            str_set (&app.curr_xkb_str, file_content);
            str_set (&app.curr_keymap_name, name);

        } else {
            // TODO: Show some kind of feedback about what went wrong with the
            // keymap file.
        }

        mem_pool_destroy (&tmp);
    }

    gtk_widget_destroy (dialog);
}

gboolean window_delete_handler (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    gtk_main_quit ();
    return FALSE;
}

GtkWidget* new_install_layout_button ()
{
    GtkWidget *install_layout_button =
        intro_button_new ("document-save", "Install Layout", "Install an .xkb file into the system.");
    g_signal_connect (G_OBJECT(install_layout_button), "clicked", G_CALLBACK (install_layout_handler), NULL);

    return install_layout_button;
}

GtkWidget* new_open_layout_button ()
{
    GtkWidget *open_layout_button =
        intro_button_new ("document-open", "Open Layout", "Open an existing .xkb file.");
    g_signal_connect (G_OBJECT(open_layout_button), "clicked", G_CALLBACK (open_xkb_file_handler), NULL);

    return open_layout_button;
}

GtkWidget* new_new_layout_button ()
{
    GtkWidget *new_layout_button =
        intro_button_new ("document-new", "New Layout", "Create a layout based on an existing one.");
    return new_layout_button;
}

void on_sidebar_allocated (GtkWidget *widget, GdkRectangle *allocation, gpointer user_data)
{
    app.sidebar_min_width = allocation->width;
}

GtkWidget* new_welcome_sidebar (struct keyboard_layout_info_t *custom_layouts, int num_custom_layouts)
{
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
        gtk_widget_set_tooltip_text (install_layout_button, "Install an .xkb file into the system.");
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

    GtkWidget *new_layout_button = new_new_layout_button ();
    GtkWidget *open_layout_button = new_open_layout_button ();

    GtkWidget *sidebar = gtk_grid_new ();
    g_signal_connect (sidebar, "size-allocate", G_CALLBACK(on_sidebar_allocated), NULL);
    gtk_grid_set_row_spacing (GTK_GRID(sidebar), 12);
    add_custom_css (sidebar, ".grid, grid { margin: 12px; }");
    gtk_grid_attach (GTK_GRID(sidebar), layout_list, 0, 0, 1, 1);
    gtk_grid_attach (GTK_GRID(sidebar), new_layout_button, 0, 1, 1, 1);
    gtk_grid_attach (GTK_GRID(sidebar), open_layout_button, 0, 2, 1, 1);

    return sidebar;
}

// Build a welcome screen that shows installed layouts and a preview when
// selected from a list.
GtkWidget* new_welcome_screen_custom_layouts (struct keyboard_layout_info_t *custom_layouts, int num_custom_layouts)
{
    gtk_window_resize (GTK_WINDOW(app.window), 1430, 570);

    app.keymap_test_button = new_keymap_test_button ();
    replace_wrapped_widget (&app.headerbar_buttons, app.keymap_test_button);


    app.keyboard_view = keyboard_view_new_with_gui (app.window,
                                                    app.repr_path, app.selected_repr,
                                                    app.settings_file_path);
    app.keyboard_view->grab_notify_cb = show_grabbed_input_state;
    app.keyboard_view->ungrab_notify_cb = show_ungrabbed_input_state;
    app.keyboard_view->selected_key_change_cb = on_selected_key_change;

    app.sidebar = new_welcome_sidebar (custom_layouts, num_custom_layouts);

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

    return paned;
}

// Build a welcome screen with only introductory buttons and no list or preview
// of keyboards.
GtkWidget* new_welcome_screen_no_custom_layouts ()
{
    window_resize_centered (app.window, 900, 570);

    gtk_header_bar_set_title (GTK_HEADER_BAR(app.header_bar), "Keyboard Editor");

    GtkWidget *buttons;
    GtkWidget *welcome_screen = new_welcome_screen ("No Custom Keymaps", "Open an .xkb file to edit it.",
                                                    &buttons);

    GtkWidget *new_layout_button = new_new_layout_button ();
    gtk_container_add (GTK_CONTAINER(buttons), new_layout_button);

    GtkWidget *open_layout_button = new_open_layout_button ();
    gtk_container_add (GTK_CONTAINER(buttons), open_layout_button);

    GtkWidget *install_layout_button = new_install_layout_button();
    gtk_container_add (GTK_CONTAINER(buttons), install_layout_button);

    return welcome_screen;
}

bool print_layout_info (struct keyboard_layout_info_t *info_list, int num_layouts, char *name)
{
    bool found = false;

    for (int i=0; i<num_layouts && !found; i++) {
        if (strcmp (info_list[i].name, name) == 0) {
            printf ("Name: %s\n", info_list[i].name);
            printf ("Description: %s\n", info_list[i].description);
            printf ("Short description: %s\n", info_list[i].short_description);

            printf ("Languages: ");
            for (int j=0; j<info_list[i].num_languages; j++) {
                if (j > 0) {
                    printf (", ");
                }
                printf ("%s", info_list[i].languages[j]);
            }
            printf ("\n");

            found = true;
        }
    }

    return found;
}

int main (int argc, char *argv[])
{
    app = ZERO_INIT(struct kle_app_t);

    init_kernel_keycode_names ();
    init_xkb_keycode_names ();

    bool success = true;
    app.argv = argv;
    app.argc = argc;

    if (argc > 1) {
        if (strcmp (argv[1], "--install") == 0) {
            if (argc == 2) {
                printf ("Expected a keymap file to install.\n");
            } else {
                struct keyboard_layout_info_t info = {0};
                info.name = get_cli_arg_opt ( "--name", argv, argc);
                info.description = get_cli_arg_opt ( "--description", argv, argc);
                info.short_description = get_cli_arg_opt ( "--short_description", argv, argc);
                // TODO: Implement --languages flag. We should improve the API
                // around keyboard_layout_info_t. Make languages a linked list
                // and allow pushing and setting the by a comma separated
                // string.
                success = unprivileged_xkb_keymap_install (argv[2], &info);
            }

        } else if (strcmp (argv[1], "--uninstall") == 0) {
            if (argc == 2) {
                printf ("Expected a keymap name to uninstall.\n");
            } else {
                success = unprivileged_xkb_keymap_uninstall (argv[2]);
            }

        } else if (strcmp (argv[1], "--uninstall-everything") == 0) {
            success = unprivileged_xkb_keymap_uninstall_everything ();

        } else if (strcmp (argv[1], "--list-custom") == 0) {
            mem_pool_t pool = {0};
            struct keyboard_layout_info_t *info_list = NULL;
            int num_layouts = 0;
            xkb_keymap_list (&pool, &info_list, &num_layouts);
            for (int i=0; i<num_layouts; i++) {
                printf ("%s\n", info_list[i].name);
            }
            mem_pool_destroy (&pool);

        } else if (strcmp (argv[1], "--list-default") == 0) {
            mem_pool_t pool = {0};
            struct keyboard_layout_info_t *info_list = NULL;
            int num_layouts = 0;
            xkb_keymap_list_default (&pool, &info_list, &num_layouts);
            for (int i=0; i<num_layouts; i++) {
                printf ("%s\n", info_list[i].name);
            }
            mem_pool_destroy (&pool);

        } else if (strcmp (argv[1], "--show-info") == 0) {
            if (argc == 2) {
                printf ("Expected a keymap name of which to show information.\n");
            } else {
                bool found = false;
                mem_pool_t pool = {0};
                struct keyboard_layout_info_t *info_list = NULL;
                int num_layouts = 0;

                xkb_keymap_list (&pool, &info_list, &num_layouts);
                found = print_layout_info (info_list, num_layouts, argv[2]);

                if (!found) {
                    xkb_keymap_list_default (&pool, &info_list, &num_layouts);
                    found = print_layout_info (info_list, num_layouts, argv[2]);
                }

                if (!found) {
                    printf ("No layout named '%s' found.\n", argv[2]);
                }

                mem_pool_destroy (&pool);
            }
        }

    } else {
        gtk_init(&argc, &argv);

        gtk_icon_theme_add_resource_path (gtk_icon_theme_get_default (),
                                          "/com/github/santileortiz/iconoscope/icons");

        mem_pool_t tmp = {0};

        app.user_dir = sh_expand (USER_CONFIG_DIR_PATH, &app.pool);
        ensure_dir_exists (app.user_dir);

        // TODO: Currently the only setting we have is the last used
        // representation name. So for now that's the only content of the
        // settings file. When we get more settings either start using a
        // .desktop formatted file, or use gsettings.
        // :implement_better_persistent_settings
        app.settings_file_path = sh_expand (SETTINGS_FILE_PATH, &app.pool);

        // Load settings from file. Currently a single line contaíning the
        // selected keyboard representation.
        if (path_exists(app.settings_file_path)) {
            app.selected_repr = full_file_read (&app.pool, app.settings_file_path);
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

        app.repr_path = sh_expand (REPRESENTATIONS_DIR_PATH, &app.pool);

        struct keyboard_layout_info_t *custom_layouts;
        int num_custom_layouts = 0;
        xkb_keymap_list (&tmp, &custom_layouts, &num_custom_layouts);

        app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        g_signal_connect (G_OBJECT(app.window), "delete-event", G_CALLBACK (window_delete_handler), NULL);
        gtk_window_set_position(GTK_WINDOW(app.window), GTK_WIN_POS_CENTER);
        gtk_window_set_gravity (GTK_WINDOW(app.window), GDK_GRAVITY_CENTER);

        app.header_bar = gtk_header_bar_new ();
        gtk_header_bar_set_title (GTK_HEADER_BAR(app.header_bar), "Keys");
        gtk_header_bar_set_show_close_button (GTK_HEADER_BAR(app.header_bar), TRUE);
        app.headerbar_buttons = gtk_grid_new ();
        gtk_header_bar_pack_start (GTK_HEADER_BAR(app.header_bar), wrap_gtk_widget (app.headerbar_buttons));
        gtk_widget_show_all (app.header_bar);
        gtk_window_set_titlebar (GTK_WINDOW(app.window), app.header_bar);

        if (num_custom_layouts > 0) {
            app.window_content = new_welcome_screen_custom_layouts (custom_layouts, num_custom_layouts);
        } else {
            app.window_content = new_welcome_screen_no_custom_layouts ();
        }

        // It's not really necessary to wrap the content because GtkWindow is
        // already a GtkBin and will never be other kind of container but meh,
        // for consistency we do it anyway.
        gtk_container_add(GTK_CONTAINER(app.window), wrap_gtk_widget(app.window_content));
        gtk_widget_show_all (app.window);
        mem_pool_destroy (&tmp);

        // Custom CSS for the representation selector in the keyboard view.
        // TODO: How can I set this just to the specific instance of the
        // combobox widget and not globally? I tried but failed, I don't think I
        // know my CSS selectors well enough.
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
            string_t repr_path = str_new(app.repr_path);
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
    str_free (&app.curr_keymap_name);
    str_free (&app.curr_xkb_str);

    return !success;
}
