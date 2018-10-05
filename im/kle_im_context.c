/*
 * Copiright (C) 2018 Santiago León O.
 */
#include <gtk/gtk.h>
#include "kle_im_context.h"

struct _KleIMContext {
    GtkIMContext parent_instance;
};

static void kle_im_context_finalize (GObject *obj);
static void kle_im_context_set_client_window (GtkIMContext *context, GdkWindow *window);
static void kle_im_context_get_preedit_string (GtkIMContext *context,
                                               gchar **str, PangoAttrList **attrs, gint *cursror_pos);
static gboolean kle_im_context_filter_keypress (GtkIMContext *context, GdkEventKey *event);
static void kle_im_context_reset (GtkIMContext *context);

G_DEFINE_DYNAMIC_TYPE (KleIMContext, kle_im_context, GTK_TYPE_IM_CONTEXT);

void
kle_im_context_register_type_external (GTypeModule *type_module)
{
    kle_im_context_register_type (type_module);
}

static void
kle_im_context_class_init (KleIMContextClass *class)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(class);
    gobject_class->finalize = kle_im_context_finalize;

    GtkIMContextClass *gtk_im_context_class = GTK_IM_CONTEXT_CLASS(class);
    gtk_im_context_class->set_client_window = kle_im_context_set_client_window;
    gtk_im_context_class->get_preedit_string = kle_im_context_get_preedit_string;
    gtk_im_context_class->filter_keypress = kle_im_context_filter_keypress;
    gtk_im_context_class->reset = kle_im_context_reset;
}

KleIMContext*
kle_im_context_new (void)
{
    return KLE_IM_CONTEXT(g_object_new (KLE_TYPE_IM_CONTEXT, NULL));
}

static void
kle_im_context_class_finalize (KleIMContextClass *class)
{
    printf ("Call to: kle_im_context_class_finalize\n");
}

static void
kle_im_context_init (KleIMContext *context)
{
    printf ("Call to: kle_im_context_init\n");
}

static void
kle_im_context_finalize (GObject *context)
{
    printf ("Call to: kle_im_context_finalize\n");
}

static void
kle_im_context_set_client_window (GtkIMContext *context, GdkWindow *window)
{
    printf ("Call to: kle_im_context_set_client_window\n");
}

static void
kle_im_context_get_preedit_string (GtkIMContext *context, gchar **str, PangoAttrList **attrs, gint *cursror_pos)
{
    printf ("Call to: kle_im_context_get_preedit_string\n");
    GTK_IM_CONTEXT_CLASS (kle_im_context_parent_class)->get_preedit_string (context, str, attrs, cursror_pos);
}

static gboolean
kle_im_context_filter_keypress (GtkIMContext *context, GdkEventKey *event)
{
    printf ("Call to: kle_im_context_filter_keypress\n");
    return GTK_IM_CONTEXT_CLASS (kle_im_context_parent_class)->filter_keypress (context, event);
}

static void
kle_im_context_reset (GtkIMContext *context)
{
    printf ("Call to: kle_im_context_reset\n");
}

