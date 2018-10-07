/*
 * Copiright (C) 2018 Santiago León O.
 */
#include <gtk/gtk.h>
#include <gtk/gtkimmodule.h>
#include "kle_im_context.h"

#define CONTEXT_ID "kle_xkb_im"
static const GtkIMContextInfo context_info =
    {CONTEXT_ID, "Minimal input method", CONTEXT_ID, "/usr/share/locale", "*"};
static const GtkIMContextInfo* context_info_list[] = {&context_info};

void im_module_init(GTypeModule *module)
{
    g_assert  (module != NULL);

    printf ("Initializing kle_xkb_im in GTK%d\n", GTK_MAJOR_VERSION);
    kle_im_context_register_type_external (module);
}

void im_module_exit(void) {
    //printf ("Closing Module\n");
}

void im_module_list(const GtkIMContextInfo ***contexts, int *n_contexts)
{
    //printf ("Listing Contexts\n");
    *contexts = context_info_list;
    *n_contexts = 1;
}

GtkIMContext* im_module_create(const gchar *context_id)
{
    if (context_id != NULL && strcmp (context_id, CONTEXT_ID) == 0) {
        //printf ("Creating Context: %s\n", context_id);
        return GTK_IM_CONTEXT(kle_im_context_new ());
    }
    return NULL;
}

