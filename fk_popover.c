/*
 * Copiright (C) 2019 Santiago León O.
 */

// This is a simpler API for a more common (but less generic) type of popover
// than what GTK normally provides.

struct fk_popover_t;

#define FK_POPOVER_BUTTON_PRESSED_CB(name) void name(struct fk_popover_t *fk_popover, gpointer user_data)
typedef FK_POPOVER_BUTTON_PRESSED_CB(fk_popover_button_pressed_cb_t);

struct fk_popover_t {
    GtkWidget *popover;
    gpointer user_data;
    fk_popover_button_pressed_cb_t *accept_handler;
};

void fk_cancel_button_handler (GtkWidget *button, gpointer user_data)
{
    struct fk_popover_t *fk_popover = (struct fk_popover_t*)user_data;
    gtk_widget_destroy (fk_popover->popover);
}

void fk_accept_button_handler (GtkWidget *button, gpointer user_data)
{
    struct fk_popover_t *fk_popover = (struct fk_popover_t*)user_data;
    fk_popover->accept_handler (fk_popover, fk_popover->user_data);
    gtk_widget_destroy (fk_popover->popover);
}

void fk_popover_init (struct fk_popover_t *fk_popover,
                      GtkWidget *target_w, GdkRectangle *rect,
                      GtkWidget **popover, GtkWidget **save_button, GtkWidget **cancel_button,
                      char *accept_label, fk_popover_button_pressed_cb_t *accept_handler,
                      gpointer user_data)
{
    assert (target_w != NULL);
    assert (fk_popover != NULL);
    assert ( accept_handler != NULL && accept_label != NULL);

    GtkWidget *cancel_button_l = gtk_button_new_with_label ("Cancel");
    gtk_widget_set_margins (cancel_button_l, 6);
    g_signal_connect (G_OBJECT(cancel_button_l), "clicked", G_CALLBACK(fk_cancel_button_handler), fk_popover);
    if (cancel_button != NULL) { *cancel_button = cancel_button_l; }

    GtkWidget *save_button_l = gtk_button_new_with_label (accept_label);
    gtk_widget_set_margins (save_button_l, 6);
    g_signal_connect (G_OBJECT(save_button_l), "clicked", G_CALLBACK(fk_accept_button_handler), fk_popover);
    add_css_class (save_button_l, "suggested-action");
    fk_popover->accept_handler = accept_handler;
    if (save_button != NULL) { *save_button = save_button_l; }

    *popover = gtk_popover_new (target_w);
    gtk_popover_set_position (GTK_POPOVER(*popover), GTK_POS_BOTTOM);

    if (rect != NULL) {
        gtk_popover_set_pointing_to (GTK_POPOVER(*popover), rect);
    }

    fk_popover->user_data = user_data;
    fk_popover->popover = *popover;
}
