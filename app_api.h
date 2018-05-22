/*
 * Copiright (C) 2018 Santiago León O.
 */

GtkWidget *window = NULL;
struct keyboard_view_t *keyboard_view = NULL;
GtkWidget *custom_layout_list = NULL;
GtkWidget *keyboard_grabbing_button = NULL;
GdkSeat *gdk_seat = NULL;
bool no_custom_layouts_welcome_view = false;

gboolean grab_input (GtkButton *button, gpointer user_data);
gboolean ungrab_input (GtkButton *button, gpointer user_data);
