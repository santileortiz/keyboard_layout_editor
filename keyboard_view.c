/*
 * Copiright (C) 2018 Santiago León O.
 */

enum keyboard_view_state_t {
    KV_PREVIEW,
    KV_EDIT,
    KV_EDIT_KEYCODE_KEYPRESS,
    KV_EDIT_KEY_SPLIT,
    KV_EDIT_KEY_RESIZE,
    KV_EDIT_KEY_RESIZE_NON_RECTANGULAR,
    //KV_EDIT_KEYCODE_LOOKUP,
    //KV_EDIT_KEY_POSITION
};

enum keyboard_view_commands_t {
    KV_CMD_NONE,
    KV_CMD_SET_MODE_PREVIEW,
    KV_CMD_SET_MODE_EDIT,
    KV_CMD_SPLIT_KEY
};

enum keyboard_view_tools_t {
    KV_TOOL_KEYCODE_KEYPRESS,
    KV_TOOL_SPLIT_KEY,
    KV_TOOL_RESIZE_KEY,
    KV_TOOL_DELETE_KEY
};

enum keyboard_view_label_mode_t {
    KV_KEYSYM_LABELS,
    KV_KEYCODE_LABELS,
};

enum key_render_type_t {
    KEY_DEFAULT,
    KEY_PRESSED,
    KEY_UNASSIGNED,
    KEY_MULTIROW_SEGMENT, // Inherits glue and width from parent
    KEY_MULTIROW_SEGMENT_SIZED
};

enum multirow_key_align_t {
    MULTIROW_ALIGN_LEFT,
    MULTIROW_ALIGN_RIGHT
};

struct key_t {
    int kc; //keycode
    float width, user_glue; // normalized to default_key_size
    float internal_glue;
    enum key_render_type_t type;
    struct key_t *next_key;
    struct key_t *next_multirow;

    // Fields specific to KEY_MULTIROW_SEGMENT_SIZED
    enum multirow_key_align_t align;
};

struct row_t {
    float height; // normalized to default_key_size
    struct row_t *next_row;

    struct key_t *first_key;
    struct key_t *last_key;
};

struct manual_tooltip_t {
    struct manual_tooltip_t *next;
    GdkRectangle rect;
    char *text;
};

struct keyboard_view_t {
    mem_pool_t *pool;

    // Array of key_t pointers indexed by keycode. Provides fast access to keys
    // from a keycode. It's about 6KB in memory, maybe too much?
    struct key_t *keys_by_kc[KEY_MAX];
    struct key_t *spare_keys;

    // For now we represent the geometry of the keyboard by having a linked list
    // of rows, each of which is a linked list of keys.
    struct row_t *first_row;
    struct row_t *last_row;

    // xkbcommon state
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;

    // KEY_SPLIT state
    struct key_t *new_key;
    struct key_t *splitted_key;
    struct key_t **splitted_key_ptr;
    struct key_t *after_key;
    GdkRectangle split_key_rect;

    // KEY_RESIZE state
    struct key_t *resized_key;
    struct key_t *edge_start_sgmt, *edge_end_sgmt;
    float x_clicked_pos;
    float x_anchor;
    float original_w;
    float original_user_glue;
    float min_width;
    bool resize_right;

    // Manual tooltips list
    mem_pool_t tooltips_pool;
    struct manual_tooltip_t *first_tooltip;
    struct manual_tooltip_t *last_tooltip;

    // GUI related information and state
    GtkWidget *widget;
    GtkWidget *toolbar;
    float default_key_size; // In pixels
    int clicked_kc;
    struct key_t *selected_key;
    enum keyboard_view_state_t state;
    enum keyboard_view_label_mode_t label_mode;
    enum keyboard_view_tools_t active_tool;

    GdkRectangle debug_rect;
};

static inline
bool is_multirow_key (struct key_t *key)
{
    return key->next_multirow != key;
}

// NOTE: Assumes is_multirow(key) == true.
static inline
bool is_multirow_parent (struct key_t *key)
{
    return key->type != KEY_MULTIROW_SEGMENT && key->type != KEY_MULTIROW_SEGMENT_SIZED;
}

struct key_t* kv_get_multirow_parent (struct key_t *key)
{
    while (!is_multirow_parent (key)) {
        key = key->next_multirow;
    }
    return key;
}

#define kv_new_row(kbd) kv_new_row_h(kbd,1)
void kv_new_row_h (struct keyboard_view_t *kv, float height)
{
    struct row_t *new_row = (struct row_t*)pom_push_struct (kv->pool, struct row_t);
    *new_row = (struct row_t){0};
    new_row->height = height;

    if (kv->last_row != NULL) {
        kv->last_row->next_row = new_row;
        kv->last_row = new_row;

    } else {
        kv->first_row = new_row;
        kv->last_row = new_row;
    }
}

void kv_remove_key_sgmt (struct keyboard_view_t *kv, struct key_t **key_ptr)
{
    assert (key_ptr != NULL);

    // NOTE: This does not reset to 0 the content of the deleted segment because
    // multirow deletion needs the next_multirow pointers. Clearing is done at
    // kv_allocate_key().

    struct key_t *tmp = (*key_ptr)->next_key;
    (*key_ptr)->next_key = kv->spare_keys;
    kv->spare_keys = *key_ptr;
    *key_ptr = tmp;
}

void kv_remove_key (struct keyboard_view_t *kv, struct key_t **key_ptr)
{
    assert (key_ptr != NULL);

    if (is_multirow_key(*key_ptr)) {
        // Rows are singly linked lists so we don't have the pointers to the
        // parent of each multirow segment. We iterate the whole keyboard to
        // delete a multirow key. I don't think this will be a performance issue
        // as this should not be the most common usecase.

        struct key_t *multirow_parent = kv_get_multirow_parent(*key_ptr);

        // Find the row in which multirow_parent is located.
        struct row_t *curr_row = kv->first_row;
        {
            while (curr_row != NULL) {
                struct key_t *curr_key = curr_row->first_key;
                while (curr_key != NULL) {
                    if (curr_key == multirow_parent) {
                        break;
                    }
                    curr_key = curr_key->next_key;
                }

                if (curr_key != NULL) {
                    break;
                }
                curr_row = curr_row->next_row;
            }
        }

        // Find the parent pointer to each segment and delete it.
        struct key_t *sgmt = multirow_parent;
        do {
            struct key_t **to_delete = &curr_row->first_key;
            while (*to_delete != sgmt) {
                to_delete = &(*to_delete)->next_key;
            }
            kv_remove_key_sgmt (kv, to_delete);

            curr_row = curr_row->next_row;
            sgmt = sgmt->next_multirow;

        } while (sgmt != multirow_parent);

    } else {
        kv_remove_key_sgmt (kv, key_ptr);
    }
}

struct key_t* kv_allocate_key (struct keyboard_view_t *kv)
{
    struct key_t *new_key;
    if (kv->spare_keys == NULL) {
        new_key = (struct key_t*)pom_push_struct (kv->pool, struct key_t);
    } else {
        new_key = kv->spare_keys;
        kv->spare_keys = new_key->next_key;
    }

    *new_key = (struct key_t){0};
    new_key->next_multirow = new_key; // This one is not NULL initialized!
    return new_key;
}

#define kv_add_key(kbd,keycode)     kv_add_key_full(kbd,keycode,1,0)
#define kv_add_key_w(kbd,keycode,w) kv_add_key_full(kbd,keycode,w,0)
struct key_t* kv_add_key_full (struct keyboard_view_t *kv, int keycode,
                               float width, float glue)
{
    struct key_t *new_key = kv_allocate_key (kv);
    new_key->width = width;
    new_key->user_glue = glue;

    if (0 < keycode && keycode < KEY_CNT) {
        kv->keys_by_kc[keycode] = new_key;
    } else {
        new_key->type = KEY_UNASSIGNED;
        keycode = 0;
    }
    new_key->kc = keycode;

    struct row_t *curr_row = kv->last_row;
    assert (curr_row != NULL && "Must create a row before adding a key.");

    if (curr_row->last_key != NULL) {
        curr_row->last_key->next_key = new_key;
        curr_row->last_key = new_key;

    } else {
        curr_row->first_key = new_key;
        curr_row->last_key = new_key;
    }

    return new_key;
}

float get_multirow_segment_width (struct key_t *key) {
    assert (key->type == KEY_MULTIROW_SEGMENT);

    float multirow_key_width = 0;
    struct key_t *curr_key = key->next_multirow;
    do {
        if (curr_key->type != KEY_MULTIROW_SEGMENT) {
            multirow_key_width = curr_key->width;
        }

        curr_key = curr_key->next_multirow;
    } while (curr_key != key->next_multirow);

    return multirow_key_width;
}

