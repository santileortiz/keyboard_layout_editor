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

// FIXME: We should tie the lifespan of a popover to some pool and add a
// kv_popover_destroy() function that will get called automatically if the
// associated pool is destroyed. Right now we are leaking gtk objects here.
void fk_popover_init (struct fk_popover_t *fk_popover,
                      GtkWidget *target_w, GdkRectangle *rect,
                      GtkWidget **popover,
                      GtkWidget *content,
                      char *accept_label, fk_popover_button_pressed_cb_t *accept_handler,
                      gpointer user_data)
{
    assert (target_w != NULL);
    assert (fk_popover != NULL);
    assert ( accept_handler != NULL && accept_label != NULL);

    GtkWidget *cancel_button = gtk_button_new_with_label ("Cancel");
    gtk_widget_set_margins (cancel_button, 6);
    g_signal_connect (G_OBJECT(cancel_button), "clicked", G_CALLBACK(fk_cancel_button_handler), fk_popover);

    GtkWidget *accept_button = gtk_button_new_with_label (accept_label);
    gtk_widget_set_margins (accept_button, 6);
    g_signal_connect (G_OBJECT(accept_button), "clicked", G_CALLBACK(fk_accept_button_handler), fk_popover);
    add_css_class (accept_button, "suggested-action");
    fk_popover->accept_handler = accept_handler;

    GtkWidget *popover_l = gtk_popover_new (target_w);
    gtk_popover_set_position (GTK_POPOVER(popover_l), GTK_POS_BOTTOM);

    if (rect != NULL) {
        gtk_popover_set_pointing_to (GTK_POPOVER(popover_l), rect);
    }

    fk_popover->user_data = user_data;
    fk_popover->popover = popover_l;

    GtkWidget *grid = gtk_grid_new ();
    gtk_widget_set_margins (grid, 6);
    gtk_grid_attach (GTK_GRID(grid), content, 0, 0, 2, 1);
    gtk_grid_attach (GTK_GRID(grid), cancel_button, 0, 1, 1, 1);
    gtk_grid_attach (GTK_GRID(grid), accept_button, 1, 1, 1, 1);

    gtk_container_add (GTK_CONTAINER(popover_l), grid);
    gtk_widget_show_all (popover_l);
    if (popover != NULL) { *popover = popover_l; }
}
