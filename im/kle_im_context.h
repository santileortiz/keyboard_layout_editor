/*
 * Copiright (C) 2018 Santiago León O.
 */
#if !defined(KLE_IM_CONTEXT_H)

G_BEGIN_DECLS

#define KLE_TYPE_IM_CONTEXT (kle_im_context_get_type())
G_DECLARE_FINAL_TYPE (KleIMContext, kle_im_context, KLE, IM_CONTEXT, GtkIMContext)

KleIMContext* kle_im_context_new (void);
void kle_im_context_register_type (GTypeModule *module);

G_END_DECLS

#define KLE_IM_CONTEXT_H
#endif
