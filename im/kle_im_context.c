/*
 * Copiright (C) 2018 Santiago León O.
 */
#include <gtk/gtk.h>
#include "kle_im_context.h"

struct _KleIMContext {
    GtkIMContext parent_instance;
};

G_DEFINE_TYPE (KleIMContext, kle_im_context, GTK_TYPE_IM_CONTEXT);

gboolean kle_im_context_filter_keypress (GtkIMContext *context, GdkEventKey *event);

static void kle_im_context_class_init (KleIMContextClass *class)
{
    GtkIMContextClass *gtk_im_context_class = GTK_IM_CONTEXT_CLASS(class);

    gtk_im_context_class->filter_keypress = kle_im_context_filter_keypress;
}

static void kle_im_context_init (KleIMContext *instance)
{
}

void kle_im_context_register_type (GTypeModule *module)
{
    GTypeInfo kle_im_context_type_info = {
        sizeof(KleIMContextClass),                  /* class_size */
        NULL,  /* base_init */
        NULL,  /* base_finalize */
        (GClassInitFunc) kle_im_context_class_init, /* class_init */
        NULL,  /* class_finalize */
        NULL,  /* class_data */
        sizeof(KleIMContext),                       /* instance_size */
        0,     /* n_preallocs */
        (GInstanceInitFunc) kle_im_context_init,    /* instance_init */
        NULL   /* value_table */
    };

    g_type_module_register_type (module, GTK_TYPE_IM_CONTEXT, "KleIMContext", &kle_im_context_type_info, 0);
}

KleIMContext* kle_im_context_new (void)
{
    return g_object_new (KLE_TYPE_IM_CONTEXT, NULL);
}

gboolean kle_im_context_filter_keypress (GtkIMContext *context, GdkEventKey *event)
{
    printf ("Filtering in XKB IM!\n");
    return FALSE;
}
