/*
 * Copiright (C) 2018 Santiago León O.
 */

#define RGBA DVEC4
#define RGB(r,g,b) DVEC4(r,g,b,1)
#define ARGS_RGBA(c) (c).r, (c).g, (c).b, (c).a
#define ARGS_RGB(c) (c.r), (c).g, (c).b
#define RGB_HEX(hex) DVEC4(((double)(((hex)&0xFF0000) >> 16))/255, \
                           ((double)(((hex)&0x00FF00) >>  8))/255, \
                           ((double)((hex)&0x0000FF))/255, 1)

// Wrapper used to destroy all children from a container
void destroy_children_callback (GtkWidget *widget, gpointer data)
{
    gtk_widget_destroy (widget);
}

void window_resize_centered (GtkWidget *window, gint w, gint h)
{
    gint x, y;
    gtk_window_get_position (GTK_WINDOW(window), &x, &y);
    gtk_window_resize (GTK_WINDOW(window), w, h);
    gtk_window_move (GTK_WINDOW(window), x, y);
}

void add_custom_css (GtkWidget *widget, gchar *css_data)
{
    GtkStyleContext *style_context = gtk_widget_get_style_context (widget);
    GtkCssProvider *css_provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_data (css_provider, css_data, -1, NULL);
    gtk_style_context_add_provider (style_context,
                                    GTK_STYLE_PROVIDER(css_provider),
                                    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

void add_css_class (GtkWidget *widget, char *class)
{
    GtkStyleContext *ctx = gtk_widget_get_style_context (widget);
    gtk_style_context_add_class (ctx, class);
}

void g_object_set_property_bool (GObject *object, const char *property_name, bool value)
{
    GValue val = G_VALUE_INIT;
    g_value_init (&val, G_TYPE_BOOLEAN);
    g_value_set_boolean (&val, value);
    g_object_set_property (object, property_name, &val);
}

void cr_rounded_box (cairo_t *cr, double x, double y, double width, double height, double radius)
{
    double r = radius;
    double w = width;
    double h = height;
    cairo_move_to (cr, x, y+r);
    cairo_arc (cr, x+r, y+r, r, M_PI, 3*M_PI/2);
    cairo_arc (cr, x+w - r, y+r, r, 3*M_PI/2, 0);
    cairo_arc (cr, x+w - r, y+h - r, r, 0, M_PI/2);
    cairo_arc (cr, x+r, y+h - r, r, M_PI/2, M_PI);
    cairo_close_path (cr);
}