void kv_get_size (struct keyboard_view_t *kv, double *width, double *height)
{
    struct row_t *curr_row = kv->first_row;

    double w = 0;
    double h = 0;
    while (curr_row != NULL) {
        h += curr_row->height*kv->default_key_size;

        double row_w = 0;
        struct key_t *curr_key = curr_row->first_key;
        while (curr_key != NULL) {
            row_w += (curr_key->internal_glue + curr_key->user_glue)*kv->default_key_size;
            if (curr_key->type == KEY_MULTIROW_SEGMENT) {
                row_w += get_multirow_segment_width(curr_key)*kv->default_key_size;
            } else {
                row_w += curr_key->width*kv->default_key_size;
            }
            curr_key = curr_key->next_key;
        }

        if (row_w > w) {
            w = row_w;
        }

        curr_row = curr_row->next_row;
    }

    if (width != NULL) {
        *width = w;
    }

    if (height != NULL) {
        *height = h;
    }
}

// Compute the width and height in pixels of a key and return wether or not the
// key is rectangular.
//
// If _key_ is part of a multirow key, but it's still rectanglar (the multirow
// list has no segment of type KEY_MULTIROW_SEGMENT_SIZED) _width_ and _height_
// are set to the size of the full multirow key and true is returned. Otherwise,
// _width_ and _height_ are set to the segment of the multirow key represented
// by _key_ and false is returned.
//
// _multirow_y_offset_ is set to the distance in pixels from the top left of the
// multirow parent and the top left of _key_.
#define compute_key_size(kv,key,row,w,h) compute_key_size_full(kv,key,row,w,h,NULL)
bool compute_key_size_full (struct keyboard_view_t *kv, struct key_t *key, struct row_t *row,
                            float *width, float *height, float *multirow_y_offset)
{
    assert (width != NULL && height != NULL);

    float dummy;
    if (multirow_y_offset == NULL) {
        multirow_y_offset = &dummy;
    }
    *multirow_y_offset = 0;

    bool is_rectangular = true;
    float multirow_key_height = 0;

    if (is_multirow_key (key)) {
        // Computing the size of a multirow key is a bit contrived for several
        // reasons. First, height is stored in the row not in the key. Second,
        // _key_ can be any segment of the multirrow key. Third, non rectangular
        // multirow keys, inherit the width from the previous segment with size
        // (either a KEY_MULTIROW_SEGMENT_SIZED or the multirow parent). We
        // compute the size in three steps:

        // 1) Iterate the multirow key. Decide if it's rectangular and if so,
        // compute the number of rows spanned by it and the index of _key_ in
        // the multirow list, starting from the multirow parent.
        int key_num_rows = 0, key_offset = 0;

        // NOTE: Start from key->next_multirow because _key_ must be the last
        // element in the iteration. This handles the case for key_offset where
        // _key_ is the parent.
        struct key_t *curr_key = key->next_multirow;
        do {
            if (curr_key->type == KEY_MULTIROW_SEGMENT_SIZED) {
                is_rectangular = false;
                break;
            }

            if (is_multirow_parent(curr_key)) {
                key_offset = 0;
            } else {
                key_offset++;
            }

            key_num_rows++;

            curr_key = curr_key->next_multirow;
        } while (curr_key != key->next_multirow);

        if (is_rectangular) {
            // 2) Compute the index of _row_ in the row list of _kv_.
            struct row_t *curr_row = kv->first_row;
            int row_idx = 0;
            while (curr_row != row) {
                row_idx++;
                curr_row = curr_row->next_row;
            }

            // 3) Add heights for the rows in the multirow key and compute the
            // total height and the multirow_y_offset.
            int i = 0;
            int multirow_parent_idx = row_idx - key_offset;
            curr_row = kv->first_row;
            while (curr_row != NULL && i < multirow_parent_idx + key_num_rows) {
                if (i >= multirow_parent_idx && i < row_idx) {
                    *multirow_y_offset += curr_row->height;
                }

                if (i >= multirow_parent_idx) {
                    multirow_key_height += curr_row->height;
                }

                curr_row = curr_row->next_row;
                i++;
            }
        }
    }

    *multirow_y_offset *= kv->default_key_size;

    if (is_rectangular && is_multirow_key (key)) {
        *height = multirow_key_height*kv->default_key_size;
    } else {
        *height = row->height*kv->default_key_size;
    }

    if (key->type == KEY_MULTIROW_SEGMENT) {
        // Compute the inherited width of the multirow key segment.
        *width = get_multirow_segment_width (key)*kv->default_key_size;
    } else {
        *width = key->width*kv->default_key_size;
    }

    return is_rectangular;
}

// Simple default keyboard geometry.
// NOTE: Keycodes are used as defined in the linux kernel. To translate them
// into X11 keycodes offset them by 8 (x11_kc = kc+8).
void keyboard_view_build_default_geometry (struct keyboard_view_t *kv)
{
    kv->default_key_size = 56; // Should be divisible by 4 so everything is pixel perfect
    kv_new_row (kv);
    kv_add_key (kv, KEY_ESC);
    kv_add_key (kv, KEY_F1);
    kv_add_key (kv, KEY_F2);
    kv_add_key (kv, KEY_F3);
    kv_add_key (kv, KEY_F4);
    kv_add_key (kv, KEY_F5);
    kv_add_key (kv, KEY_F6);
    kv_add_key (kv, KEY_F7);
    kv_add_key (kv, KEY_F8);
    kv_add_key (kv, KEY_F9);
    kv_add_key (kv, KEY_F10);
    kv_add_key (kv, KEY_F11);
    kv_add_key (kv, KEY_F12);
    kv_add_key (kv, KEY_NUMLOCK);
    kv_add_key (kv, KEY_SCROLLLOCK);
    kv_add_key (kv, KEY_INSERT);

    kv_new_row (kv);
    kv_add_key (kv, KEY_GRAVE);
    kv_add_key (kv, KEY_1);
    kv_add_key (kv, KEY_2);
    kv_add_key (kv, KEY_3);
    kv_add_key (kv, KEY_4);
    kv_add_key (kv, KEY_5);
    kv_add_key (kv, KEY_6);
    kv_add_key (kv, KEY_7);
    kv_add_key (kv, KEY_8);
    kv_add_key (kv, KEY_9);
    kv_add_key (kv, KEY_0);
    kv_add_key (kv, KEY_MINUS);
    kv_add_key (kv, KEY_EQUAL);
    kv_add_key_w (kv, KEY_BACKSPACE, 2);
    kv_add_key (kv, KEY_HOME);

    kv_new_row (kv);
    kv_add_key_w (kv, KEY_TAB, 1.5);
    kv_add_key (kv, KEY_Q);
    kv_add_key (kv, KEY_W);
    kv_add_key (kv, KEY_E);
    kv_add_key (kv, KEY_R);
    kv_add_key (kv, KEY_T);
    kv_add_key (kv, KEY_Y);
    kv_add_key (kv, KEY_U);
    kv_add_key (kv, KEY_I);
    kv_add_key (kv, KEY_O);
    kv_add_key (kv, KEY_P);
    kv_add_key (kv, KEY_LEFTBRACE);
    kv_add_key (kv, KEY_RIGHTBRACE);
    kv_add_key_w (kv, KEY_BACKSLASH, 1.5);
    kv_add_key (kv, KEY_PAGEUP);

    kv_new_row (kv);
    kv_add_key_w (kv, KEY_CAPSLOCK, 1.75);
    kv_add_key (kv, KEY_A);
    kv_add_key (kv, KEY_S);
    kv_add_key (kv, KEY_D);
    kv_add_key (kv, KEY_F);
    kv_add_key (kv, KEY_G);
    kv_add_key (kv, KEY_H);
    kv_add_key (kv, KEY_J);
    kv_add_key (kv, KEY_K);
    kv_add_key (kv, KEY_L);
    kv_add_key (kv, KEY_SEMICOLON);
    kv_add_key (kv, KEY_APOSTROPHE);
    kv_add_key_w (kv, KEY_ENTER, 2.25);
    kv_add_key (kv, KEY_PAGEDOWN);

    kv_new_row (kv);
    kv_add_key_w (kv, KEY_LEFTSHIFT, 2.25);
    kv_add_key (kv, KEY_Z);
    kv_add_key (kv, KEY_X);
    kv_add_key (kv, KEY_C);
    kv_add_key (kv, KEY_V);
    kv_add_key (kv, KEY_B);
    kv_add_key (kv, KEY_N);
    kv_add_key (kv, KEY_M);
    kv_add_key (kv, KEY_COMMA);
    kv_add_key (kv, KEY_DOT);
    kv_add_key (kv, KEY_SLASH);
    kv_add_key_w (kv, KEY_RIGHTSHIFT, 1.75);
    kv_add_key (kv, KEY_UP);
    kv_add_key (kv, KEY_END);

    kv_new_row (kv);
    kv_add_key_w (kv, KEY_LEFTCTRL, 1.5);
    kv_add_key_w (kv, KEY_LEFTMETA, 1.5);
    kv_add_key_w (kv, KEY_LEFTALT, 1.5);
    kv_add_key_w (kv, KEY_SPACE, 5.5);
    kv_add_key_w (kv, KEY_RIGHTALT, 1.5);
    kv_add_key_w (kv, KEY_RIGHTCTRL, 1.5);
    kv_add_key (kv, KEY_LEFT);
    kv_add_key (kv, KEY_DOWN);
    kv_add_key (kv, KEY_RIGHT);
}

