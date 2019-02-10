/*
 * Copiright (C) 2019 Santiago León O.
 */

struct fk_searchable_list_t {
    GtkWidget *search_entry;
    GtkWidget *list;

    // NOTE: This is used to detect which is the first visible row and select it
    // inside the filter function.
    // @select_first_on_filter_invalidate
    bool is_first_row;
};

void fk_searchable_list_search_changed (GtkEditable *search_entry, gpointer user_data)
{
    struct fk_searchable_list_t *fk_searchable_list = (struct fk_searchable_list_t*)user_data;

    // @select_first_on_filter_invalidate
    fk_searchable_list->is_first_row = true;
    gtk_list_box_invalidate_filter (GTK_LIST_BOX(fk_searchable_list->list));
}

gboolean fk_searchable_list_search_filter (GtkListBoxRow *row, gpointer user_data)
{
    struct fk_searchable_list_t *fk_searchable_list = (struct fk_searchable_list_t*)user_data;
    const gchar *search_str = gtk_entry_get_text (GTK_ENTRY(fk_searchable_list->search_entry));
    GtkWidget *row_label = gtk_bin_get_child (GTK_BIN(row));
    const char *name = gtk_label_get_text (GTK_LABEL(row_label));

    bool res = strcasestr (name, search_str) != NULL ? TRUE : FALSE;

    // @select_first_on_filter_invalidate
    if (res == TRUE && fk_searchable_list->is_first_row) {
        fk_searchable_list->is_first_row = false;
        gtk_list_box_select_row (GTK_LIST_BOX(fk_searchable_list->list), row);
    }

    return res;
}

void fk_searchable_list_init (struct fk_searchable_list_t *fk_searchable_list,
                              char *placeholder_text,
                              GtkWidget **search_entry_w, GtkWidget **list_w)
{
    assert (fk_searchable_list != NULL);
    assert (placeholder_text != NULL);
    assert (search_entry_w != NULL && list_w != NULL);

    fk_searchable_list->search_entry = gtk_search_entry_new ();
    gtk_widget_set_margins (fk_searchable_list->search_entry, 6);
    gtk_entry_set_placeholder_text (GTK_ENTRY(fk_searchable_list->search_entry),
                                    placeholder_text);

    g_signal_connect (G_OBJECT(fk_searchable_list->search_entry),
                      "changed",
                      G_CALLBACK (fk_searchable_list_search_changed),
                      fk_searchable_list);

    fk_searchable_list->list = gtk_list_box_new ();
    gtk_widget_set_vexpand (fk_searchable_list->list, TRUE);
    gtk_widget_set_hexpand (fk_searchable_list->list, TRUE);
    gtk_list_box_set_filter_func (GTK_LIST_BOX(fk_searchable_list->list),
                                  fk_searchable_list_search_filter,
                                  fk_searchable_list, NULL);

    GtkWidget *scrolled_keycode_list = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_disable_hscroll (GTK_SCROLLED_WINDOW(scrolled_keycode_list));
    gtk_scrolled_window_set_min_content_width (GTK_SCROLLED_WINDOW(scrolled_keycode_list), 200);
    gtk_scrolled_window_set_min_content_height (GTK_SCROLLED_WINDOW(scrolled_keycode_list), 100);
    gtk_container_add (GTK_CONTAINER (scrolled_keycode_list), fk_searchable_list->list);
    GtkWidget *frame = gtk_frame_new (NULL);
    gtk_widget_set_margins (frame, 6);
    gtk_container_add (GTK_CONTAINER(frame), scrolled_keycode_list);

    *search_entry_w = fk_searchable_list->search_entry;
    *list_w = frame;
}

// DATA_GETTER is an expression containing i as index that evaluates to a char*
// of the i'th row. The value of i will iterate from 0 to len-1.
#define fk_populate_list(fk_searchable_list,DATA_GETTER,len)                         \
{                                                                                    \
    bool first = true;                                                               \
    for (int i=0; i < (len); i++)                                                    \
    {                                                                                \
        if ((DATA_GETTER) != NULL) {                                                 \
            GtkWidget *row = gtk_label_new (DATA_GETTER);                            \
            gtk_container_add (GTK_CONTAINER((fk_searchable_list)->list), row);      \
            gtk_widget_set_halign (row, GTK_ALIGN_START);                            \
                                                                                     \
            if (first) {                                                             \
                first = false;                                                       \
                GtkWidget *r = gtk_widget_get_parent (row);                          \
                gtk_list_box_select_row (GTK_LIST_BOX((fk_searchable_list)->list),   \
                                         GTK_LIST_BOX_ROW(r));                       \
            }                                                                        \
                                                                                     \
                                                                                     \
            gtk_widget_set_margin_start (row, 6);                                    \
            gtk_widget_set_margin_end (row, 6);                                      \
            gtk_widget_set_margin_top (row, 3);                                      \
            gtk_widget_set_margin_bottom (row, 3);                                   \
        }                                                                            \
    }                                                                                \
}

