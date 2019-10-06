/*
 * Copiright (C) 2019 Santiago León O.
 */

// There are 2 versions of the string representations the ones created by
// kv_to_string() contain the minimum information necessary to be stored and
// parsed back using kv_set_from_string(). On the other hand strings generated
// by kv_to_string_debug() contain extra information about the state of the
// keyboard that can be computed (like internal glue) or information that
// changes constantly like key type representing a pressed key.
// @keyboard_string_formats
#define kv_to_string(pool,kv) kv_to_string_full(pool,kv,false)
#define kv_to_string_debug(pool,kv) kv_to_string_full(pool,kv,true)
char* kv_to_string_full (mem_pool_t *pool, struct keyboard_view_t *kv, bool full);

void kv_print (struct keyboard_view_t *kv);
void kv_set_from_string (struct keyboard_view_t *kv, char *str);

