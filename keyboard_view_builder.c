/*
 * Copiright (C) 2019 Santiago León O.
 */

// This component of the keyboard view is wat allows creating a keyboard view
// data structure programatically.
//
// TODO: I separated the builder state from the keyboard view into
// geometry_edit_ctx_t, I now think this was a mistake as it adds mental
// overhead to the caller of keyboard_view. The reason I did this was that
// maintaining the end pointers to the row list and segment list was becomming
// very cumbersome and error prone (I don't have automated test cases that
// detect when an end pointer needed to be updated but wasn't).  Separating this
// state into it's own struct forced the caller call
// kv_geometry_ctx_init_append() and compute these values just before they start
// appending stuff to the view. I now think a better compromise is to put the
// state inside keyboard_view_t but still require a call to
// kv_geometry_ctx_init_append() before calling any view modifying functions. We
// would need to detect this somehow and faíl if the user didn't do this. Later,
// when we know the editing tools are not going to change, we can find out all
// cases where these end pointers can change, update them accordingly, tests for
// these special cases and remove kv_geometry_ctx_init_append().

static inline
bool kv_is_view_empty (struct keyboard_view_t *kv)
{
    return kv->first_row == NULL;
}

static inline
bool is_multirow_key (struct sgmt_t *key)
{
    return key->next_multirow != key;
}

// NOTE: Assumes is_multirow(key) == true.
static inline
bool is_multirow_parent (struct sgmt_t *key)
{
    return key->type != KEY_MULTIROW_SEGMENT && key->type != KEY_MULTIROW_SEGMENT_SIZED;
}

struct sgmt_t* kv_get_multirow_parent (struct sgmt_t *key)
{
    while (!is_multirow_parent (key)) {
        key = key->next_multirow;
    }
    return key;
}

static inline
int kv_get_num_rows (struct keyboard_view_t *kv)
{
    int num_rows = 0;
    struct row_t *r = kv->first_row;
    while (r != NULL) {
        num_rows++;
        r = r->next_row;
    }
    return num_rows;
}

float get_sgmt_width (struct sgmt_t *sgmt) {
    float width = 0;
    if (sgmt->type == KEY_MULTIROW_SEGMENT) {
        struct sgmt_t *curr_key = sgmt->next_multirow;
        do {
            if (curr_key->type != KEY_MULTIROW_SEGMENT) {
                width = curr_key->width;
            }

            curr_key = curr_key->next_multirow;
        } while (curr_key != sgmt->next_multirow);

    } else {
        width = sgmt->width;
    }

    return width;
}

struct row_t* kv_allocate_row (struct keyboard_view_t *kv)
{
    struct row_t *new_row;
    if (kv->spare_rows == NULL) {
        new_row = (struct row_t*)pom_push_struct (&kv->keyboard_pool, struct row_t);
    } else {
        new_row = kv->spare_rows;
        kv->spare_rows = new_row->next_row;
    }

    *new_row = (struct row_t){0};
    new_row->height = 1;
    return new_row;
}

struct sgmt_t* kv_allocate_key (struct keyboard_view_t *kv)
{
    struct sgmt_t *new_key;
    if (kv->spare_keys == NULL) {
        new_key = (struct sgmt_t*)pom_push_struct (&kv->keyboard_pool, struct sgmt_t);
    } else {
        new_key = kv->spare_keys;
        kv->spare_keys = new_key->next_sgmt;
    }

    *new_key = (struct sgmt_t){0};
    new_key->next_multirow = new_key; // This one is not NULL initialized!
    return new_key;
}

// A keyboard view created with this is useful to use the data structure
// programatically without having any GUI. Used to test the parser/writer of the
// keyboard view string representation.
struct keyboard_view_t* kv_new ()
{
    mem_pool_t bootstrap_pool = {0};
    mem_pool_t *pool = mem_pool_push_size (&bootstrap_pool, sizeof(mem_pool_t));
    *pool = bootstrap_pool;

    struct keyboard_view_t *kv = mem_pool_push_size (pool, sizeof(struct keyboard_view_t));
    *kv = ZERO_INIT(struct keyboard_view_t);
    kv->pool = pool;

    return kv;
}

void kv_clear (struct keyboard_view_t *kv)
{
    mem_pool_destroy (&kv->keyboard_pool);
    kv->keyboard_pool = ZERO_INIT(mem_pool_t);

    memset(kv->keys_by_kc, 0, sizeof(kv->keys_by_kc));
    kv->spare_keys = NULL;
    kv->spare_rows = NULL;
    kv->first_row = NULL;
}

void keyboard_view_destroy (struct keyboard_view_t *kv)
{
    mem_pool_destroy (&kv->keyboard_pool);
    mem_pool_destroy (&kv->tooltips_pool);
    mem_pool_destroy (&kv->resize_pool);
    mem_pool_destroy (kv->pool);
}