struct key_t* kv_add_key_multirow (struct keyboard_view_t *kv, struct key_t *key)
{
    struct key_t *new_key = kv_add_key_w (kv, -1, key->width);
    new_key->type = KEY_MULTIROW_SEGMENT;

    // look for the last multirow segment so new_key is added after it.
    while (!is_multirow_parent (key->next_multirow)) {
        key = key->next_multirow;
    }

    new_key->next_multirow = key->next_multirow;
    key->next_multirow = new_key;
    return new_key;
}

struct key_t* kv_add_key_multirow_sized (struct keyboard_view_t *kv, struct key_t *key,
                                float width, enum multirow_key_align_t align)
{
    struct key_t *new_key = kv_add_key_w (kv, -1, width);
    new_key->type = KEY_MULTIROW_SEGMENT_SIZED;
    new_key->align = align;

    // look for the last multirow segment so new_key is added after it.
    while (!is_multirow_parent (key->next_multirow)) {
        key = key->next_multirow;
    }

    new_key->next_multirow = key->next_multirow;
    key->next_multirow = new_key;
    return new_key;
}

struct key_state_t {
    int count;
    float parent_left; // left border of the multirow parent
    float left, right; // left and right borders of the current multirow segment
    struct key_t *parent;
    int parent_idx;
};

struct row_state_t {
    struct key_t *curr_key;
    float width;
};

void kv_compute_glue (struct keyboard_view_t *kv)
{
    int num_rows = 0;
    {
        struct row_t *r = kv->first_row;
        while (r != NULL) {
            num_rows++;
            r = r->next_row;
        }
    }

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
        struct key_t *curr_key = rows_state[row_idx].curr_key;
        while (curr_key && !is_multirow_key(curr_key)) {
            rows_state[row_idx].width += curr_key->width + curr_key->user_glue;
            curr_key = curr_key->next_key;
        }

        // Handle the end conditions.
        if (curr_key != NULL) {
            // Here the end of the row is found after a multirow key.
            rows_state[row_idx].curr_key = curr_key->next_key;
            if (curr_key->next_key == NULL) {
                done_rows++;
            }

        } else {
            // This happens when the row does not end in a multirow key.
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
                struct key_t *tmp_key = curr_key;
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
                struct key_t *parent = kv_get_multirow_parent(curr_key);
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
                    struct key_t *sgmt = key_state->parent;
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

                        sgmt->internal_glue = left - rows_state[r_i].width;
                        rows_state[r_i].width = right;
                        r_i++;
                        sgmt = sgmt->next_multirow;
                    } while (sgmt != key_state->parent);
                    key_state->parent->internal_glue -= key_state->parent->user_glue;

                    // Continue processing at the multirow parent's row.
                    row_idx = key_state->parent_idx;
                } else {
                    row_idx++;
                }
            }
        }
    }
}

void multirow_test_geometry (struct keyboard_view_t *kv)
{
    kv->default_key_size = 56; // Should be divisible by 4 so everything is pixel perfect
    kv_new_row_h (kv, 1.5);
    struct key_t *multi1 = kv_add_key (kv, KEY_A);
    kv_add_key (kv, KEY_1);
    //kv_add_key (kv, KEY_2);
    struct key_t *multi4 = kv_add_key_w (kv, KEY_D, 2);

    kv_new_row_h (kv, 1.25);
    struct key_t *multi2 = kv_add_key (kv, KEY_B);
    kv_add_key_multirow (kv, multi1);
    kv_add_key_full (kv, KEY_3, 1, 1);
    kv_add_key_multirow_sized (kv, multi4, 1, MULTIROW_ALIGN_RIGHT);

    kv_new_row_h (kv, 1);
    kv_add_key (kv, KEY_4);
    kv_add_key_multirow (kv, multi2);
    struct key_t *multi3 = kv_add_key (kv, KEY_C);
    multi4 = kv_add_key_multirow (kv, multi4);

    kv_new_row_h (kv, 0.75);
    kv_add_key (kv, KEY_5);
    kv_add_key (kv, KEY_6);
    kv_add_key_multirow (kv, multi3);
    kv_add_key_multirow_sized (kv, multi4, 3, MULTIROW_ALIGN_RIGHT);

    kv_compute_glue (kv);
}

void cr_render_key_label (cairo_t *cr, const char *label, double x, double y, double width, double height)
{
    PangoLayout *text_layout;
    {
        PangoFontDescription *font_desc = pango_font_description_new ();
        pango_font_description_set_family (font_desc, "Open Sans");
        pango_font_description_set_size (font_desc, 13*PANGO_SCALE);
        pango_font_description_set_weight (font_desc, PANGO_WEIGHT_NORMAL);

        text_layout = pango_cairo_create_layout (cr);
        pango_layout_set_font_description (text_layout, font_desc);
        pango_font_description_free(font_desc);
    }

    pango_layout_set_text (text_layout, label, strlen(label));

    // Reduce font size untill the lable fits inside the key.
    // NOTE: This should be just a fallback for special cases, most keys should
    // have a lable that fits inside them.
    PangoRectangle logical;
    int font_size = 13;
    pango_layout_get_pixel_extents (text_layout, NULL, &logical);
    while ((logical.width + 4 >= width || logical.height >= height) && font_size > 0) {
        font_size--;
        PangoFontDescription *font_desc =
            pango_font_description_copy (pango_layout_get_font_description (text_layout));
        pango_font_description_set_size (font_desc, font_size*PANGO_SCALE);
        pango_layout_set_font_description (text_layout, font_desc);
        pango_layout_get_pixel_extents (text_layout, NULL, &logical);
        pango_font_description_free(font_desc);
    }

    if (logical.width < width && logical.height < height) {
        double text_x_pos = x + (width - logical.width)/2;
        double text_y_pos = y + (height - logical.height)/2;

        cairo_set_source_rgb (cr, 0,0,0);
        cairo_move_to (cr, text_x_pos, text_y_pos);
        pango_cairo_show_layout (cr, text_layout);

    } else {
        // We don't want to resize keys so we instead should make sure that this
        // never happens.
        printf ("Skipping rendering for label: %s\n", label);
    }
    g_object_unref (text_layout);
}

#define KEY_LEFT_MARGIN 5
#define KEY_TOP_MARGIN 2
#define KEY_CORNER_RADIUS 5
void cr_render_key (cairo_t *cr, double x, double y, double width, double height,
                    const char *label, dvec4 color)
{
    cr_rounded_box (cr, x+0.5, y+0.5,
                    width-1,
                    height-1,
                    KEY_CORNER_RADIUS);
    cairo_set_source_rgb (cr, ARGS_RGB(color));
    cairo_fill_preserve (cr);

    cairo_set_source_rgba (cr, 0, 0, 0, 0.05);
    cairo_fill_preserve (cr);

    cairo_set_source_rgb (cr, 0, 0, 0);
    cairo_stroke (cr);

    float cap_x = x+KEY_LEFT_MARGIN+0.5;
    float cap_y = y+KEY_TOP_MARGIN+0.5;
    float cap_w = width-2*KEY_LEFT_MARGIN-1;
    float cap_h = height-2*KEY_LEFT_MARGIN-1;
    cr_rounded_box (cr, cap_x, cap_y,
                    cap_w, cap_h,
                    KEY_CORNER_RADIUS);
    cairo_set_source_rgb (cr, ARGS_RGB(color));
    cairo_fill_preserve (cr);

    cairo_set_source_rgba (cr, 0, 0, 0, 0.1);
    cairo_stroke (cr);

    cr_render_key_label (cr, label, cap_x, cap_y, cap_w, cap_h);
}

void cr_non_rectangular_key_path (cairo_t *cr, double x, double y, double margin,
                                  struct keyboard_view_t *kv, struct row_t *row, struct key_t *key)
{
    double r = KEY_CORNER_RADIUS;
    double left = x + margin + 0.5;
    double right = left + key->width*kv->default_key_size - 2*margin - 1;

    // Count the number of rows.
    int num_rows = 0;
    {
        struct row_t *r = row;
        while (r != NULL) {
            num_rows++;
            r = r->next_row;
        }
    }

    int num_return_points = 0;
    dvec2 return_path[num_rows*2];

    // Draw top horizontal segment
    struct round_path_ctx ctx = round_path_start (cr, left, y + margin + 0.5, r);
    round_path_move_to (&ctx, right, y + margin + 0.5);

