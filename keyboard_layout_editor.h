/*
 * Copiright (C) 2018 Santiago León O.
 */

struct kle_app_t {
    mem_pool_t pool;

    char* argv_0;
    char** argv;
    int argc;

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

GtkWidget* app_keys_sidebar_new (struct kle_app_t *app, int kc);

string_t app_get_repr_path (struct kle_app_t *app);
