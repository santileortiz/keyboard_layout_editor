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

    int sidebar_min_width;

    // UI widgets that change
    GtkWidget *window_content;
    GtkWidget *custom_layout_list;
    GtkWidget *keyboard_grabbing_button;
    GtkWidget *sidebar;
    GtkWidget *keys_sidebar;
    struct fk_popover_t edit_symbol_popover;
    struct fk_searchable_list_t keysym_lookup_ui;
};

gboolean grab_input (GtkButton *button, gpointer user_data);
gboolean ungrab_input (GtkButton *button, gpointer user_data);
GtkWidget* app_keys_sidebar_new (struct kle_app_t *app, int kc);

string_t app_get_repr_path (struct kle_app_t *app);
