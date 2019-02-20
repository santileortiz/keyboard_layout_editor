/*
 * Copiright (C) 2018 Santiago León O.
 */

struct kle_app_t {
    char* argv_0;
    GtkWidget *window;
    struct keyboard_view_t *keyboard_view;
    GdkSeat *gdk_seat;

    char *user_dir;
    char *selected_repr;

    GResource *gresource;

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

void grab_input (GtkButton *button, gpointer user_data);
void ungrab_input (GtkButton *button, gpointer user_data);
GtkWidget* app_keys_sidebar_new (struct kle_app_t *app, int kc);

string_t app_get_repr_path (struct kle_app_t *app);