    // Line sweep from top to bottom that both draws the right vertical path and
    // adds points for the left vertical path into a buffer to be drawn later.
    struct key_t *next_segment = key->next_multirow;
    double next_left, next_right;
    while (!is_multirow_parent (next_segment)) {
        y += row->height*kv->default_key_size;
        if (next_segment->type == KEY_MULTIROW_SEGMENT_SIZED) {
            if (next_segment->align == MULTIROW_ALIGN_RIGHT) {
                next_right = right;
                next_left = right - next_segment->width*kv->default_key_size + 2*margin + 1;

                double margin_offset = margin + 0.5;
                if (left < next_left) {
                    margin_offset *= -1;
                }

                return_path[num_return_points] = DVEC2(left, y + margin_offset);
                return_path[num_return_points+1] = DVEC2(next_left, y + margin_offset);
                num_return_points += 2;

            } else if (next_segment->align == MULTIROW_ALIGN_LEFT) {
                next_right = left + next_segment->width*kv->default_key_size - 2*margin - 1;
                next_left = left;

                double margin_offset = margin + 0.5;
                if (right > next_right) {
                    margin_offset *= -1;
                }

                round_path_move_to (&ctx, right, y + margin_offset);
                round_path_move_to (&ctx, next_right, y + margin_offset);

            }

            right = next_right;
            left = next_left;
        }

        next_segment = next_segment->next_multirow;
        row = row->next_row;
    }

    y += row->height*kv->default_key_size - margin - 0.5;

    // Draw bottom horizontal segment
    round_path_move_to (&ctx, next_right, y);
    round_path_move_to (&ctx, next_left, y);

    // Draw left vertical path from the buffer
    while (num_return_points > 0) {
        num_return_points--;
        double px = return_path[num_return_points].x;
        double py = return_path[num_return_points].y;
        round_path_move_to (&ctx, px, py);
    }

    round_path_close (&ctx);
}

void cr_render_multirow_key (cairo_t *cr, double x, double y, struct keyboard_view_t *kv,
                             struct row_t *row, struct key_t *key, char *label, dvec4 color)
{
    assert (is_multirow_key(key) && is_multirow_parent (key));
    cr_non_rectangular_key_path (cr, x, y, 0, kv, row, key);
    cairo_set_source_rgb (cr,ARGS_RGB(color));
    cairo_fill_preserve (cr);

    cairo_set_source_rgba (cr, 0, 0, 0, 0.05);
    cairo_fill_preserve (cr);

    cairo_set_source_rgb (cr, 0, 0, 0);
    cairo_stroke (cr);

    cr_non_rectangular_key_path (cr, x, y - KEY_LEFT_MARGIN + KEY_TOP_MARGIN,
                                 KEY_LEFT_MARGIN, kv, row, key);
    cairo_set_source_rgb (cr,ARGS_RGB(color));
    cairo_fill_preserve (cr);

    cairo_set_source_rgba (cr, 0, 0, 0, 0.1);
    cairo_stroke (cr);

    // Render the label centered in the multirow parent segment. We may want to
    // allow the user to configure the position?. Maybe allow them to choose the
    // segment, or a more general approach would be to have it positioned
    // relative to the top left corner by some vector, but then how to guarantee
    // it will always be inside the key?.
    cr_render_key_label (cr, label,
                         x + KEY_LEFT_MARGIN, y + KEY_TOP_MARGIN,
                         key->width*kv->default_key_size - 2*KEY_LEFT_MARGIN - 1,
                         row->height*kv->default_key_size - 2*KEY_LEFT_MARGIN - 1);
}

void keyboard_view_get_margins (struct keyboard_view_t *kv, double *left_margin, double *top_margin)
{
    double kbd_width, kbd_height;
    kv_get_size (kv, &kbd_width, &kbd_height);

    if (left_margin != NULL) {
        *left_margin = 0;
        int canvas_w = gtk_widget_get_allocated_width (kv->widget);
        if (kbd_width < canvas_w) {
            *left_margin = floor((canvas_w - kbd_width)/2);
        }
    }

    if (top_margin != NULL) {
        *top_margin = 0;
        int canvas_h = gtk_widget_get_allocated_height (kv->widget);
        if (kbd_height < canvas_h) {
            *top_margin = floor((canvas_h - kbd_height)/2);
        }
    }
}

char *keysym_representations[][2] = {
      {"Alt_L", "Alt"},
      {"Alt_R", "AltGr"},
      {"ISO_Level3_Shift", "AltGr"},
      {"Control_L", "Ctrl"},
      {"Control_R", "Ctrl"},
      {"Shift_L", "Shift"},
      {"Shift_R", "Shift"},
      {"Caps_Lock", "CapsLock"},
      {"Super_L", "⌘ "},
      {"Super_R", "⌘ "},
      {"Prior", "Page\nUp"},
      {"Next", "Page\nDown"},
      {"Num_Lock", "Num\nLock"},
      {"Scroll_Lock", "Scroll\nLock"},
      {"Escape", "Esc"},
      {"Up", "↑"},
      {"Down", "↓"},
      {"Right", "→"},
      {"Left", "←"},
      {"Return", "↵ "}
      };

gboolean keyboard_view_render (GtkWidget *widget, cairo_t *cr, gpointer data)
{
    struct keyboard_view_t *kv = keyboard_view;
    cairo_set_source_rgba (cr, 1, 1, 1, 1);
    cairo_paint(cr);
    cairo_set_line_width (cr, 1);

    mem_pool_t pool = {0};
    double left_margin, top_margin;
    keyboard_view_get_margins (keyboard_view, &left_margin, &top_margin);

    double y_pos = top_margin;
    struct row_t *curr_row = kv->first_row;
    while (curr_row != NULL) {
        struct key_t *curr_key = curr_row->first_key;
        double x_pos = left_margin;
        while (curr_key != NULL) {

            // Compute the label for the key
            char buff[64];
            buff[0] = '\0';
            if (curr_key->type == KEY_DEFAULT || curr_key->type == KEY_PRESSED) {
                switch (keyboard_view->label_mode) {
                    case KV_KEYSYM_LABELS:
                        if (curr_key->kc == KEY_FN) {
                            strcpy (buff, "Fn");
                        }

                        xkb_keysym_t keysym;
                        if (buff[0] == '\0') {
                            int buff_len = 0;
                            keysym = xkb_state_key_get_one_sym(keyboard_view->xkb_state, curr_key->kc + 8);
                            buff_len = xkb_keysym_to_utf8(keysym, buff, sizeof(buff) - 1);
                            buff[buff_len] = '\0';
                        }

                        if (buff[0] == '\0' || // Keysym is non printable
                            buff[0] == ' ' ||
                            buff[0] == '\x1b' || // Escape
                            buff[0] == '\x7f' || // Del
                            buff[0] == '\n' ||
                            buff[0] == '\r' ||
                            buff[0] == '\b' ||
                            buff[0] == '\t' )
                        {
                            xkb_keysym_get_name(keysym, buff, ARRAY_SIZE(buff)-1);
                            if (strcmp (buff, "NoSymbol") == 0) {
                                buff[0] = '\0';
                            }

                            int i;
                            for (i=0; i < ARRAY_SIZE(keysym_representations); i++) {
                                if (strcmp (buff, keysym_representations[i][0]) == 0) {
                                    strcpy (buff, keysym_representations[i][1]);
                                    break;
                                }
                            }
                        }
                        break;

                    case KV_KEYCODE_LABELS:
                        snprintf (buff, ARRAY_SIZE(buff), "%i", curr_key->kc);
                        break;

                    default: break; // Leave an empty label
                }
            }

            x_pos += (curr_key->internal_glue + curr_key->user_glue)*kv->default_key_size;

            dvec4 key_color;
            if (curr_key->type != KEY_MULTIROW_SEGMENT) {
                if (keyboard_view->selected_key != NULL && curr_key == keyboard_view->selected_key) {
                    buff[0] = '\0';
                    key_color = RGB_HEX(0xe34442);

                } else if (curr_key->type == KEY_PRESSED ||
                           (curr_key->type != KEY_UNASSIGNED && curr_key->kc == keyboard_view->clicked_kc)) {
                    key_color = RGB_HEX(0x90de4d);

                } else {
                    key_color = RGB(1,1,1);
                }
            }

            float key_width, key_height;
            if (compute_key_size (kv, curr_key, curr_row, &key_width, &key_height)) {
                if (curr_key->type != KEY_MULTIROW_SEGMENT && curr_key->type != KEY_MULTIROW_SEGMENT_SIZED) {
                    cr_render_key (cr, x_pos, y_pos, key_width, key_height, buff, key_color);
                }

            } else {
                if (is_multirow_parent(curr_key)) {
                    cr_render_multirow_key (cr, x_pos, y_pos, kv, curr_row, curr_key, buff, key_color);
                }
            }

            x_pos += key_width;
            curr_key = curr_key->next_key;
        }

        y_pos += curr_row->height*kv->default_key_size;
        curr_row = curr_row->next_row;
    }

    cairo_rectangle (cr, kv->debug_rect.x, kv->debug_rect.y, kv->debug_rect.width, kv->debug_rect.height);
    cairo_set_source_rgba (cr, 0, 0, 1, 0.5);
    cairo_fill (cr);

    mem_pool_destroy (&pool);
    return FALSE;
}

