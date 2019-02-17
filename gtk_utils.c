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

void add_global_css (gchar *css_data)
{
    GdkScreen *screen = gdk_screen_get_default ();
    GtkCssProvider *css_provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_data (css_provider, css_data, -1, NULL);
    gtk_style_context_add_provider_for_screen (screen,
                                    GTK_STYLE_PROVIDER(css_provider),
                                    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
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

#define is_in_rect(x,y,rect) is_point_in_box(x,y,(rect).x,(rect).y,(rect).width,(rect).height)
bool is_point_in_box (double p_x, double p_y, double x, double y, double width, double height)
{
    if (p_x < x || p_x > x+width) {
        return false;
    } else if (p_y < y || p_y > y+height) {
        return false;
    } else {
        return true;
    }
}

void add_css_class (GtkWidget *widget, char *class)
{
    GtkStyleContext *ctx = gtk_widget_get_style_context (widget);
    gtk_style_context_add_class (ctx, class);
}

void g_object_set_property_bool (GObject *object, const char *property_name, gboolean value)
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

enum edge_direction {
    EDGE_UP,
    EDGE_RIGHT,
    EDGE_LEFT,
    EDGE_DOWN
};

static inline
enum edge_direction opposite_direction (enum edge_direction dir)
{
    return dir^0x3;
}

void cr_draw_round_corner (cairo_t *cr, double x, double y, double r,
                           enum edge_direction in, enum edge_direction out)
{
    switch (in) {
        case EDGE_UP:
            switch (out) {
                case EDGE_RIGHT:
                    cairo_arc (cr, x+r, y+r, r, M_PI, 3*M_PI/2);
                    break;
                case EDGE_LEFT:
                    cairo_arc_negative (cr, x-r, y+r, r, 0, 3*M_PI/2);
                    break;
                default:
                    invalid_code_path;
            }
            break;

        case EDGE_RIGHT:
            switch (out) {
                case EDGE_UP:
                    cairo_arc_negative (cr, x-r, y-r, r, M_PI/2, 0);
                    break;
                case EDGE_DOWN:
                    cairo_arc (cr, x-r, y+r, r, 3*M_PI/2, 0);
                    break;
                default:
                    invalid_code_path;
            }
            break;

        case EDGE_DOWN:
            switch (out) {
                case EDGE_RIGHT:
                    cairo_arc_negative (cr, x+r, y-r, r, M_PI, M_PI/2);
                    break;
                case EDGE_LEFT:
                    cairo_arc (cr, x-r, y-r, r, 0, M_PI/2);
                    break;
                default:
                    invalid_code_path;
            }
            break;

        case EDGE_LEFT:
            switch (out) {
                case EDGE_UP:
                    cairo_arc (cr, x+r, y-r, r, M_PI/2, M_PI);
                    break;
                case EDGE_DOWN:
                    cairo_arc_negative (cr, x+r, y+r, r, 3*M_PI/2, M_PI);
                    break;
                default:
                    invalid_code_path;
            }
            break;
    }
}

// NOTE: The following implmentation for paths with round corners has only been
// implemented for the keyboard usecase (i.e. orthogonal closed paths). More
// testing and code is required for a general implementation.
#define DEBUG_ROUND_PATH 0
struct round_path_ctx {
    cairo_t *cr;
    double x_prev, y_prev;
    double r; // Corner radius
    enum edge_direction prev_dir;
    bool start;
    enum edge_direction start_dir;
    double x_start, y_start;
};

struct round_path_ctx round_path_start (cairo_t *cr, double x, double y, double radius)
{
#if DEBUG_ROUND_PATH
    printf ("\nStart: (%f, %f)\n", x, y);
#endif
    struct round_path_ctx res;
    res.start = true;
    res.cr = cr;
    res.x_prev = x;
    res.y_prev = y;
    res.x_start = x;
    res.y_start = y;
    res.r = radius;
    return res;
}

void round_path_move_to (struct round_path_ctx *ctx, double x, double y)
{
#if DEBUG_ROUND_PATH
    printf ("Move: (%f, %f)\n", x, y);
#endif

    assert ((x == ctx->x_prev || y == ctx->y_prev) && "Only orthogonal shapes are supported");

    enum edge_direction new_dir;
    if (ctx->x_prev == x) {
        // Horizontal line
        if (ctx->y_prev < y) {
            new_dir = EDGE_DOWN;
        } else {
            new_dir = EDGE_UP;
        }

    } else { // if (ctx->y_prev == y) {
        // vertical line
        if (ctx->x_prev < x) {
            new_dir = EDGE_RIGHT;
        } else {
            new_dir = EDGE_LEFT;
        }
    }

    if (!ctx->start) {
        // Draw the previous corner
        if (ctx->prev_dir == opposite_direction (new_dir) ||
            ctx->prev_dir == new_dir) {
            // do nothing
            printf ("Round path has straight segments or 180 degree turns, most likely a bug.\n");

        } else if (ctx->prev_dir != new_dir) {
            cr_draw_round_corner (ctx->cr, ctx->x_prev, ctx->y_prev, ctx->r, ctx->prev_dir, new_dir);
        }

    } else {
        ctx->start = false;
        ctx->start_dir = new_dir;
        switch (new_dir) {
            case EDGE_UP:
                cairo_move_to (ctx->cr, ctx->x_start, ctx->y_start - ctx->r);
                break;
            case EDGE_RIGHT:
                cairo_move_to (ctx->cr, ctx->x_start + ctx->r, ctx->y_start);
                break;
            case EDGE_DOWN:
                cairo_move_to (ctx->cr, ctx->x_start, ctx->y_start + ctx->r);
                break;
            case EDGE_LEFT:
                cairo_move_to (ctx->cr, ctx->x_start - ctx->r, ctx->y_start);
                break;
        }
    }

    ctx->x_prev = x;
    ctx->y_prev = y;
    ctx->prev_dir = new_dir;
}

void round_path_close (struct round_path_ctx *ctx)
{
#if DEBUG_ROUND_PATH
    printf ("Close\n");
#endif

    assert ((ctx->x_start == ctx->x_prev || ctx->y_start == ctx->y_prev)
            && "Start and end must be aligned to close the rounded path");

    enum edge_direction last_edge_dir;
    if (ctx->x_prev == ctx->x_start) {
        // Horizontal line
        if (ctx->y_prev < ctx->y_start) {
            last_edge_dir = EDGE_DOWN;
        } else {
            last_edge_dir = EDGE_UP;
        }

    } else { // if (ctx->y_prev == y) {
        // vertical line
        if (ctx->x_prev < ctx->x_start) {
            last_edge_dir = EDGE_RIGHT;
        } else {
            last_edge_dir = EDGE_LEFT;
        }
    }

    cr_draw_round_corner (ctx->cr, ctx->x_prev, ctx->y_prev, ctx->r, ctx->prev_dir, last_edge_dir);
    cr_draw_round_corner (ctx->cr, ctx->x_start, ctx->y_start, ctx->r, last_edge_dir, ctx->start_dir);
}

// Widget Wrapping Idiom
// ---------------------
//
// Gtk uses reference counting of its objects. As a convenience for C
// programming they have a feature called "floating references", this feature
// makes most objects be created with a floating reference, instead of a normal
// one. Then the object that will take ownership of it, calls
// g_object_ref_sink() and becomes the owner of this floating reference making
// it a normal one. This allows C code to not call g_object_unref() on the newly
// created object and not leak a reference (and thus, not leak memory).
//
// The problem is this only work for objects that inherit from the
// GInitialyUnowned class. In all the Gtk class hierarchy ~150 classes inherit
// from GInitiallyUnowned and ~100 don't, which makes floating references the
// common case. What I normally do is assume things use floating references, and
// never call g_object_ref_sink() under the assumption that the function I'm
// passing the object to will sink it.
//
// Doing this for GtkWidgets is very useful to implement changes in the UI
// without creating a tangle of signals. What I do is completely replace widgets
// that need to be updated, instead of trying to update the state of objects to
// reflect changes. I keep weak references (C pointers, not even Gtk weak
// references) to widgets that may need to be replaced, then I remove the widget
// from it's container and replace it by a new one. Here not taking references
// to any widget becomes useful as removing the widget form it's parent will
// have the effect of destroying all the children objects (as long as Gtk
// internally isn't leaking memory).
//
// The only issue I've found is that containers like GtkPaned or GtkGrid use
// special _add methods with different declaration than gtk_container_add().
// It's very common to move widgets accross different kinds of containers, which
// adds the burden of having to know where in the code we assumed something was
// stored in a GtkGrid or a GtkPaned etc. What I do to alieviate this pain is
// wrap widgets that will be replaced into a GtkBox with wrap_gtk_widget() and
// then use replace_wrapped_widget() to replace it.
//
// Doing this ties the lifespan of an GInitiallyUnowned object to the lifespan
// of the parent. Sometimes this is not desired, in those cases we must call
// g_object_ref_sink() (NOT g_object_ref(), because as far as I understand it
// will set refcount to 2 and never free them). Fortunatelly cases where we are
// a longer lifespan is expected will print lots of Critical warnings or crash
// the application so they are easy to detect and fix, a memory leak is harder
// to detect/fix.
//
// NOTE: The approach of storing a pointer to the parent widget not only doubles
// the number of pointers that need to be stored for a replaceable widget, but
// also doesn't work because we can't know the true parent of a widget. For
// instance, GtkScrolledWindow wraps its child in a GtkViewPort.
//
// TODO: I need a way to detect when I'm using objects that don't inherit from
// GInitiallyUnowned and be sure we are not leaking references. Also make sure
// g_object_ref_sink() is used instead of g_object_ref().
GtkWidget* wrap_gtk_widget (GtkWidget *widget)
{
    GtkWidget *wrapper = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add (GTK_CONTAINER(wrapper), widget);
    return wrapper;
}

void replace_wrapped_widget (GtkWidget **original, GtkWidget *new_widget)
{
    GtkWidget *parent = gtk_widget_get_parent (*original);
    gtk_container_remove (GTK_CONTAINER(parent), *original);

    *original = new_widget;
    gtk_container_add (GTK_CONTAINER(parent), new_widget);

    gtk_widget_show_all (new_widget);
}

// An issue with the wrapped widget idiom is that if a widget triggers a replace
// of one of its ancestors, then we are effectiveley destroying ourselves from
// the signal handler. I've found cases where Gtk does not like that and shows
// lots of critical errors. I think these errors happen because the signal where
// we triggered the destruction of the ancestor is not the only one being
// handled, then after destruction, other handlers try to trigger inside the
// widget that does not exist anymore.
//
// I don't know if this is a Gtk bug, or we are not supposed to trigger the
// destruction of an object from the signal handler of a signal of the object
// that will be destroyed. The case I found not to work was a GtkCombobox
// destroying itself from the "changed" signal. A button destroying itself from
// the "clicked" signal worked fine though.
//
// In any case, if this happens, the function replace_wrapped_widget_deferred()
// splits widget replacement so that widget destruction is triggered, and then
// in the handler for the destroy signal, we actually replace the widget. Not
// nice, but seems to solve the issue.
gboolean idle_widget_destroy (gpointer user_data)
{
    gtk_widget_destroy ((GtkWidget *) user_data);
    return FALSE;
}

void replace_wrapped_widget_deferred_cb (GtkWidget *object, gpointer user_data)
{
    // We do this here so we never add the old and new widgets to the wrapper
    // and end up increasing the allocation size and glitching/
    gtk_widget_show_all ((GtkWidget *) user_data);
}

void replace_wrapped_widget_deferred (GtkWidget **original, GtkWidget *new_widget)
{
    GtkWidget *parent = gtk_widget_get_parent (*original);
    gtk_container_add (GTK_CONTAINER(parent), new_widget);

    g_signal_connect (G_OBJECT(*original), "destroy", G_CALLBACK (replace_wrapped_widget_deferred_cb), new_widget);
    g_idle_add (idle_widget_destroy, *original);
    *original = new_widget;
}

// This is the only way I found to disable horizontal scrolling in a scrolled
// window. I think calling gtk_adjustment_set_upper() 'should' work, but it
// doesn't.
void _gtk_scrolled_window_disable_hscroll_cb (GtkAdjustment *adjustment, gpointer user_data)
{
    if (gtk_adjustment_get_value (adjustment) != 0) {
        gtk_adjustment_set_value (adjustment, 0);
    }
}

void gtk_scrolled_window_disable_hscroll (GtkScrolledWindow *scrolled_window)
{
    gtk_scrolled_window_set_policy (scrolled_window, GTK_POLICY_EXTERNAL, GTK_POLICY_AUTOMATIC);
    GtkAdjustment *hadj = gtk_scrolled_window_get_hadjustment (scrolled_window);
#if 1
    g_signal_connect (G_OBJECT(hadj),
                      "value-changed",
                      G_CALLBACK (_gtk_scrolled_window_disable_hscroll_cb),
                      NULL);
#else
    // FIXME: Why U not work!?
    gtk_adjustment_set_upper (hadj, 0);
#endif
}

void combo_box_text_append_text_with_id (GtkComboBoxText *combobox, const gchar *text)
{
    gtk_combo_box_text_append (combobox, text, text);
}

void gtk_widget_set_margins (GtkWidget *widget, int size)
{
    gtk_widget_set_margin_top (widget, size);
    gtk_widget_set_margin_bottom (widget, size);
    gtk_widget_set_margin_start (widget, size);
    gtk_widget_set_margin_end (widget, size);
}

GtkWidget *title_label_new (char *label)
{
    GtkWidget *label_w = gtk_label_new (label);
    gtk_widget_set_halign (label_w, GTK_ALIGN_END);
    gtk_widget_set_hexpand (label_w, TRUE);
    add_css_class (label_w, "h4");
    gtk_widget_set_margins (label_w, 6);

    return label_w;
}

void labeled_text_new (char *label, char *value, GtkWidget **label_widget, GtkWidget **value_widget)
{
    GtkWidget *label_w = title_label_new (label);
    if (label_widget != NULL) { *label_widget = label_w; }

    GtkWidget *value_w = gtk_label_new (value);
    gtk_label_set_ellipsize (GTK_LABEL(value_w), PANGO_ELLIPSIZE_END);
    gtk_widget_set_halign (value_w, GTK_ALIGN_START);
    gtk_widget_set_hexpand (value_w, TRUE);
    gtk_label_set_selectable (GTK_LABEL(value_w), TRUE);
    add_css_class (value_w, "h5");
    gtk_widget_set_margins (value_w, 6);
    if (value_widget != NULL) { *value_widget = value_w; }
}

void labeled_text_new_in_grid (GtkGrid *grid, char *label, char *value, int x, int y)
{
    GtkWidget *label_w, *value_w;
    labeled_text_new (label, value, &label_w, &value_w);

    gtk_grid_attach (grid, label_w, x, y, 1, 1);
    gtk_grid_attach (grid, value_w, x+1, y, 1, 1);
}

void labeled_combobox_new (char *label, GtkWidget **label_widget, GtkWidget **combobox_widget)
{
    GtkWidget *label_w = title_label_new (label);
    if (label_widget != NULL) { *label_widget = label_w; }

    GtkWidget *combobox_w = gtk_combo_box_text_new ();
    gtk_widget_set_halign (combobox_w, GTK_ALIGN_START);
    gtk_widget_set_hexpand (combobox_w, TRUE);
    gtk_widget_set_margins (combobox_w, 6);
    if (combobox_widget != NULL) { *combobox_widget = combobox_w; }
}

void labeled_combobox_new_in_grid (GtkGrid *grid, char *label, int x, int y, GtkWidget **combobox_widget)
{
    assert (combobox_widget != NULL && "You really need the combobox to fill it.");

    GtkWidget *label_w;
    labeled_combobox_new (label, &label_w, combobox_widget);

    gtk_grid_attach (grid, label_w, x, y, 1, 1);
    gtk_grid_attach (grid, *combobox_widget, x+1, y, 1, 1);
}

GtkWidget* new_icon_button (const char *icon_name,
                            const char *tooltip,
                            GCallback click_handler)
{
    // TODO: For some reason the highlight circle on this button button has
    // height 32px but width 30px. Code's circle on header buttons is 30px by 30px.
    GtkWidget *new_button = gtk_button_new_from_icon_name (icon_name, GTK_ICON_SIZE_LARGE_TOOLBAR);
    gtk_widget_set_tooltip_text (new_button, tooltip);
    g_signal_connect (new_button, "clicked", G_CALLBACK (click_handler), NULL);
    gtk_widget_set_halign (new_button, GTK_ALIGN_FILL);
    gtk_widget_set_valign (new_button, GTK_ALIGN_FILL);
    gtk_widget_set_margin_start (new_button, 6);
    gtk_widget_set_margin_end (new_button, 6);
    gtk_widget_show (new_button);
    return new_button;
}

void set_header_icon_button (GtkWidget **button,
                             const char *icon_name,
                             const char *tooltip,
                             GCallback click_handler)
{
    GtkWidget *parent = gtk_widget_get_parent (*button);
    gtk_container_remove (GTK_CONTAINER(parent), *button);
    *button = new_icon_button (icon_name, tooltip, click_handler);
    gtk_header_bar_pack_start (GTK_HEADER_BAR(parent), *button);
}

GtkWidget *new_welcome_screen (const char *title, const char *message, GtkWidget **buttons_cotainer)
{
    assert (title != NULL && message != NULL);

    GtkWidget *title_label = gtk_label_new (title);
    add_css_class (title_label, "h1");
    gtk_widget_set_halign (title_label, GTK_ALIGN_CENTER);

    GtkWidget *subtitle_label = gtk_label_new (message);
    add_css_class (subtitle_label, "h2");
    add_css_class (subtitle_label, "dim-label");
    gtk_widget_set_halign (subtitle_label, GTK_ALIGN_CENTER);

    GtkWidget *grid = gtk_grid_new ();
    gtk_widget_set_halign (grid, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (grid, GTK_ALIGN_CENTER);
    gtk_widget_set_margins (grid, 12);

    gtk_grid_attach (GTK_GRID(grid), title_label, 0, 0, 1, 1);
    gtk_grid_attach (GTK_GRID(grid), subtitle_label, 0, 1, 1, 1);

    if (buttons_cotainer != NULL) {
        *buttons_cotainer = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
        gtk_widget_set_margins (*buttons_cotainer, 24);
        gtk_grid_attach (GTK_GRID(grid), *buttons_cotainer, 0, 2, 1, 1);
    }

    // For some reason setting the grid to have vexpand, hexpand to TRUE and
    // valign and halign to GTK_ALIGN_CENTER or GTK_ALIGN_FILL does not center
    // it. The only reason we do this is to center the grid.
    GtkWidget *useless_wrapper = gtk_event_box_new ();
    add_css_class (useless_wrapper, "view");
    add_css_class (useless_wrapper, "welcome");
    gtk_widget_set_halign (useless_wrapper, GTK_ALIGN_FILL);
    gtk_widget_set_valign (useless_wrapper, GTK_ALIGN_FILL);
    gtk_widget_set_vexpand (useless_wrapper, TRUE);
    gtk_widget_set_hexpand (useless_wrapper, TRUE);
    gtk_container_add (GTK_CONTAINER(useless_wrapper), grid);

    return useless_wrapper;
}

