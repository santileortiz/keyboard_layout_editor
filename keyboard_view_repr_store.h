/*
 * Copiright (C) 2018 Santiago León O.
 */

struct kv_repr_state_t {
    char *repr;

    struct kv_repr_state_t *next;
};

struct kv_repr_t {
    bool is_internal;
    char *name;
    struct kv_repr_state_t *states;
    struct kv_repr_state_t *last_state;

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
struct kv_repr_t* kv_repr_get_by_name (struct kv_repr_store_t *store, const char *name);
void kv_repr_push_state (struct kv_repr_store_t *store, struct kv_repr_t *repr, char *str);
