/*
 * Copiright (C) 2018 Santiago León O.
 */
#include <gtk/gtk.h>
#include "kle_im_context.h"

struct _KleIMContext {
    GtkIMContext parent_instance;
};

static void kle_im_context_finalize (GObject *obj);
static gboolean kle_im_context_filter_keypress (GtkIMContext *context, GdkEventKey *event);

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
    gtk_im_context_class->filter_keypress = kle_im_context_filter_keypress;
}

KleIMContext*
kle_im_context_new (void)
{
    return KLE_IM_CONTEXT(g_object_new (KLE_TYPE_IM_CONTEXT, NULL));
}

static void
kle_im_context_class_finalize (KleIMContextClass *class)
{
    //printf ("Call to: kle_im_context_class_finalize\n");
}

static void
kle_im_context_init (KleIMContext *context)
{
    //printf ("Call to: kle_im_context_init\n");
}

static void
kle_im_context_finalize (GObject *context)
{
    //printf ("Call to: kle_im_context_finalize\n");
}

static gboolean
kle_im_context_filter_keypress (GtkIMContext *context, GdkEventKey *event)
{
    //printf ("Call to: kle_im_context_filter_keypress\n");
    gunichar uni = gdk_keyval_to_unicode (event->keyval);

    if (!g_unichar_iscntrl (uni)) {
        gchar buff[10];
        int len = g_unichar_to_utf8 (uni, buff);
        buff[len] = '\0';

        if (*buff != '\0' && event->type == GDK_KEY_PRESS ) {
            g_signal_emit_by_name (context, "commit", &buff);
            return TRUE;
        }
    }

    return FALSE;
}