struct key_t* keyboard_view_get_key (struct keyboard_view_t *kv, double x, double y,
                                     GdkRectangle *rect, bool *is_rectangular,
                                     struct key_t **clicked_sgmt, struct key_t ***parent_ptr)
{
    double kbd_x, kbd_y;
    keyboard_view_get_margins (kv, &kbd_x, &kbd_y);
    if (x < kbd_x || y < kbd_y) {
        return NULL;
    }

    struct row_t *curr_row = kv->first_row;
    while (curr_row != NULL) {
        float next_y = kbd_y + curr_row->height*kv->default_key_size;
        if (next_y > y) {
            // In this case x_kbd now contains the x coordinate of the
            // rectangle (top left corner).
            break;
        }
        // We only commit to the updated kbd_y after we check we don't want to
        // break.
        kbd_y = next_y;

        curr_row = curr_row->next_row;
    }

    struct key_t *curr_key = NULL, *prev_key = NULL;
    if (curr_row != NULL) {
        curr_key = curr_row->first_key;
        while (curr_key != NULL) {
            // First update x_kbd with the glues, if the point is here we return NULL.
            kbd_x += (curr_key->internal_glue + curr_key->user_glue)*kv->default_key_size;
            if (kbd_x > x) {
                curr_key = NULL;
                break;
            }

            float next_x;
            if (curr_key->type == KEY_MULTIROW_SEGMENT) {
                next_x = kbd_x + get_multirow_segment_width (curr_key)*kv->default_key_size;
            } else {
                next_x = kbd_x + curr_key->width*kv->default_key_size;
            }

            if (next_x > x) {
                // In this case x_kbd now contains the x coordinate of the
                // rectangle (top left corner).
                break;
            }
            // We only commit to the updated kbd_x after we check we don't want
            // to break.
            kbd_x = next_x;

            prev_key = curr_key;
            curr_key = curr_key->next_key;
        }
    }

    if (curr_key != NULL) {
        if (clicked_sgmt != NULL) {
            *clicked_sgmt = curr_key;
        }

        if (rect != NULL) {
            float key_width, key_height;
            float multirow_y_offset;
            // For non rectangular multirow keys this returns the rectangle
            // of the segment where x and y are.
            bool l_is_rectangular = compute_key_size_full (kv, curr_key, curr_row, &key_width, &key_height, &multirow_y_offset);
            if (is_rectangular != NULL) {
                *is_rectangular = l_is_rectangular;
            }

            rect->width = (int)key_width;
            rect->height = (int)key_height;
            rect->y = kbd_y - multirow_y_offset;
            rect->x = kbd_x;

            kv->debug_rect = *rect;
        }

        // In a multirow key data is stored in the multirow parent. Make the
        // return value the multirow parent of the key.
        if (is_multirow_key(curr_key) && !is_multirow_parent(curr_key)) {
            curr_key = kv_get_multirow_parent (curr_key);
        }

        if (parent_ptr != NULL) {
            *parent_ptr = NULL;
            if (curr_key != NULL) {
                if (prev_key == NULL) {
                    *parent_ptr = &curr_row->first_key;
                } else {
                    *parent_ptr = &prev_key->next_key;
                }
            }
        }
    }

    return curr_key;
}

void kv_update (struct keyboard_view_t *kv, enum keyboard_view_commands_t cmd, GdkEvent *e);

void start_edit_handler (GtkButton *button, gpointer user_data)
{
    kv_update (keyboard_view, KV_CMD_SET_MODE_EDIT, NULL);
}
void stop_edit_handler (GtkButton *button, gpointer user_data)
{
    kv_update (keyboard_view, KV_CMD_SET_MODE_PREVIEW, NULL);
}

void split_key_handler (GtkButton *button, gpointer user_data)
{
    keyboard_view->active_tool = KV_TOOL_SPLIT_KEY;
}

void delete_key_handler (GtkButton *button, gpointer user_data)
{
    keyboard_view->active_tool = KV_TOOL_DELETE_KEY;
}

void resize_key_handler (GtkButton *button, gpointer user_data)
{
    keyboard_view->active_tool = KV_TOOL_RESIZE_KEY;
}

void keycode_keypress_handler (GtkButton *button, gpointer user_data)
{
    keyboard_view->active_tool = KV_TOOL_KEYCODE_KEYPRESS;
}

void kv_push_manual_tooltip (struct keyboard_view_t *kv, GdkRectangle *rect, const char *text)
{
    struct manual_tooltip_t *new_tooltip =
        (struct manual_tooltip_t*)mem_pool_push_size (&kv->tooltips_pool, sizeof (struct manual_tooltip_t));
    *new_tooltip = ZERO_INIT (struct manual_tooltip_t);
    new_tooltip->rect = *rect;
    new_tooltip->text = pom_strndup (&kv->tooltips_pool, text, strlen(text));
    if (kv->first_tooltip == NULL) {
        kv->first_tooltip = new_tooltip;
    } else {
        kv->last_tooltip->next = new_tooltip;
    }
    kv->last_tooltip = new_tooltip;
}

void kv_clear_manual_tooltips (struct keyboard_view_t *kv)
{
    kv->first_tooltip = NULL;
    kv->last_tooltip = NULL;
    mem_pool_destroy (&kv->tooltips_pool);
    kv->tooltips_pool = ZERO_INIT (mem_pool_t);
}

// FIXME: @broken_tooltips_in_overlay
void button_allocated (GtkWidget *widget, GdkRectangle *rect, gpointer user_data)
{
    gchar *text = gtk_widget_get_tooltip_text (widget);
    kv_push_manual_tooltip (keyboard_view, rect, text);
    g_free (text);
}

GtkWidget* toolbar_button_new (const char *icon_name, char *tooltip, GCallback callback, gpointer user_data)
{
    if (icon_name == NULL) {
        icon_name = "bug-symbolic";
    }

    GtkWidget *new_button = gtk_button_new_from_icon_name (icon_name, GTK_ICON_SIZE_SMALL_TOOLBAR);
    add_css_class (new_button, "flat");
    g_signal_connect (G_OBJECT(new_button), "clicked", callback, user_data);

    gtk_widget_set_tooltip_text (new_button, tooltip);
    // FIXME: @broken_tooltips_in_overlay
    g_signal_connect (G_OBJECT(new_button), "size-allocate", G_CALLBACK(button_allocated), tooltip);

    gtk_widget_show (new_button);

    return new_button;
}

void kv_set_simple_toolbar (GtkWidget **toolbar)
{
    assert (toolbar != NULL);

    if (*toolbar == NULL) {
        *toolbar = gtk_grid_new ();
        gtk_widget_show (*toolbar);

    } else {
        gtk_container_foreach (GTK_CONTAINER(*toolbar),
                               destroy_children_callback,
                               NULL);
    }

    GtkWidget *edit_button = toolbar_button_new ("edit-symbolic",
                                                 "Edit the view to match your keyboard",
                                                 G_CALLBACK (start_edit_handler), NULL);
    gtk_grid_attach (GTK_GRID(*toolbar), edit_button, 0, 0, 1, 1);

}

void kv_set_full_toolbar (GtkWidget **toolbar)
{
    assert (toolbar != NULL);

    if (*toolbar == NULL) {
        *toolbar = gtk_grid_new ();
        gtk_widget_show (*toolbar);

    } else {
        gtk_container_foreach (GTK_CONTAINER(*toolbar),
                               destroy_children_callback,
                               NULL);
    }

    GtkWidget *stop_edit_button = toolbar_button_new ("close-symbolic",
                                                      "Stop edit mode",
                                                      G_CALLBACK (stop_edit_handler), NULL);
    gtk_grid_attach (GTK_GRID(*toolbar), stop_edit_button, 0, 0, 1, 1);

    GtkWidget *keycode_keypress = toolbar_button_new (NULL,
                                                      "Assign a keycode by pressing a key",
                                                      G_CALLBACK (keycode_keypress_handler), NULL);
    gtk_grid_attach (GTK_GRID(*toolbar), keycode_keypress, 1, 0, 1, 1);

    GtkWidget *split_key_button = toolbar_button_new ("edit-cut-symbolic",
                                                      "Split key",
                                                      G_CALLBACK (split_key_handler), NULL);
    gtk_grid_attach (GTK_GRID(*toolbar), split_key_button, 2, 0, 1, 1);

    GtkWidget *delete_key_button = toolbar_button_new ("edit-delete-symbolic",
                                                      "Delete key",
                                                      G_CALLBACK (delete_key_handler), NULL);
    gtk_grid_attach (GTK_GRID(*toolbar), delete_key_button, 3, 0, 1, 1);

    GtkWidget *resize_key_button = toolbar_button_new (NULL,
                                                      "Resize key",
                                                      G_CALLBACK (resize_key_handler), NULL);
    gtk_grid_attach (GTK_GRID(*toolbar), resize_key_button, 4, 0, 1, 1);
}