struct key_state_t {
    int count;
    float parent_left; // left border of the multirow parent
    float left, right; // left and right borders of the current multirow segment
    struct sgmt_t *parent;
    int parent_idx;
};

struct row_state_t {
    struct sgmt_t *curr_key;
    float width;
};

// NOTE: This function only modifies the internal glue for multirow keys, it's
// expected that all other keys will have internal_glue == 0.
void kv_compute_glue (struct keyboard_view_t *kv)
{
    int num_rows = kv_get_num_rows (kv);

    struct key_state_t keys_state[num_rows];
    {
        int i;
        for (i=0; i<num_rows; i++) {
            keys_state[i] = ZERO_INIT(struct key_state_t);
        }
    }

    struct row_state_t rows_state[num_rows];
    int row_idx = 0;
    {
        struct row_t *curr_row = kv->first_row;
        while (curr_row != NULL) {
            rows_state[row_idx].curr_key = curr_row->first_key;
            rows_state[row_idx].width = 0;
            curr_row = curr_row->next_row;
            row_idx++;
        }
    }

    int done_rows = 0;
    row_idx = 0;
    while (done_rows < num_rows) {
        assert (row_idx < num_rows);
        struct sgmt_t *curr_key = rows_state[row_idx].curr_key;
        while (curr_key && !is_multirow_key(curr_key)) {
            rows_state[row_idx].width += curr_key->width + curr_key->user_glue;
            curr_key = curr_key->next_sgmt;
        }

        if (curr_key != NULL) {
            // Move row state to the segment after the multirow segment that
            // will be processed.
            rows_state[row_idx].curr_key = curr_key->next_sgmt;

        } else {
            // Reached the end of a row
            done_rows++;
            row_idx++;
            continue;
        }

        // Process the found multirow segment
        if (is_multirow_parent (curr_key) ) {
            // If it's a multirow parent we create a new multirow state and add
            // it to keys_state indexed by the row id of the parent.
            struct key_state_t *new_state = &keys_state[row_idx];
            new_state->parent = curr_key;
            new_state->parent_idx = row_idx;
            new_state->parent_left = rows_state[row_idx].width;
            new_state->left = rows_state[row_idx].width;
            new_state->right = new_state->left + curr_key->width;

            int len = 0;
            {
                struct sgmt_t *tmp_key = curr_key;
                do {
                    len++;
                    tmp_key = tmp_key->next_multirow;
                } while (tmp_key != curr_key);
            }
            new_state->count = len - 1;

            row_idx++;

        } else {
            // If it's a multirow segment then update the corresponding key
            // state and if all multirow segments have been found, compute the
            // glue for them and jump to the parent's row and continue from
            // there.

            struct key_state_t *key_state = NULL;
            {
                // Is there a way to find key_state in constant time?, In
                // practice I don't think this will be a problem as keyboards
                // don't tend to have a lot of multirow keys.
                struct sgmt_t *parent = kv_get_multirow_parent(curr_key);
                int i;
                for (i=0; i<num_rows; i++) {
                    if (keys_state[i].parent == parent) {
                        key_state = &keys_state[i];
                        break;
                    }
                }
            }

            if (key_state != NULL) {
                // Detect if the current multirow segment collides with the row
                // by updating key_state->left and key_state->right. If it does
                // collide then update key_state->parent_left.
                if (curr_key->type == KEY_MULTIROW_SEGMENT) {
                    if (key_state->left < rows_state[row_idx].width) {
                        key_state->parent_left += rows_state[row_idx].width - key_state->left;

                        // Move left and right by rows_state[row_idx].width - key_state->left
                        key_state->right += rows_state[row_idx].width - key_state->left;
                        key_state->left = rows_state[row_idx].width;
                    }

                } else { // curr_key->type == KEY_MULTIROW_SEGMENT_SIZED
                    if (curr_key->align == MULTIROW_ALIGN_LEFT) {
                        if (key_state->left < rows_state[row_idx].width) {
                            key_state->parent_left += rows_state[row_idx].width - key_state->left;

                            key_state->left = rows_state[row_idx].width;
                            key_state->right = key_state->left + curr_key->width;

                        } else {
                            key_state->right = key_state->left + curr_key->width;
                        }

                    } else { // curr_key->align == MULTIROW_ALIGN_RIGHT
                        if (key_state->right - curr_key->width < rows_state[row_idx].width) {
                            key_state->parent_left += rows_state[row_idx].width -
                                                     (key_state->right - curr_key->width);

                            key_state->left = rows_state[row_idx].width;
                            key_state->right = key_state->left + curr_key->width;

                        } else {
                            key_state->left = key_state->right - curr_key->width;
                        }
                    }
                }

                key_state->count--;
                if (key_state->count == 0) {
                    // Based on the computed key_state->parent_left compute the
                    // glue for all segments and update the row width.
                    int r_i = key_state->parent_idx;
                    struct sgmt_t *sgmt = key_state->parent;
                    float left = key_state->parent_left + key_state->parent->user_glue;
                    float right = left + sgmt->width;
                    do {
                        if (sgmt->type == KEY_MULTIROW_SEGMENT_SIZED) {
                            if (sgmt->align == MULTIROW_ALIGN_LEFT) {
                                right = left + sgmt->width;
                            } else { // sgmt->align == MULTIROW_ALIGN_RIGHT
                                left = right - sgmt->width;
                            }
                        }

                        sgmt->internal_glue = left - rows_state[r_i].width - key_state->parent->user_glue;
                        rows_state[r_i].width = right;
                        r_i++;
                        sgmt = sgmt->next_multirow;
                    } while (sgmt != key_state->parent);

                    // Continue processing at the multirow parent's row.
                    row_idx = key_state->parent_idx;
                } else {
                    row_idx++;
                }
            }
        }
    }
}

