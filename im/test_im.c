#include <gtk/gtk.h>

GtkIMContext *im_context;

void print_str_hex (const gchar *str)
{
    printf ("%s : ", str);
    while (*str) {
        printf ("%x ", (unsigned char)*str);
        str++;
    }
    printf ("\n");
}

static void commit_cb (GtkIMContext *context, const gchar *str, GtkEntry *entry)
{
    print_str_hex (str);
}

static gint key_press (GtkWidget *widget, GdkEventKey *event)
{
    if (!gtk_im_context_filter_keypress (im_context, event)) {
        printf ("(Ignored)\n");
    }

    return FALSE;
}

static void text_change (GtkEditable *editable, gpointer *user_data)
{
    gchar *text = gtk_editable_get_chars (editable, 0, -1);
    printf ("Entry: ");
    print_str_hex (text);
}

int main(int argc, char *argv[])
{
    GtkWidget *window;
    GtkWidget *entry;

    gtk_init(&argc, &argv);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_window_set_title(GTK_WINDOW(window), "GTK window");

    entry = gtk_entry_new();
    gtk_container_add(GTK_CONTAINER(window), entry);
    g_signal_connect (entry, "changed",
                      G_CALLBACK (text_change), NULL);

    im_context = gtk_im_context_simple_new ();

    g_signal_connect (im_context, "commit",
                      G_CALLBACK (commit_cb), NULL);
    g_signal_connect (window, "key-press-event",
                      G_CALLBACK (key_press), NULL);
    g_signal_connect(window, "destroy",
                     G_CALLBACK(gtk_main_quit), NULL);

    gtk_widget_show_all(window);

    gtk_main();

    return 0;
}