// Round i downwards to the nearest multiple of 1/2^n. If i is negative treat it
// as positive and then put back the sign.
float bin_floor (float i, int n)
{
    bool is_negative = false;
    if (i < 0) {
        is_negative = true;
        i *= -1;
    }

    int k;
    float dec = 1;
    float res = (int)i;
    for (k=0; k < n; k++) {
        dec /= 2;
        if (res + dec <= i) {
            res += dec;
        }
    }

    if (is_negative) {
        res *= -1;
    }
    return res;
}

// Round i upwards to the nearest multiple of 1/2^n. If i is negative treat it
// as positive and then put back the sign.
float bin_ceil (float i, int n)
{
    bool is_negative = false;
    if (i < 0) {
        is_negative = true;
        i *= -1;
    }

    int k;
    float dec = 1;
    float res = (int)i;
    for (k=0; k < n; k++) {
        dec /= 2;
        if (res + dec < i) {
            res += dec;
        }
    }

    if (res < i) {
        res += dec;
    }

    if (is_negative) {
        res *= -1;
    }
    return res;
}

static inline
float kv_get_min_key_width (struct keyboard_view_t *kv)
{
    return bin_ceil(2*(KEY_LEFT_MARGIN + KEY_CORNER_RADIUS)/kv->default_key_size, 3);
}

void kv_set_key_split (struct keyboard_view_t *kv, double ptr_x)
{
    float min_width = kv_get_min_key_width (kv)*kv->default_key_size;
    float left_width, right_width;
    left_width = CLAMP(ptr_x - kv->split_key_rect.x, min_width, kv->split_key_rect.width-min_width);
    right_width = CLAMP(kv->split_key_rect.width - left_width, min_width, kv->split_key_rect.width-min_width);
    left_width = bin_floor(floorf(left_width)/kv->default_key_size, 3);
    right_width = bin_ceil(ceilf(right_width)/kv->default_key_size, 3);

    if (kv->split_key_rect.x + kv->split_key_rect.width/2 < ptr_x) {
        *kv->splitted_key_ptr = kv->splitted_key;
        kv->splitted_key->next_key = kv->new_key;
        kv->new_key->next_key = kv->after_key;

        kv->splitted_key->width = left_width;
        kv->new_key->width = right_width;

    } else {
        *kv->splitted_key_ptr = kv->new_key;
        kv->new_key->next_key = kv->splitted_key;
        kv->splitted_key->next_key = kv->after_key;

        kv->splitted_key->width = right_width;
        kv->new_key->width = left_width;
    }
}

