/*
 * Copiright (C) 2018 Santiago León O.
 */

struct kle_app_t {
    char* argv_0;
    GtkWidget *window;
    struct keyboard_view_t *keyboard_view;
    GdkSeat *gdk_seat;
    bool no_custom_layouts_welcome_view;

    char *user_dir;
    char *selected_repr;

    GResource *gresource;

    // UI widgets that change
    GtkWidget *custom_layout_list;
    GtkWidget *keyboard_grabbing_button;
    GtkWidget *sidebar;
    GtkWidget *keys_sidebar;
};

gboolean grab_input (GtkButton *button, gpointer user_data);
gboolean ungrab_input (GtkButton *button, gpointer user_data);
GtkWidget* app_keys_sidebar_new (struct kle_app_t *app, int kc);

string_t app_get_repr_path (struct kle_app_t *app);
