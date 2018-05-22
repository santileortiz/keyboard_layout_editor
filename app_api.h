/*
 * Copiright (C) 2018 Santiago León O.
 */

GtkWidget *window = NULL;
GtkWidget *keyboard_view = NULL;
GtkWidget *custom_layout_list = NULL;
GtkWidget *keyboard_grabbing_button = NULL;
GdkSeat *gdk_seat = NULL;
bool no_custom_layouts_welcome_view = false;

gboolean ungrab_keyboard_handler (GtkButton *button, gpointer user_data);
gboolean grab_keyboard_handler (GtkButton *button, gpointer user_data);