void kv_update (struct keyboard_view_t *kv, enum keyboard_view_commands_t cmd, GdkEvent *e)
{
    // To avoid segfaults when e==NULL without having to check if e==NULL every
    // time, create a dmmy event that has a type that will fail all checks.
    GdkEvent null_event = {0};
    if (e == NULL) {
        null_event.type = GDK_NOTHING;
        e = &null_event;
    }

    struct key_t *button_event_key = NULL, *button_event_key_clicked_sgmt = NULL, **button_event_key_ptr = NULL;
    bool button_event_key_is_rectangular = false;
    GdkRectangle button_event_key_rect = {0};
    if (e->type == GDK_BUTTON_PRESS || e->type == GDK_BUTTON_RELEASE) {
        GdkEventButton *btn_e = (GdkEventButton*)e;
        button_event_key = keyboard_view_get_key (kv, btn_e->x, btn_e->y,
                                                  &button_event_key_rect,
                                                  &button_event_key_is_rectangular,
                                                  &button_event_key_clicked_sgmt,
                                                  &button_event_key_ptr);

    } else if (e->type == GDK_MOTION_NOTIFY) {
        GdkEventMotion *btn_e = (GdkEventMotion*)e;
        button_event_key = keyboard_view_get_key (kv, btn_e->x, btn_e->y,
                                                  &button_event_key_rect,
                                                  &button_event_key_is_rectangular,
                                                  &button_event_key_clicked_sgmt,
                                                  &button_event_key_ptr);
    }

    uint16_t key_event_kc = 0;
    struct key_t *key_event_key = NULL;
    if (e->type == GDK_KEY_PRESS || e->type == GDK_KEY_RELEASE) {
        key_event_kc = ((GdkEventKey*)e)->hardware_keycode;
        key_event_key = kv->keys_by_kc[key_event_kc-8];
    }

    if (e->type == GDK_KEY_PRESS) {
        if (key_event_key != NULL) {
            key_event_key->type = KEY_PRESSED;
        }
        xkb_state_update_key(kv->xkb_state, key_event_kc, XKB_KEY_DOWN);
    }

    if (e->type == GDK_KEY_RELEASE) {
        if (key_event_key != NULL) {
            key_event_key->type = KEY_DEFAULT;
        }
        xkb_state_update_key(kv->xkb_state, key_event_kc, XKB_KEY_UP);
    }

    switch (kv->state) {
        case KV_PREVIEW:
            if (cmd == KV_CMD_SET_MODE_EDIT) {
                // FIXME: @broken_tooltips_in_overlay
                kv_clear_manual_tooltips (kv);
                kv_set_full_toolbar (&kv->toolbar);
                kv->label_mode = KV_KEYCODE_LABELS;
                kv->state = KV_EDIT;

            } else if (e->type == GDK_BUTTON_PRESS && button_event_key != NULL) {
                xkb_state_update_key(kv->xkb_state, button_event_key->kc+8, XKB_KEY_DOWN);
                kv->clicked_kc = button_event_key->kc;

            } else if (e->type == GDK_BUTTON_RELEASE && button_event_key != NULL) {
                xkb_state_update_key(kv->xkb_state, kv->clicked_kc+8, XKB_KEY_UP);
                kv->clicked_kc = 0;
            }
            break;

        case KV_EDIT:
            if (cmd == KV_CMD_SET_MODE_PREVIEW) {
                // FIXME: @broken_tooltips_in_overlay
                kv_clear_manual_tooltips (kv);
                kv_set_simple_toolbar (&kv->toolbar);
                kv->label_mode = KV_KEYSYM_LABELS;
                kv->state = KV_PREVIEW;

            } else if (kv->active_tool == KV_TOOL_KEYCODE_KEYPRESS &&
                       e->type == GDK_BUTTON_RELEASE && button_event_key != NULL) {
                // NOTE: We handle this on release because we are taking a grab of
                // all input. Doing so on a key press breaks GTK's grab created
                // before sending the event, which may cause trouble.
                //
                // For the other tools we default to make them release based
                // just for consistency.
                // @select_is_release_bassed
                kv->selected_key = button_event_key;
                grab_input (NULL, NULL);
                kv->state = KV_EDIT_KEYCODE_KEYPRESS;

            } else if (kv->active_tool == KV_TOOL_SPLIT_KEY &&
                       e->type == GDK_BUTTON_RELEASE && button_event_key != NULL) {
                kv->split_key_rect = button_event_key_rect;
                kv->splitted_key = button_event_key;
                kv->splitted_key_ptr = button_event_key_ptr;

                kv->new_key = kv_allocate_key (kv);
                kv->new_key->type = KEY_UNASSIGNED;
                kv->after_key = button_event_key->next_key;

                GdkEventButton *event = (GdkEventButton*)e;
                kv_set_key_split (kv, event->x);

                kv->state = KV_EDIT_KEY_SPLIT;

            } else if (kv->active_tool == KV_TOOL_DELETE_KEY &&
                       e->type == GDK_BUTTON_RELEASE && button_event_key != NULL) {
                kv_remove_key (kv, button_event_key_ptr);
                kv_compute_glue (kv);

            } else if (kv->active_tool == KV_TOOL_RESIZE_KEY &&
                       e->type == GDK_BUTTON_RELEASE && button_event_key != NULL) {
                if (button_event_key_is_rectangular) {
                    GdkEventButton *event = (GdkEventButton*)e;
                    if (event->x < button_event_key_rect.x + button_event_key_rect.width/2) {
                        kv->resize_right = false;
                    } else {
                        kv->resize_right = true;
                    }

                    if (is_multirow_key(button_event_key)) {
                        kv->resized_key = kv_get_multirow_parent(button_event_key);
                    } else {
                        kv->resized_key = button_event_key;
                    }

                    kv->original_user_glue = kv->resized_key->user_glue;
                    kv->x_clicked_pos = event->x;
                    kv->original_w = button_event_key->width;
                    kv->state = KV_EDIT_KEY_RESIZE;

                } else {
                    GdkEventButton *event = (GdkEventButton*)e;
                    struct key_t *resized_key, *edge_start_sgmt, *edge_end_sgmt;
                    if (event->x < button_event_key_rect.x + button_event_key_rect.width/2) {
                        kv->resize_right = false;
                    } else {
                        kv->resize_right = true;

                        edge_start_sgmt = NULL;
                        resized_key = button_event_key; // multirow parent
                        if (button_event_key_clicked_sgmt != button_event_key) {
                            int num_same_edge = 0;
                            struct key_t *curr_key = button_event_key;
                            do {
                                curr_key = curr_key->next_multirow;

                                if (curr_key->type == KEY_MULTIROW_SEGMENT_SIZED) {
                                    if (curr_key->align == MULTIROW_ALIGN_LEFT) {
                                        edge_start_sgmt = resized_key;
                                        num_same_edge = 0;
                                        resized_key = curr_key;
                                    }

                                    if (curr_key->align == MULTIROW_ALIGN_RIGHT) {
                                        if (num_same_edge == 0) {
                                            resized_key = curr_key;
                                        }
                                        num_same_edge++;
                                    }
                                }

                                if (curr_key->type == KEY_MULTIROW_SEGMENT) {
                                    num_same_edge++;
                                }

                            } while (curr_key != button_event_key_clicked_sgmt);
                        }


                    }

                    edge_end_sgmt = NULL;
                    {
                        struct key_t *curr_key = button_event_key_clicked_sgmt->next_multirow;
                        while (!is_multirow_parent (curr_key)) {
                            if (curr_key->type == KEY_MULTIROW_SEGMENT_SIZED &&
                                curr_key->align == MULTIROW_ALIGN_LEFT) {
                                edge_end_sgmt = curr_key;
                                break;
                            }
                            curr_key = curr_key->next_multirow;
                        }
                    }

                    float min_w = resized_key->width;
                    {
                        struct key_t *end = edge_end_sgmt;
                        if (end == NULL) {
                            end = button_event_key;
                        }

                        struct key_t *curr_key = resized_key->next_multirow;
                        do {
                            min_w = MIN (min_w, curr_key->width);
                            curr_key = curr_key->next_multirow;
                        } while (curr_key != end);
                    }
                    kv->min_width = resized_key->width - min_w + kv_get_min_key_width(kv);

#if 0
                    // Debug code
                    {
                        struct key_t *curr_key = button_event_key;
                        do {
                            printf ("%p ", curr_key);
                            curr_key = curr_key->next_multirow;
                        } while (curr_key != button_event_key);
                        printf ("\n");
                        printf ("r: %p ", resized_key);
                        printf ("s: %p ", edge_start_sgmt);
                        printf ("e: %p\n", edge_end_sgmt);
                    }
#endif

                    kv->resized_key = resized_key;
                    kv->edge_start_sgmt = edge_start_sgmt;
                    kv->edge_end_sgmt = edge_end_sgmt;
                    kv->original_w = resized_key->width;
                    kv->x_clicked_pos = event->x;
                    kv->x_anchor = event->x;
                    kv->state = KV_EDIT_KEY_RESIZE_NON_RECTANGULAR;
                }
            }
            break;

        case KV_EDIT_KEYCODE_KEYPRESS:
            if (e->type == GDK_KEY_PRESS) {
                // If the keycode was already assigned, unassign it from that
                // key.
                if (key_event_key != NULL) {
                    // NOTE: Because key_event_key won't be accessible again
                    // through keys_by_kc (the selected key wil take it's place
                    // there), then it would remain pressed unless we do this.
                    key_event_key->type = KEY_UNASSIGNED;
                }

                // If selected_key has a keycode assigned, remove it's pointer
                // from keys_by_kc because it will change position.
                if (kv->selected_key->type == KEY_PRESSED || kv->selected_key->type == KEY_DEFAULT) {
                    kv->keys_by_kc[kv->selected_key->kc] = NULL;
                }

                // Update selected_key info
                kv->selected_key->kc = key_event_kc-8;
                kv->selected_key->type = KEY_DEFAULT;

                // Put a pointer to selected_key in the correct position in
                // keys_by_kc.
                kv->keys_by_kc[key_event_kc-8] = kv->selected_key;

                kv->selected_key = NULL;
                ungrab_input (NULL, NULL);
                kv->state = KV_EDIT;

            } else if (e->type == GDK_BUTTON_RELEASE) { // @select_is_release_bassed
                if (button_event_key == NULL || button_event_key == kv->selected_key) {
                    ungrab_input (NULL, NULL);
                    kv->selected_key = NULL;
                    kv->state = KV_EDIT;
                } else {
                    // Edit the newly clicked key.
                    kv->selected_key = button_event_key;
                }
            }
            break;

        case KV_EDIT_KEY_SPLIT:
            if (e->type == GDK_MOTION_NOTIFY) {
                GdkEventMotion *event = (GdkEventMotion*)e;
                kv_set_key_split (kv, event->x);

            } else if (e->type == GDK_BUTTON_RELEASE) {
                kv->state = KV_EDIT;

            } else if (e->type == GDK_KEY_PRESS) {
                if (key_event_kc - 8 == KEY_ESC) {
                    // Add allocated key into the spare key list
                    kv->new_key->next_key = kv->spare_keys;
                    kv->spare_keys = kv->new_key;

                    // Get the row list back to normal
                    *kv->splitted_key_ptr = kv->splitted_key;
                    kv->splitted_key->next_key = kv->after_key;
                    kv->splitted_key->width = kv->split_key_rect.width/kv->default_key_size;;

                    kv->state = KV_EDIT;
                }

            }
            break;

        case KV_EDIT_KEY_RESIZE:
            if (e->type == GDK_MOTION_NOTIFY) {
                GdkEventMotion *event = (GdkEventMotion*)e;
                float min_width = kv_get_min_key_width (kv);

                float delta_w;
                if (kv->resize_right) {
                    delta_w = event->x - kv->x_clicked_pos;
                } else {
                    delta_w = kv->x_clicked_pos - event->x;
                    float max_glue =
                        (kv->original_user_glue + kv->original_w - min_width)*kv->default_key_size;
                    float g = CLAMP(kv->original_user_glue*kv->default_key_size - delta_w, 0, max_glue);
                    kv->resized_key->user_glue = bin_ceil(g/kv->default_key_size, 3);
                }

                float new_width = (kv->original_w*kv->default_key_size + delta_w)/kv->default_key_size;
                kv->resized_key->width = MAX(bin_floor(new_width, 3), min_width);
                kv_compute_glue (kv);

            } else if (e->type == GDK_BUTTON_RELEASE) {
                kv->state = KV_EDIT;

            } else if (e->type == GDK_KEY_PRESS) {
                if (key_event_kc - 8 == KEY_ESC) {
                    kv->resized_key->width = kv->original_w;
                    if (!kv->resize_right) {
                        kv->resized_key->user_glue = kv->original_user_glue;
                    }

                    kv_compute_glue (kv);
                    kv->state = KV_EDIT;
                }
            }

            break;

        case KV_EDIT_KEY_RESIZE_NON_RECTANGULAR:
            if (e->type == GDK_MOTION_NOTIFY) {
                GdkEventMotion *event = (GdkEventMotion*)e;

                float delta_w = bin_floor((event->x - kv->x_anchor)/kv->default_key_size, 3);
                float new_width = kv->resized_key->width + delta_w;

                if (delta_w != 0 && new_width >= kv->min_width) {
                    kv->resized_key->width = new_width;
                    if (kv->edge_start_sgmt != NULL) {
                        if (new_width == kv->edge_start_sgmt->width) {
                            kv->resized_key->type = KEY_MULTIROW_SEGMENT;
                        } else {
                            kv->resized_key->type = KEY_MULTIROW_SEGMENT_SIZED;
                        }
                    }

                    float last_width = new_width;
                    struct key_t *curr_key = kv->resized_key->next_multirow;
                    while (!is_multirow_parent(curr_key) && curr_key != kv->edge_end_sgmt) {
                        if (curr_key->type != KEY_MULTIROW_SEGMENT) {
                            curr_key->width += delta_w;
                            last_width = curr_key->width;
                        }
                        curr_key = curr_key->next_multirow;
                    }

                    if (kv->edge_end_sgmt != NULL) {
                        if (last_width == kv->edge_end_sgmt->width) {
                            kv->edge_end_sgmt->type = KEY_MULTIROW_SEGMENT;
                        } else {
                            kv->edge_end_sgmt->type = KEY_MULTIROW_SEGMENT_SIZED;
                        }
                    }

                    kv->x_anchor += delta_w*kv->default_key_size;
                }

                kv_compute_glue (kv);

            } else if (e->type == GDK_BUTTON_RELEASE) {
                kv->state = KV_EDIT;

            } else if (e->type == GDK_KEY_PRESS) {
                if (key_event_kc - 8 == KEY_ESC) {
                    float delta_w = kv->original_w - kv->resized_key->width;
                    kv->resized_key->width = kv->original_w;

                    struct key_t *curr_key = kv->resized_key->next_multirow;
                    while (!is_multirow_parent(curr_key) && curr_key != kv->edge_end_sgmt) {
                        if (curr_key->type != KEY_MULTIROW_SEGMENT) {
                            curr_key->width += delta_w;
                        }
                        curr_key = curr_key->next_multirow;
                    }

                    kv_compute_glue (kv);
                    kv->state = KV_EDIT;
                }
            }

            break;
    }

    gtk_widget_queue_draw (kv->widget);
}

