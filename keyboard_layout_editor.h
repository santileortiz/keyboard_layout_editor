/*
 * Copiright (C) 2018 Santiago León O.
 */

struct kle_app_t {
    char* argv_0;
    GtkWidget *window;
    struct keyboard_view_t *keyboard_view;
    GtkWidget *custom_layout_list;
    GtkWidget *keyboard_grabbing_button;
    GdkSeat *gdk_seat;
    bool no_custom_layouts_welcome_view;
};

gboolean grab_input (GtkButton *button, gpointer user_data);
gboolean ungrab_input (GtkButton *button, gpointer user_data);