struct geometry_edit_ctx_t {
    struct sgmt_t *last_key;
    struct row_t *last_row;
    struct keyboard_view_t *kv;
};

void kv_geometry_ctx_init_append (struct keyboard_view_t *kv, struct geometry_edit_ctx_t *ctx)
{
    *ctx = ZERO_INIT (struct geometry_edit_ctx_t);
    struct row_t *last_row = NULL;
    if (!kv_is_view_empty (kv)) {
        last_row = kv->first_row;
        while (last_row->next_row != NULL) {
            last_row = last_row->next_row;
        }
    }

    struct sgmt_t *last_key = NULL;
    if (last_row && last_row->first_key) {
        last_key = last_row->first_key;
        while (last_key->next_sgmt != NULL) {
            last_key = last_key->next_sgmt;
        }
    }

    ctx->last_key = last_key;
    ctx->last_row = last_row;
    ctx->kv = kv;
}

void kv_end_geometry (struct geometry_edit_ctx_t *ctx)
{
    kv_compute_glue (ctx->kv);
}

#define kv_new_row(kbd) kv_new_row_h(kbd,1)
void kv_new_row_h (struct geometry_edit_ctx_t *ctx, float height)
{
    struct keyboard_view_t *kv = ctx->kv;

    struct row_t *new_row = kv_allocate_row (kv);
    *new_row = ZERO_INIT (struct row_t);
    new_row->height = height;

    if (!kv_is_view_empty (kv)) {
        ctx->last_row->next_row = new_row;
    } else {
        kv->first_row = new_row;
    }

    ctx->last_row = new_row;
    ctx->last_key = NULL;
}

#define kv_add_key(ctx,keycode)     kv_add_key_full(ctx,keycode,1,0)
#define kv_add_key_w(ctx,keycode,w) kv_add_key_full(ctx,keycode,w,0)
struct sgmt_t* kv_add_key_full (struct geometry_edit_ctx_t *ctx,
                               int keycode, float width, float glue)
{
    struct keyboard_view_t *kv = ctx->kv;

    struct sgmt_t *new_key = kv_allocate_key (kv);
    new_key->width = width;
    new_key->user_glue = glue;

    if (0 < keycode && keycode < KEY_CNT) {
        kv->keys_by_kc[keycode] = new_key;
    } else {
        keycode = 0;
    }
    new_key->kc = keycode;

    struct row_t *curr_row = ctx->last_row;
    assert (curr_row != NULL && "Must create a row before adding a key.");

    if (ctx->last_key != NULL) {
        ctx->last_key->next_sgmt = new_key;
    } else {
        curr_row->first_key = new_key;
    }

    ctx->last_key = new_key;

    return new_key;
}

#define kv_add_multirow_sgmt(ctx,key) kv_add_multirow_sized_sgmt(ctx,key,0,0)
void kv_add_multirow_sized_sgmt (struct geometry_edit_ctx_t *ctx,
                                 struct sgmt_t *key,
                                 float width, enum multirow_key_align_t align)
{
    // look for the last multirow segment so new_key is added after it.
    while (!is_multirow_parent (key->next_multirow)) {
        key = key->next_multirow;
    }

    struct sgmt_t *new_key;
    if (width == 0 || width == get_sgmt_width (key)) {
        new_key = kv_add_key_w (ctx, -1, 0);
        new_key->type = KEY_MULTIROW_SEGMENT;

    } else {
        new_key = kv_add_key_w (ctx, -1, width);
        new_key->type = KEY_MULTIROW_SEGMENT_SIZED;
        new_key->align = align;
    }

    new_key->next_multirow = key->next_multirow;
    key->next_multirow = new_key;
}