gboolean key_press_handler (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    kv_update (keyboard_view, KV_CMD_NONE, event);
    return TRUE;
}

gboolean key_release_handler (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    kv_update (keyboard_view, KV_CMD_NONE, event);
    return TRUE;
}

gboolean kv_motion_notify (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    kv_update (keyboard_view, KV_CMD_NONE, event);
    return TRUE;
}

gboolean kv_button_press (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    GdkEventButton *button_event = (GdkEventButton*)event;
    if (button_event->type == GDK_2BUTTON_PRESS || button_event->type == GDK_3BUTTON_PRESS) {
        // Ignore double and triple clicks.
        return FALSE;
    }

    kv_update (keyboard_view, KV_CMD_NONE, event);
    return TRUE;
}

gboolean kv_button_release (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    kv_update (keyboard_view, KV_CMD_NONE, event);
    return TRUE;
}

gboolean kv_tooltip_handler (GtkWidget *widget, gint x, gint y,
                                        gboolean keyboard_mode, GtkTooltip *tooltip,
                                        gpointer user_data)
{
    if (keyboard_mode) {
        return FALSE;
    }

    gboolean show_tooltip = FALSE;
    GdkRectangle rect = {0};
    struct key_t *key = keyboard_view_get_key (keyboard_view, x, y, &rect, NULL, NULL, NULL);
    if (key != NULL) {
        // For non rectangular multirow keys keyboard_view_get_key() returns the
        // rectangle of the segment that was hovered. This has the (undesired?)
        // effect of making the tooltip jump while moving the mouse over the
        // same key but across different segments. Because the tooltip area can
        // only be a rectangle, the only other option would be to set it to the
        // bounding box, which would cause the tooltip to not jump even when
        // changing across different keys (although the text inside would change
        // appropiately).
        if (keyboard_view->label_mode == KV_KEYCODE_LABELS) {
            gtk_tooltip_set_text (tooltip, keycode_names[key->kc]);

        } else { // KV_KEYSYM_LABELS
            char buff[64];
            xkb_keysym_t keysym = xkb_state_key_get_one_sym(keyboard_view->xkb_state, key->kc + 8);
            xkb_keysym_get_name(keysym, buff, ARRAY_SIZE(buff)-1);
            gtk_tooltip_set_text (tooltip, buff);
        }

        gtk_tooltip_set_tip_area (tooltip, &rect);
        show_tooltip = TRUE;

    } else {
        struct manual_tooltip_t *curr_tooltip = keyboard_view->first_tooltip;
        while (curr_tooltip != NULL) {
            if (is_in_rect (x, y, curr_tooltip->rect)) {
                gtk_tooltip_set_text (tooltip, curr_tooltip->text);
                gtk_tooltip_set_tip_area (tooltip, &curr_tooltip->rect);
                show_tooltip = TRUE;
                break;
            }
            curr_tooltip = curr_tooltip->next;
        }
    }

    return show_tooltip;
}

void keyboard_view_set_keymap (struct keyboard_view_t *kv, const char *keymap_name)
{
    if (kv->xkb_keymap != NULL) {
        xkb_keymap_unref(kv->xkb_keymap);
    }

    if (kv->xkb_state != NULL) {
        xkb_state_unref(kv->xkb_state);
    }

    mem_pool_t pool = {0};
    char *keymap_str = reconstruct_installed_custom_layout (&pool, keymap_name);

    struct xkb_context *xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!xkb_ctx) {
        printf ("Error creating xkb_context.\n");
    }

    kv->xkb_keymap = xkb_keymap_new_from_string (xkb_ctx, keymap_str,
                                                 XKB_KEYMAP_FORMAT_TEXT_V1,
                                                 XKB_KEYMAP_COMPILE_NO_FLAGS);
    xkb_context_unref (xkb_ctx);

    if (!kv->xkb_keymap) {
        printf ("Error creating xkb_keymap.\n");
    }

    kv->xkb_state = xkb_state_new(kv->xkb_keymap);
    if (!kv->xkb_state) {
        printf ("Error creating xkb_state.\n");
    }

    gtk_widget_queue_draw (kv->widget);

    mem_pool_destroy (&pool);
}

// I'm experimenting with some ownership patterns here. The idea is that when
// creating a new keyboard_view_t the caller can pass a mem_pool_t, in which
// case everything will be freed when that pool is freed. If instead NULL is
// passed, then we bootstrap a mem_pool_t and store everything there, in this
// case keyboard_view_destroy() must be called when it is no longer needed.
struct keyboard_view_t* keyboard_view_new (mem_pool_t *pool, GtkWidget *window)
{
    if (pool == NULL) {
        mem_pool_t bootstrap_pool = {0};
        pool = mem_pool_push_size (&bootstrap_pool, sizeof(mem_pool_t));
        *pool = bootstrap_pool;
    }

    struct keyboard_view_t *kv = mem_pool_push_size (pool, sizeof(struct keyboard_view_t));
    *kv = ZERO_INIT(struct keyboard_view_t);

    g_signal_connect (G_OBJECT(window), "key-press-event", G_CALLBACK (key_press_handler), NULL);
    g_signal_connect (G_OBJECT(window), "key-release-event", G_CALLBACK (key_release_handler), NULL);

    // Build the GtkWidget as an Overlay of a GtkDrawingArea and a GtkGrid
    // containing the toolbar.
    //
    // NOTE: Using a horizontal GtkBox as container for the toolbar didn't work
    // because it took the full height of the drawing area. Which puts buttons
    // in the center of the keyboard view vertically.
    {
        GtkWidget *kv_widget = gtk_overlay_new ();
        gtk_widget_add_events (kv_widget,
                               GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                               GDK_POINTER_MOTION_MASK);
        gtk_widget_set_vexpand (kv_widget, TRUE);
        gtk_widget_set_hexpand (kv_widget, TRUE);
        g_signal_connect (G_OBJECT (kv_widget), "button-press-event", G_CALLBACK (kv_button_press), NULL);
        g_signal_connect (G_OBJECT (kv_widget), "button-release-event", G_CALLBACK (kv_button_release), NULL);
        g_signal_connect (G_OBJECT (kv_widget), "motion-notify-event", G_CALLBACK (kv_motion_notify), NULL);
        g_object_set_property_bool (G_OBJECT (kv_widget), "has-tooltip", true);

        // FIXME: Tooltips for children of a GtkOverlay appear to be broken (or
        // I was unable set them up properly). Only one query-tooltip signal is
        // sent to the overlay. Even if a children of the overlay has a tooltip,
        // it never receives the query-tooltip signal. It's as if tooltip
        // "events" don't trickle down to children.
        //
        // To work around this, we manually add tooltips for buttons in the
        // toolbar. Then the correct tooltip is chosen in the handler for the
        // query-tooltip signal, for the overlay.
        // @broken_tooltips_in_overlay
        g_signal_connect (G_OBJECT (kv_widget), "query-tooltip", G_CALLBACK (kv_tooltip_handler), NULL);
        gtk_widget_show (kv_widget);

        GtkWidget *draw_area = gtk_drawing_area_new ();
        gtk_widget_set_vexpand (draw_area, TRUE);
        gtk_widget_set_hexpand (draw_area, TRUE);
        g_signal_connect (G_OBJECT (draw_area), "draw", G_CALLBACK (keyboard_view_render), NULL);
        gtk_widget_show (draw_area);
        gtk_overlay_add_overlay (GTK_OVERLAY(kv_widget), draw_area);
        kv->widget = kv_widget;
    }

    kv_set_simple_toolbar (&kv->toolbar);
    gtk_overlay_add_overlay (GTK_OVERLAY(kv->widget), kv->toolbar);

    multirow_test_geometry (kv);
    //keyboard_view_build_default_geometry (kv);
    kv->pool = pool;
    kv->state = KV_PREVIEW;
    kv_update (kv, KV_CMD_SET_MODE_EDIT, NULL);

    return kv;
}

void keyboard_view_destroy (struct keyboard_view_t *kv)
{
    mem_pool_destroy (kv->pool);
}

