/*
 * Copiright (C) 2018 Santiago León O.
 */

struct kv_repr_t {
    bool is_internal;
    bool saved;
    char *name;
    char *repr;

    struct kv_repr_t *next;
};

struct kv_repr_store_t {
    mem_pool_t pool;
    struct kv_repr_t *reprs;
    struct kv_repr_t *last_repr;
    struct kv_repr_t *curr_repr;
};

struct kv_repr_store_t* kv_repr_store_new ();
void kv_repr_store_destroy (struct kv_repr_store_t *store);
