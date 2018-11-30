/*
 * Copiright (C) 2018 Santiago León O.
 */

// This is the main data structure for the keyboard, is is used to store the
// geometry and the state of the keyboard. It also implements several tools to
// be ablo to edit the geometry and make the keyboard look like the user's
// physical keyboard.
//
// Because keyboards are so different, I think that instead of providing a big
// list of possible keyboard geometries, it's better to design a small toolset
// that allows the user to move, reshape keys and assign keycodes. Several
// tradeoffs were made between flexibility and ease of use, for instance, curved
// keyboards can't be represented by this data structure as doing so would
// transform the toolset pretty much into a general vector editor, and that is
// way out of the scope of the application in terms of complexity both at in
// implementation and ease of use.
//
// The main data structure is a liked list of rows (row_t), each of which
// contains a linked list of key segments (sgmt_t). The key segments of a key
// that extends across multiple rows are called "multirow keys" and are grouped
// with the pointers next_multirow forming a cyclic linked list. In multirow
// keys the top segment is called the "key parent" and is the segment that
// represents the full key, here is where the keycode will be stored.
//
// Multirow keys are rigid, so the position of a segment in a row restricts the
// position of the other segments, this means there may be some blank space
// before some of the segments of a multirow key. This distance is called
// internal glue, it's stored for each segment in the keyboard, and computed by
// kv_compute_glue(). Note that a multirow key will always have at least one
// segment (and maybe more) with internal glue equal to 0, we call such segments
// supporting segments.
//
//                              S_1's Internal
//                                    glue
//                                   /---/+---+
//                                        |S_1|
//                                   +---+|   |
//                                   |   ||S_2|  S's Supporting segment
//                                   +---++---+
//                                          S
//
// Sometimes the user may want to force there being some glue, for example to
// separate a keypad from the rest of the keyboard. To represent this, there is
// a second value called user glue. This one is stored per key, so in a multirow
// key it is located in the multirow parent. The user glue is added on top of
// the computed internal glue, which means it is defined to be the glue of the
// supporting segment.
//
// The toolset provided to edit the keyboard geometries was chosen to be small and
// simple, but with enough tools to be able to describe most geometries a user may
// find. Some tools work in stages, for example resize consists of a start, a
// drag and the end, on these kinds of tools an important property we ensure is
// reversibility. A tool is reversible if doing the same action in reverse, gets
// the user back to the starting position. This means that when resizing an edge
// by moving the mouse to the left, then moving the mouse to the right (without
// having ended the resize) should alow the user to get back to the initial
// state.
//
// Even though the implementation is usable, a list of things I would like to
// see implemented in the future are:
//
// - Add an undo/redo system that can be used through Ctrl+Z/Y.
//
// - Feedback on the sizes while editing things. I would like to give feedback
//   about the value that is being changed, be it the glue, width, height, etc.
//
// - Make possible to change the pointer, so we can give feedback about when the
//   left or the right, bottom or top edge is being resized. This could also be
//   used to notify about invalid segment edge resizes (@arbitrary_align).
//
// - Create a keyboard geometry file format. Something like what kv_print() does
//   but better thought out so it can also be parsed.
//
// - Read keysym_representations from a file.
//
// - A tool that allows to search the names for keycodes, to be able to create
//   geometries for keyboards we don't actually have.
//
// - Full implementation of push for edge/segment resize. Currently pushing keys
//   to the right is implemented implicitly by how kv_compute_glue() works.
//   Still more work needs to be done to make it feel more intuitive. For once,
//   we only adjust glue one step beyond where the push happens, see the test
//   case edge_resize_leave_original_pos_2() for example. A full implementation
//   would have to use a better adjust_edge_glue_info_t that stores information
//   about keys beyond a single step of visibility. Also, pushing to the left
//   is not implemented, although we have the tools to edit the glue, and the
//   left edge (kv_adjust_edge_glue(), kv_adjust_left_edge()) we are not using
//   them to fake things being pushed to the left, we need to design an
//   algorithm that detects the value and the position of changes that would
//   fake things being pushed to the left.
//

enum keyboard_view_state_t {
    KV_PREVIEW,
    KV_EDIT,
    KV_EDIT_KEYCODE_KEYPRESS,
    KV_EDIT_KEY_SPLIT,
    KV_EDIT_KEY_SPLIT_NON_RECTANGULAR,
    KV_EDIT_KEY_RESIZE,
    KV_EDIT_KEY_RESIZE_SEGMENT,
    KV_EDIT_KEY_RESIZE_ROW,
    KV_EDIT_KEY_PUSH_RIGHT
    //KV_EDIT_KEYCODE_LOOKUP,
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
    KV_TOOL_DELETE_KEY,
    KV_TOOL_RESIZE_KEY,
    KV_TOOL_RESIZE_SEGMENT,
    KV_TOOL_RESIZE_ROW,
    KV_TOOL_VERTICAL_EXTEND,
    KV_TOOL_VERTICAL_SHRINK,
    KV_TOOL_ADD_KEY,
    KV_TOOL_PUSH_RIGHT
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

// TODO: Alignment modes have been limited to left or right. This leaves out the
// case for arbitrary alignment, making impossible to create some key shapes
// like:                                    +---+
//                                          |   |
//                       +-------+      +---+   +---+
//                       |       |      |           |
//                   +---+   +---+      +---+   +---+      
//                   |       |              |   |
//                   +-------+              +---+
// An arbitrary rendering mode would need a displacement value from a fixed
// reference point in the previous segment (the left edge for example). Even
// though it "could" replace the left and right alignment modes, doing so may
// require a lot of time. When originally choosing to implement right and left
// alignment modes, I chose them because I wanted explicit detection of when two
// contiguous multirow segments make a single straight line as edge. Doing so
// avoids rendering ugly rounded corners.
//
// Now that we have settled on a fixed step size for all widths, this should
// extend to the displacement value for arbitrary alignment. Which means we
// could detect straight edges by comparisons between width and the displacement
// values, without the need for an epsilon value.
//
// Anyway, this change is non trivial because it implies changes in all complex
// parts of the code. Algorithms that would be modified include: non rectangular
// key path drawing, glue computation, size computation, key search from pointer
// coordinates, segment search from pointer coordinates. Also, it's necessary to
// keep an invariant that forbids keys with disjoint segments.
//
// @arbitrary_align
#define sgmt_check_align(sgmt,algn) ((sgmt)->type == KEY_MULTIROW_SEGMENT_SIZED && (sgmt)->align == algn)
enum multirow_key_align_t {
    MULTIROW_ALIGN_LEFT,
    MULTIROW_ALIGN_RIGHT
};

struct sgmt_t {
    int kc; //keycode
    float width, user_glue; // normalized to default_key_size
    float internal_glue;
    enum key_render_type_t type;
    struct sgmt_t *next_sgmt;
    struct sgmt_t *next_multirow;

    // Fields specific to KEY_MULTIROW_SEGMENT_SIZED
    enum multirow_key_align_t align;
};

struct row_t {
    float height; // normalized to default_key_size
    struct row_t *next_row;

    struct sgmt_t *first_key;
};

struct manual_tooltip_t {
    struct manual_tooltip_t *next;
    GdkRectangle rect;
    char *text;
};

enum locate_sgmt_status_t {
    LOCATE_OUTSIDE_TOP,
    LOCATE_OUTSIDE_BOTTOM,
    LOCATE_HIT_KEY,
    LOCATE_HIT_GLUE
};

struct keyboard_view_t {
    mem_pool_t *pool;

    // Pool where all sgmt_t and row_t structures will be allocated
    mem_pool_t keyboard_pool;

    int geometry_idx;

    // Array of sgmt_t pointers indexed by keycode. Provides fast access to keys
    // from a keycode. It's about 6KB in memory, maybe too much?
    struct sgmt_t *keys_by_kc[KEY_MAX];
    struct sgmt_t *spare_keys;
    struct row_t *spare_rows;

    // For now we represent the geometry of the keyboard by having a linked list
    // of rows, each of which is a linked list of keys.
    struct row_t *first_row;

    // xkbcommon state
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;

    // KEY_SPLIT state
    struct sgmt_t *new_key;
    struct sgmt_t **new_key_ptr;
    struct sgmt_t *split_key;
    float left_min_width, right_min_width;
    float split_rect_x;
    float split_full_width;

    // State used by several resize tools: RESIZE_KEY, RESIZE_SEGMENT,
    // RESIZE_ROW, PUSH_RIGHT (user glue resize).
    float clicked_pos; // clicked x or y coordinate
    float original_size;

    // KEY_RESIZE state
    struct sgmt_t *edge_start;
    struct sgmt_t *edge_prev_sgmt, *edge_end_sgmt;
    float original_user_glue;
    float min_width;
    bool edit_right_edge;
    bool do_glue_adjust;
    mem_pool_t resize_pool;
    struct multirow_glue_info_t *edge_glue;
    int edge_glue_len;

    // KEY_RESIZE_SEGMENT state
    struct sgmt_t *resized_segment;
    struct row_t *resized_segment_row;
    struct sgmt_t *resized_segment_prev;
    float resized_segment_glue_plus_w;
    float resized_segment_original_user_glue;
    float resized_segment_original_glue;

    // KEY_RESIZE_ROW state
    bool resize_row_top;
    struct row_t *resized_row;

    // KEY_ADD state
    GdkRectangle to_add_rect;
    bool to_add_rect_hidden;
    float added_key_user_glue;
    struct sgmt_t **added_key_ptr;
    struct row_t *added_key_row;
    enum locate_sgmt_status_t locate_stat;

    // PUSH_RIGHT state
    struct sgmt_t *push_right_key;

    // Manual tooltips list
    mem_pool_t tooltips_pool;
    struct manual_tooltip_t *first_tooltip;
    struct manual_tooltip_t *last_tooltip;

    // GUI related information and state
    GtkWidget *widget;
    GtkWidget *toolbar;
    float default_key_size; // In pixels
    int clicked_kc;
    struct sgmt_t *selected_key;
    enum keyboard_view_state_t state;
    enum keyboard_view_label_mode_t label_mode;
    enum keyboard_view_tools_t active_tool;

    GdkRectangle debug_rect;
};

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

struct sgmt_t* kv_get_multirow_parent (struct sgmt_t *key)
{
    while (!is_multirow_parent (key)) {
        key = key->next_multirow;
    }
    return key;
}

static inline
float get_sgmt_user_glue (struct sgmt_t *sgmt)
{
    struct sgmt_t *parent = kv_get_multirow_parent (sgmt);
    return parent->user_glue;
}

static inline
float get_sgmt_total_glue (struct sgmt_t *sgmt)
{
    return get_sgmt_user_glue(sgmt) + sgmt->internal_glue;
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

// TODO: Consider removing this function and instead adding a pointer in
// sgmt_t to the respective row. Because the move key tool does not seem likely
// to be implemented, this pointer only needs to be set at sgmt_t allocation.
// This would reduce the time complexity of every place this function is called,
// from linear in the number of segments to O(1).
//
// Returns the row where sgmt is located
struct row_t* kv_get_row (struct keyboard_view_t *kv, struct sgmt_t *sgmt)
{
    struct row_t *curr_row = kv->first_row;
    while (curr_row != NULL) {
        struct sgmt_t *curr_sgmt = curr_row->first_key;
        while (curr_sgmt != NULL) {
            if (curr_sgmt == sgmt) {
                break;
            }
            curr_sgmt = curr_sgmt->next_sgmt;
        }

        if (curr_sgmt != NULL) {
            break;
        }
        curr_row = curr_row->next_row;
    }
    return curr_row;
}

static inline
struct sgmt_t* kv_get_prev_sgmt (struct row_t *row, struct sgmt_t *sgmt)
{
    struct sgmt_t *prev = NULL, *curr = row->first_key;
    while (curr != sgmt) {
        prev = curr;
        curr = curr->next_sgmt;
    }
    return prev;
}

static inline
struct sgmt_t* kv_get_prev_multirow (struct sgmt_t *sgmt) {
    struct sgmt_t *curr_key = sgmt;
    while (curr_key->next_multirow != sgmt) {
        curr_key = curr_key->next_multirow;
    }
    return curr_key;
}

static inline
struct row_t* kv_get_prev_row (struct keyboard_view_t *kv, struct row_t *row)
{
    struct row_t *prev_row = NULL, *curr_row = kv->first_row;
    if (curr_row != NULL) {
        while (curr_row != row) {
            prev_row = curr_row;
            curr_row = curr_row->next_row;
        }
    }
    return prev_row;
}

// NOTE: This does not update the multirow circular linked list, as it requires
// computing the prev multirow segment. Then deleting a multirow key would
// become O(n^2) in the length of the key. Althought multirow keys are not big,
// I prefer to not add quadratic costs in unexpected places. To unlink a segment
// from the multirow circular linked list use kv_unlink_multirow_sgmt().
void kv_remove_key_sgmt (struct keyboard_view_t *kv, struct sgmt_t **sgmt_ptr,
                         struct row_t *row, struct sgmt_t *prev_sgmt) // Optional
{
    assert (sgmt_ptr != NULL);

    // NOTE: This does not reset to 0 the content of the segment because
    // multirow deletion needs the next_multirow pointers. Clearing is done
    // at kv_allocate_key().
    struct sgmt_t *tmp = (*sgmt_ptr)->next_sgmt;
    (*sgmt_ptr)->next_sgmt = kv->spare_keys;
    kv->spare_keys = *sgmt_ptr;
    *sgmt_ptr = tmp;
}

// Unlinks sgmt->nex_multirow from the multirow circular linked list. The
// argument prev_multirow is the previous segment in the circular linked list,
// it can be NULL in which case the multirow key is iterated.
static inline
struct sgmt_t* kv_unlink_multirow_sgmt (struct sgmt_t *sgmt, struct sgmt_t *prev_multirow)
{
    if (prev_multirow == NULL) {
        prev_multirow = sgmt;
        while (prev_multirow->next_multirow != sgmt) {
            prev_multirow = prev_multirow->next_multirow;
        }
    }

    assert (prev_multirow->next_multirow == sgmt);
    prev_multirow->next_multirow = sgmt->next_multirow;
    return prev_multirow;
}

// NOTE: If the key being removed can contain the last segment of a row, then
// the call to kv_remove_key() MUST be followed by a call to
// kv_remove_empty_rows(). This is not done unconditionally at the end of
// kv_remove_key() because for example when cancelling a split we know the
// removed key can't be the last one in a row.
void kv_remove_key (struct keyboard_view_t *kv, struct sgmt_t **key_ptr)
{
    assert (key_ptr != NULL);

    struct sgmt_t *multirow_parent = kv_get_multirow_parent(*key_ptr);
    // Remove the pointer to *key_ptr from the lookup table
    kv->keys_by_kc[multirow_parent->kc] = NULL;

    if (is_multirow_key(*key_ptr)) {
        // Rows are singly linked lists so we don't have the pointers to the
        // parent of each multirow segment. We may iterate the whole keyboard to
        // delete a multirow key. I don't think this will be a performance issue
        // as this should not be the most common usecase.

        // Find the row in which multirow_parent is located.
        struct row_t *curr_row = kv_get_row (kv, multirow_parent);

        // Find the parent pointer to each segment and delete it.
        struct sgmt_t *sgmt = multirow_parent;
        struct sgmt_t *prev_sgmt = NULL;
        do {
            struct sgmt_t **to_delete = &curr_row->first_key;
            while (*to_delete != sgmt) {
                to_delete = &(*to_delete)->next_sgmt;
            }

            kv_remove_key_sgmt (kv, to_delete, curr_row, prev_sgmt);

            curr_row = curr_row->next_row;
            prev_sgmt = sgmt;
            sgmt = sgmt->next_multirow;

        } while (sgmt != multirow_parent);

    } else {
        // NOTE: This is the most common case, passing NULL for the last
        // arguments slows things down, but I doubt anyone will notice.
        kv_remove_key_sgmt (kv, key_ptr, NULL, NULL);
    }
}

void kv_remove_empty_rows (struct keyboard_view_t *kv)
{
    struct row_t **row_ptr = &kv->first_row;
    while (*row_ptr != NULL) {
        if ((*row_ptr)->first_key == NULL) {
            struct row_t *tmp = (*row_ptr)->next_row;
            (*row_ptr)->next_row = kv->spare_rows;
            kv->spare_rows = *row_ptr;
            *row_ptr = tmp;

        } else {
            row_ptr = &(*row_ptr)->next_row;
        }
    }
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

float get_sgmt_width (struct sgmt_t *sgmt) {
    float width;
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

void kv_get_size (struct keyboard_view_t *kv, double *width, double *height)
{
    struct row_t *curr_row = kv->first_row;

    double w = 0;
    double h = 0;
    while (curr_row != NULL) {
        h += curr_row->height*kv->default_key_size;

        double row_w = 0;
        struct sgmt_t *curr_key = curr_row->first_key;
        while (curr_key != NULL) {
            row_w += curr_key->internal_glue + get_sgmt_user_glue(curr_key) + get_sgmt_width(curr_key);
            curr_key = curr_key->next_sgmt;
        }
        row_w *= kv->default_key_size;

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
bool compute_key_size_full (struct keyboard_view_t *kv, struct sgmt_t *key, struct row_t *row,
                            float *width, float *height, float *multirow_y_offset)
{
    assert (!kv_is_view_empty(kv));
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
        struct sgmt_t *curr_key = key->next_multirow;
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

    *width = get_sgmt_width (key)*kv->default_key_size;

    return is_rectangular;
}

void kv_print (struct keyboard_view_t *kv)
{
    struct row_t *row = kv->first_row;
    while (row != NULL) {
        struct sgmt_t *sgmt = row->first_key;
        while (sgmt != NULL) {
            if (!is_multirow_key (sgmt)) {
                printf ("(KC: %i, W: %.3f, UG %.3f) ",
                        sgmt->kc, sgmt->width, sgmt->user_glue);

            } else if (is_multirow_parent (sgmt)) {
                printf ("P:(KC: %i, W: %.3f, UG %.3f, IG: %.3f) ",
                        sgmt->kc, sgmt->width, sgmt->user_glue, sgmt->internal_glue);
            } else {
                printf ("S:(W: %.3f, IG: %.3f) ", sgmt->width, sgmt->internal_glue);
            }
            sgmt = sgmt->next_sgmt;
        }
        printf ("\n");
        row = row->next_row;
    }
    printf ("\n");
}

static inline
bool is_supporting_sgmt (struct sgmt_t *sgmt)
{
    return sgmt->internal_glue == 0;
}

// This function simplifies the task of increasing a value that has some
// boundary before (or after) which the update should stop (or start) happening.
//
// Let ov (old value), nv (new value) and b (boundary) be 3 floating point
// values, assuming (for now) that ov != nv != b. Define the active region as
// the region of the number line less than b. This function works in such a way
// that if the change from ov to nv happens inside the active region, then the
// change between these values will be returned. If instead, the change happens
// outside the active region, 0 is returned. If the change in the value crosses
// the boundary into the active region, the returned value is equal to the
// distance between the bounday and the new value (nv - b). If the change in
// value crosses the boundary out of the active region, the returned value is
// the value necessary to move the old value into the boundary (b - ov).
//
//                                retval (positive)
//                              |------->|
//                  +++ov+++++++b--------nv---------
//                      ----------------->
//                           change
//                                                       - inactive region
//                                                       + active region
//                                retval (negative)
//                              |<-------|
//                  +++nv+++++++b--------ov---------
//                      <-----------------
//                           change
//
// Even though the explanation above assumes all values are different, care has
// been taken to correctly handle all cases where equality happens. This, in
// fact, is what makes the implementation tricky, and what motivated abstracting
// it into a documented function.
//
// The macro bnd_delta_update_inv() is a version of the same function where the
// active region, is the region of the number line grater than the boundary b.
//
#define bnd_delta_update_inv(old,new,bnd) (-bnd_delta_update(-(old),-(new),-(bnd)))
static inline
float bnd_delta_update (float old_val, float new_val, float boundary)
{
    if (old_val == new_val) return 0;

    float adjustment = 0;

    bool was_after = old_val < boundary;
    bool is_after = new_val < boundary;
    if (was_after || is_after) {
        if (was_after && is_after) {
            adjustment = new_val - old_val;
        } else {
            if (old_val < new_val) {
                adjustment = boundary - old_val;
            } else {
                adjustment = new_val - boundary;
            }
        }
    }

    return adjustment;
}

// This function adjusts the user glue of the key containing sgmt so that it
// stays fixed in place. It is used when the glue of sgmt changes for some
// reason. The delta_glue argument represents the difference between the new
// total glue to the old total glue (total glue := internal_glue + user_glue).
void kv_adjust_sgmt_glue (struct keyboard_view_t *kv, struct sgmt_t *sgmt, float delta_glue)
{
    if (sgmt != NULL && delta_glue != 0) {
        struct sgmt_t *parent = kv_get_multirow_parent (sgmt);

        if (!is_multirow_key(sgmt)) {
            parent->user_glue = MAX (0, parent->user_glue + delta_glue);

        } else {
            float next_min_glue = INFINITY;
            struct sgmt_t *curr_sgmt = sgmt->next_multirow;
            while (curr_sgmt != sgmt) {
                next_min_glue = MIN (next_min_glue, curr_sgmt->internal_glue);
                curr_sgmt = curr_sgmt->next_multirow;
            }

            // @user_glue_computation
            if (delta_glue > 0) {
                if (is_supporting_sgmt(sgmt)) {
                    parent->user_glue += MIN (next_min_glue, delta_glue);
                }

            } else {
                if (sgmt->internal_glue < -delta_glue) {
                    float maybe_new_glue = parent->user_glue + sgmt->internal_glue + delta_glue;
                    parent->user_glue = MAX (0, maybe_new_glue);
                }
            }
        }
    }
}

#define get_glue_key(is_right_edge,sgmt) (is_right_edge ? sgmt->next_sgmt : sgmt)

// This is a generalization of kv_adjust_sgmt_glue(), that adjusts the glue when
// the same change happens to multiple contiguous segments of a key. At the
// beginning this was done by just calling kv_adjust_sgmt_glue() for each
// segment whose glue would change. Soon I noticed we can't just call
// kv_adjust_sgmt_glue() for all segments, instead this should be done
// conditionelly on some specific cases. As these conditions became more complex
// I decided to separate them into this function. Consider the following
// example:
//
//                                                key_1
//                            +---+               +---+
//                            |   |               | A |
//                            |   |       key_2   |   |
//                            |   X       +---+   |   |
//                            |   |       | B |   | D |
//                            |   |   +---+---+---+   |
//                            |   |   |       C       |
//                            +---+   +---------------+
//
//  When resizing the X edge, the naive approach would change the glue for the
//  A, B and C segments. The problem with this is the glue of key_1 will be
//  updated twice, once for A and then again for C. The straightforward solution
//  was to call kv_adjust_sgmt_glue() in a clever way such as not to call it
//  multiple times on the same glue, but this is not enough, as a call to
//  kv_adjust_sgmt_glue() alters user glue immediately, then the next call to
//  it will have a partial user glue state where some keys have been updated but
//  some don't. This is the main objective of this function, gather all data
//  necessary to produce a correct update, and then uptade all necessary keys
//  from this static data and not from the changing data in the keyboard data
//  structure.
//
//  The way the logic in kv_adjust_sgmt_glue() generalizes, is based arround a
//  the concept of visibility: we say a segment S is visible form segment X if a
//  horizontal line segment from X to S does not cross any other key other than
//  X and S. A call to kv_adjust_sgmt_glue(sgmt) classifies the segments of the
//  key containing smgt into two groups, "sgmt" and "the rest of the segments of
//  the key", next_min_glue is computed from the second group while the maximum
//  user glue is based on the internal glue in the first one. In the
//  generalization kv_adjust_edge_glue(), these groups are replaced respectively
//  by "visible segments" and "non visible segments" (also called blocked
//  segments). To compare exactly how the logic changes see the segments of code
//  marked with @user_glue_computation.

struct adjust_edge_glue_info_t {
    struct sgmt_t *key;
    struct sgmt_t *first_visible_edge;
    struct sgmt_t *first_visible_sgmt;
    float min_glue_visible;
    float min_glue_blocked;
    bool has_blocked_support;
};

void kv_adjust_edge_glue (struct keyboard_view_t *kv,
                          struct sgmt_t *edge_start, struct sgmt_t *edge_end_sgmt, bool is_right_edge,
                          float delta_glue)
{
    if (delta_glue == 0) return;

    int num_rows = kv_get_num_rows (kv);

    struct adjust_edge_glue_info_t info[num_rows];
    int num_info = 0;

    // Find all visible keys and create one info entry for each one, avoiding
    // duplicates.
    struct sgmt_t *curr_sgmt = edge_start;
    do {
        struct sgmt_t *glue_key = is_right_edge ? curr_sgmt->next_sgmt : curr_sgmt;
        if (glue_key) {
            struct sgmt_t *parent = kv_get_multirow_parent (glue_key);

            int i;
            for (i=0; i<num_info; i++) {
                if (info[i].key == parent) break;
            }

            if (i == num_info) {
                info[num_info].key = parent;
                info[num_info].first_visible_sgmt = glue_key;
                info[num_info].first_visible_edge = curr_sgmt;
                num_info++;
            }
        }

        curr_sgmt = curr_sgmt->next_multirow;
    } while (curr_sgmt != edge_end_sgmt);

    // Compute the data required for each info entry.
    for (int i=0; i<num_info; i++) {
        info[i].min_glue_blocked = INFINITY;
        info[i].min_glue_visible = INFINITY;
        info[i].has_blocked_support = false;

        // We iterate each key that has an info entry in 3 stages: 
        //
        // 1) Traverse leading non visible segments in info key.
        struct sgmt_t *info_sgmt = info[i].key;
        while (info_sgmt != info[i].first_visible_sgmt) {
            info[i].has_blocked_support =
                info[i].has_blocked_support || is_supporting_sgmt (info_sgmt);
            info[i].min_glue_blocked = MIN (info[i].min_glue_blocked, info_sgmt->internal_glue);
            info_sgmt = info_sgmt->next_multirow;
        }

        // 2) Traverse edge and info key simultaneously. Handling all info key
        // segments, either as blocked or visible.
        struct sgmt_t *edge_sgmt = info[i].first_visible_edge;
        do {
            struct sgmt_t *glue_key = get_glue_key(is_right_edge,edge_sgmt);
            // Because keys (info key, in particular) are continous vertically
            // and keys can't cross each other (in particular edge key and info
            // key) this must be true:
            assert (glue_key != NULL); 

            if (glue_key == info_sgmt) {
                info[i].min_glue_visible = MIN (info[i].min_glue_visible, glue_key->internal_glue);
            } else {
                info[i].has_blocked_support =
                    info[i].has_blocked_support || is_supporting_sgmt (info_sgmt);
                info[i].min_glue_blocked = MIN (info[i].min_glue_blocked, info_sgmt->internal_glue);
            }

            edge_sgmt = edge_sgmt->next_multirow;
            info_sgmt = info_sgmt->next_multirow;
        } while (edge_sgmt != edge_end_sgmt && info_sgmt != info[i].key);

        // 3) Traverse the remaining non visible segments of the info key if
        // there are any.
        if (info_sgmt != info[i].key) {
            do {
                info[i].has_blocked_support =
                    info[i].has_blocked_support || is_supporting_sgmt (info_sgmt);
                info[i].min_glue_blocked = MIN (info[i].min_glue_blocked, info_sgmt->internal_glue);
                info_sgmt = info_sgmt->next_multirow;
            } while (info_sgmt != info[i].key);
        }
    }

    bool debug_info = 0;
    float old_glue_dbg[num_info];

    // Update the user glue of each info key, based on the computed data.
    for (int i=0; i<num_info; i++) {
        if (debug_info) old_glue_dbg[i] = info[i].key->user_glue;

        if (info[i].min_glue_blocked == INFINITY) {
            // The key is fully visible.
            info[i].key->user_glue = MAX (0, info[i].key->user_glue + delta_glue);

        } else {
            // @user_glue_computation
            if (delta_glue > 0) {
                if (!info[i].has_blocked_support) {
                    info[i].key->user_glue += MIN (info[i].min_glue_blocked, delta_glue); 
                }

            } else {
                if (info[i].min_glue_visible < -delta_glue) {
                    float maybe_new_glue = info[i].key->user_glue + info[i].min_glue_visible + delta_glue;
                    info[i].key->user_glue = MAX (0, maybe_new_glue);
                }
            }
        }
    }

    if (debug_info) {
        struct sgmt_t *curr_sgmt = edge_start;
        printf ("Edge: ");
        do {
            printf ("%p, ", curr_sgmt);
            curr_sgmt = curr_sgmt->next_multirow;
        } while (curr_sgmt != edge_start);
        printf ("\n");

        printf ("Next: ");
        curr_sgmt = edge_start;
        do {
            printf ("%p, ", curr_sgmt->next_sgmt);
            curr_sgmt = curr_sgmt->next_multirow;
        } while (curr_sgmt != edge_start);
        printf ("\n");

        for (int i=0; i<num_info; i++) {
            printf ("Info[%i]\n", i);
            printf ("  Segments: ");
            struct sgmt_t *curr_sgmt = info[i].key;
            do {
                printf ("%p, ", curr_sgmt);
                curr_sgmt = curr_sgmt->next_multirow;
            } while (curr_sgmt != info[i].key);
            printf ("\n");

            printf ("  Key: %p\n", info[i].key);
            printf ("  First visible sgmt: %p\n", info[i].first_visible_sgmt);
            printf ("  Min glue visible: %f\n", info[i].min_glue_visible);
            printf ("  Min glue rest: %f\n", info[i].min_glue_blocked);
            printf ("  Has non visible support: %s\n", info[i].has_blocked_support ? "true" : "false");
            printf ("  User glue change: %f -> %f\n", old_glue_dbg[i], info[i].key->user_glue); 
            printf ("\n");
        }
        printf ("\n");
    }
}

// Pushes the full keyboard right by the amount specified in the change
// argument. Any changes caused by this push, to the user glue of the sgmt
// argument, will be reverted. For exampe the sgmt argument is used in the
// resize segment tool, to avoid pushing the key containing a segment being
// resized beyond the left edge.
void kv_adjust_left_edge (struct keyboard_view_t *kv, struct sgmt_t *sgmt, float change)
{
    int num_rows = kv_get_num_rows (kv);
    if (num_rows == 0) return;

    struct sgmt_t fake_edge[num_rows];
    struct row_t *curr_row = kv->first_row;

    for (int i=0; i<num_rows; i++) {
        fake_edge[i] = ZERO_INIT(struct sgmt_t);
        fake_edge[i].type = KEY_MULTIROW_SEGMENT;
        fake_edge[i].next_sgmt = curr_row->first_key;
        fake_edge[i].next_multirow = &fake_edge[(i+1) % num_rows];
        curr_row = curr_row->next_row;
    }
    fake_edge[0].type = KEY_DEFAULT;

    if (sgmt) {
        struct sgmt_t *parent = kv_get_multirow_parent (sgmt);
        float old_sgmt_glue = parent->user_glue;

        kv_adjust_edge_glue (kv, fake_edge, fake_edge, true, change);

        parent->user_glue = old_sgmt_glue;

    } else {
        kv_adjust_edge_glue (kv, fake_edge, fake_edge, true, change);
    }
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

// Makes sure the amount of user glue in the first keys is the minimum
// necessary. If any change to the keyboard may have reduced the width of the
// total keyboard from the left side, then this must be called.
void kv_equalize_left_edge (struct keyboard_view_t *kv)
{
    int idx = 0;
    struct row_t *curr_row = kv->first_row;
    float extra_glue = INFINITY;
    while (curr_row != NULL) {
        extra_glue = MIN (extra_glue, get_sgmt_total_glue(curr_row->first_key));
        curr_row = curr_row->next_row;
        idx++;
    }

    if (extra_glue != 0) {
        kv_adjust_left_edge (kv, NULL, -extra_glue);
    }
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
                                  struct keyboard_view_t *kv, struct row_t *row, struct sgmt_t *key)
{
    double r = KEY_CORNER_RADIUS;
    double left = x + margin + 0.5;
    double right = left + key->width*kv->default_key_size - 2*margin - 1;

    int num_rows = kv_get_num_rows (kv);
    int num_return_points = 0;
    dvec2 return_path[num_rows*2];

    // Draw top horizontal segment
    struct round_path_ctx ctx = round_path_start (cr, left, y + margin + 0.5, r);
    round_path_move_to (&ctx, right, y + margin + 0.5);

    // Line sweep from top to bottom that both draws the right vertical path and
    // adds points for the left vertical path into a buffer to be drawn later.
    struct sgmt_t *next_segment = key->next_multirow;
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
                             struct row_t *row, struct sgmt_t *key, char *label, dvec4 color)
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
    struct keyboard_view_t *kv = app.keyboard_view;
    cairo_set_source_rgba (cr, 1, 1, 1, 1);
    cairo_paint(cr);
    cairo_set_line_width (cr, 1);

    mem_pool_t pool = {0};
    double left_margin, top_margin;
    keyboard_view_get_margins (kv, &left_margin, &top_margin);

    double y_pos = top_margin;
    struct row_t *curr_row = kv->first_row;
    while (curr_row != NULL) {
        struct sgmt_t *curr_key = curr_row->first_key;
        double x_pos = left_margin;
        while (curr_key != NULL) {

            // Compute the label for the key
            char buff[64];
            buff[0] = '\0';
            if (curr_key->type == KEY_DEFAULT || curr_key->type == KEY_PRESSED) {
                switch (kv->label_mode) {
                    case KV_KEYSYM_LABELS:
                        if (curr_key->kc == KEY_FN) {
                            strcpy (buff, "Fn");
                        }

                        xkb_keysym_t keysym;
                        if (buff[0] == '\0') {
                            int buff_len = 0;
                            keysym = xkb_state_key_get_one_sym(kv->xkb_state, curr_key->kc + 8);
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

            x_pos += (curr_key->internal_glue + get_sgmt_user_glue(curr_key))*kv->default_key_size;

            dvec4 key_color;
            if (curr_key->type != KEY_MULTIROW_SEGMENT) {
                if (kv->selected_key != NULL && curr_key == kv->selected_key) {
                    buff[0] = '\0';
                    key_color = RGB_HEX(0xe34442);

                } else if (curr_key->type == KEY_PRESSED ||
                           (curr_key->type != KEY_UNASSIGNED && curr_key->kc == kv->clicked_kc)) {
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
            curr_key = curr_key->next_sgmt;
        }

        y_pos += curr_row->height*kv->default_key_size;
        curr_row = curr_row->next_row;
    }

    if (kv->active_tool == KV_TOOL_ADD_KEY && !kv->to_add_rect_hidden) {
        cairo_rectangle (cr, kv->to_add_rect.x, kv->to_add_rect.y, kv->to_add_rect.width, kv->to_add_rect.height);
        cairo_set_source_rgba (cr, 0, 0, 1, 0.5);
        cairo_fill (cr);
    }

#if 0
    cairo_rectangle (cr, kv->debug_rect.x, kv->debug_rect.y, kv->debug_rect.width, kv->debug_rect.height);
    cairo_set_source_rgba (cr, 0, 0, 1, 0.5);
    cairo_fill (cr);
#endif

    mem_pool_destroy (&pool);
    return FALSE;
}

// Returns a pointer to the pointer that points to sgmt. It assumes that sgmt is
// located in row, otherwise an assert is triggered.
//
// This pointer to a pointer is useful because it can be located either inside a
// row_t or a sgmt_t. The equivalent approach of returning the parent structure
// to key would require sometimes returning a row_t, when key is the first in
// the row, and a sgmt_t in the rest of the cases.
struct sgmt_t** kv_get_sgmt_ptr (struct row_t *row, struct sgmt_t *sgmt)
{
    struct sgmt_t **key_ptr = &row->first_key;
    while (*key_ptr != sgmt) {
        key_ptr = &(*key_ptr)->next_sgmt;
        assert (*key_ptr != NULL);
    }
    return key_ptr;
}

enum locate_sgmt_status_t
kv_locate_sgmt (struct keyboard_view_t *kv, double x, double y,
                struct sgmt_t **sgmt, struct row_t **row,
                struct sgmt_t ***sgmt_ptr,
                double *x_pos, double *y_pos,
                double *left_margin, double *top_margin)
{
    enum locate_sgmt_status_t status;
    double kbd_x, kbd_y;
    keyboard_view_get_margins (kv, &kbd_x, &kbd_y);

    if (left_margin != NULL) {
        *left_margin = kbd_x;
    }

    if (top_margin != NULL) {
        *top_margin = kbd_y;
    }

    if (y < kbd_y) {
        if (y_pos != NULL) {
            *y_pos = kbd_y;
        }
        return LOCATE_OUTSIDE_TOP;
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

    if (curr_row == NULL) {
        if (y_pos != NULL) {
            *y_pos = kbd_y;
        }
        return LOCATE_OUTSIDE_BOTTOM;
    }

    struct sgmt_t *curr_key = NULL, *prev_key = NULL;
    if (curr_row != NULL) {
        curr_key = curr_row->first_key;
        while (curr_key != NULL) {
            // Check if x is inside a glue, if the point is here we return NULL.
            kbd_x += (curr_key->internal_glue + get_sgmt_user_glue(curr_key))*kv->default_key_size;
            if (kbd_x > x) {
                status = LOCATE_HIT_GLUE;
                break;
            }

            float next_x;
            next_x = kbd_x + get_sgmt_width (curr_key)*kv->default_key_size;

            if (next_x > x) {
                // In this case x_kbd now contains the x coordinate of the
                // rectangle (top left corner).
                status = LOCATE_HIT_KEY;
                break;
            }
            // We only commit to the updated kbd_x after we check we don't want
            // to break.
            kbd_x = next_x;

            prev_key = curr_key;
            curr_key = curr_key->next_sgmt;
        }

        if (curr_key == NULL) {
            status = LOCATE_HIT_GLUE;
        }
    }

    if (x_pos != NULL) {
        *x_pos = kbd_x;
    }

    if (y_pos != NULL) {
        *y_pos = kbd_y;
    }

    if (sgmt != NULL) {
        *sgmt = curr_key;
    }

    if (row != NULL) {
        *row = curr_row;
    }

    if (sgmt_ptr != NULL) {
        *sgmt_ptr = NULL;
        if (status == LOCATE_HIT_KEY || status == LOCATE_HIT_GLUE) {
            if (prev_key == NULL) {
                *sgmt_ptr = &curr_row->first_key;
            } else {
                *sgmt_ptr = &prev_key->next_sgmt;
            }
        }
    }

    return status;
}

struct sgmt_t* keyboard_view_get_key (struct keyboard_view_t *kv, double x, double y,
                                     GdkRectangle *rect, bool *is_rectangular,
                                     struct sgmt_t **clicked_sgmt, struct sgmt_t ***parent_ptr)
{
    double kbd_x, kbd_y;
    struct sgmt_t *curr_key, **curr_key_ptr;
    struct row_t *curr_row;
    enum locate_sgmt_status_t status =
        kv_locate_sgmt (kv, x, y, &curr_key, &curr_row, &curr_key_ptr, &kbd_x, &kbd_y, NULL, NULL);

    if (status == LOCATE_OUTSIDE_TOP || status == LOCATE_OUTSIDE_BOTTOM) {
        return NULL;
    }

    if (status == LOCATE_HIT_KEY) {
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

            // Because we changed the curr_key that will be returned and is
            // expected to be the multirow parent. If th caller also wants it's
            // parent_ptr, then we need to look it up.
            if (parent_ptr != NULL) {
                *parent_ptr = NULL;

                if (curr_key != NULL) {
                    *parent_ptr = kv_get_sgmt_ptr (kv_get_row (kv, curr_key), curr_key);
                }
            }

        } else {
            if (parent_ptr != NULL) {
                *parent_ptr = curr_key_ptr;
            }
        }

    } else { // status == LOCATE_HIT_GLUE
        curr_key = NULL;
    }

    return curr_key;
}

float kv_get_sgmt_x_pos (struct keyboard_view_t *kv, struct sgmt_t *sgmt)
{
    double kbd_x, kbd_y;
    keyboard_view_get_margins (kv, &kbd_x, &kbd_y);

    float x;
    struct row_t *curr_row = kv->first_row;
    while (curr_row != NULL) {
        struct sgmt_t *curr_key = curr_row->first_key;
        x = kbd_x;
        while (curr_key != NULL) {
            x += (curr_key->internal_glue + get_sgmt_user_glue(curr_key))*kv->default_key_size;

            if (curr_key == sgmt) {
                break;
            }

            x += get_sgmt_width (curr_key)*kv->default_key_size;

            curr_key = curr_key->next_sgmt;
        }

        if (curr_key != NULL) {
            break;
        }
        curr_row = curr_row->next_row;
    }

    return x;
}

void kv_update (struct keyboard_view_t *kv, enum keyboard_view_commands_t cmd, GdkEvent *e);

void start_edit_handler (GtkButton *button, gpointer user_data)
{
    kv_update (app.keyboard_view, KV_CMD_SET_MODE_EDIT, NULL);
}
void stop_edit_handler (GtkButton *button, gpointer user_data)
{
    kv_update (app.keyboard_view, KV_CMD_SET_MODE_PREVIEW, NULL);
}

void keycode_keypress_handler (GtkButton *button, gpointer user_data)
{
    app.keyboard_view->active_tool = KV_TOOL_KEYCODE_KEYPRESS;
}

void split_key_handler (GtkButton *button, gpointer user_data)
{
    app.keyboard_view->active_tool = KV_TOOL_SPLIT_KEY;
}

void delete_key_handler (GtkButton *button, gpointer user_data)
{
    app.keyboard_view->active_tool = KV_TOOL_DELETE_KEY;
}

void resize_key_handler (GtkButton *button, gpointer user_data)
{
    app.keyboard_view->active_tool = KV_TOOL_RESIZE_KEY;
}

void resize_segment_handler (GtkButton *button, gpointer user_data)
{
    app.keyboard_view->active_tool = KV_TOOL_RESIZE_SEGMENT;
}

void resize_row_handler (GtkButton *button, gpointer user_data)
{
    app.keyboard_view->active_tool = KV_TOOL_RESIZE_ROW;
}

void vertical_extend_handler (GtkButton *button, gpointer user_data)
{
    app.keyboard_view->active_tool = KV_TOOL_VERTICAL_EXTEND;
}

void vertical_shrink_handler (GtkButton *button, gpointer user_data)
{
    app.keyboard_view->active_tool = KV_TOOL_VERTICAL_SHRINK;
}

void add_key_handler (GtkButton *button, gpointer user_data)
{
    app.keyboard_view->active_tool = KV_TOOL_ADD_KEY;
}

void push_right_handler (GtkButton *button, gpointer user_data)
{
    app.keyboard_view->active_tool = KV_TOOL_PUSH_RIGHT;
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
    kv_push_manual_tooltip (app.keyboard_view, rect, text);
    g_free (text);
}

GtkWidget* toolbar_button_new (const char *icon_name, char *tooltip, GCallback callback, gpointer user_data)
{
    assert (icon_name != NULL);

    GtkWidget *new_button = gtk_button_new_from_icon_name (icon_name, GTK_ICON_SIZE_SMALL_TOOLBAR);
    add_css_class (new_button, "flat");
    g_signal_connect (G_OBJECT(new_button), "clicked", callback, user_data);

    gtk_widget_set_tooltip_text (new_button, tooltip);
    // FIXME: @broken_tooltips_in_overlay
    g_signal_connect (G_OBJECT(new_button), "size-allocate", G_CALLBACK(button_allocated), tooltip);

    gtk_widget_show (new_button);

    return new_button;
}

void kv_toolbar_init (GtkWidget **toolbar)
{
    if (*toolbar == NULL) {
        // NOTE: Using a horizontal GtkBox as container for the toolbar didn't
        // work because it took the full height of the drawing area. Which puts
        // buttons in the center of the keyboard view vertically.
        *toolbar = gtk_grid_new ();
        gtk_widget_show (*toolbar);

    } else {
        gtk_container_foreach (GTK_CONTAINER(*toolbar),
                               destroy_children_callback,
                               NULL);
    }
}

void kv_set_simple_toolbar (GtkWidget **toolbar)
{
    assert (toolbar != NULL);
    kv_toolbar_init (toolbar);

    GtkWidget *edit_button = toolbar_button_new ("edit-symbolic",
                                                 "Edit the view to match your keyboard",
                                                 G_CALLBACK (start_edit_handler), NULL);
    gtk_grid_attach (GTK_GRID(*toolbar), edit_button, 0, 0, 1, 1);

}

void kv_set_full_toolbar (GtkWidget **toolbar)
{
    assert (toolbar != NULL);
    kv_toolbar_init (toolbar);

    int i = 0;
    GtkWidget *stop_edit_button = toolbar_button_new ("close-symbolic",
                                                      "Stop edit mode",
                                                      G_CALLBACK (stop_edit_handler), NULL);
    gtk_grid_attach (GTK_GRID(*toolbar), stop_edit_button, i++, 0, 1, 1);

    GtkWidget *keycode_keypress = toolbar_button_new ("set-keycode-symbolic",
                                                      "Assign keycode by pressing key",
                                                      G_CALLBACK (keycode_keypress_handler), NULL);
    gtk_grid_attach (GTK_GRID(*toolbar), keycode_keypress, i++, 0, 1, 1);

    GtkWidget *add_key_button = toolbar_button_new ("add-key-symbolic",
                                                    "Add key",
                                                    G_CALLBACK (add_key_handler), NULL);
    gtk_grid_attach (GTK_GRID(*toolbar), add_key_button, i++, 0, 1, 1);

    GtkWidget *delete_key_button = toolbar_button_new ("delete-key-symbolic",
                                                      "Delete key",
                                                      G_CALLBACK (delete_key_handler), NULL);
    gtk_grid_attach (GTK_GRID(*toolbar), delete_key_button, i++, 0, 1, 1);

    GtkWidget *split_key_button = toolbar_button_new ("split-key-symbolic",
                                                      "Split key",
                                                      G_CALLBACK (split_key_handler), NULL);
    gtk_grid_attach (GTK_GRID(*toolbar), split_key_button, i++, 0, 1, 1);

    GtkWidget *resize_key_button = toolbar_button_new ("resize-key-symbolic",
                                                      "Resize key edge",
                                                      G_CALLBACK (resize_key_handler), NULL);
    gtk_grid_attach (GTK_GRID(*toolbar), resize_key_button, i++, 0, 1, 1);

    GtkWidget *resize_segment_button = toolbar_button_new ("resize-segment-symbolic",
                                                           "Resize key segment",
                                                           G_CALLBACK (resize_segment_handler), NULL);
    gtk_grid_attach (GTK_GRID(*toolbar), resize_segment_button, i++, 0, 1, 1);

    GtkWidget *resize_row_button = toolbar_button_new ("resize-row-symbolic",
                                                       "Resize row",
                                                       G_CALLBACK (resize_row_handler), NULL);
    gtk_grid_attach (GTK_GRID(*toolbar), resize_row_button, i++, 0, 1, 1);

    GtkWidget *vertical_extend_button = toolbar_button_new ("vextend-key-symbolic",
                                                            "Extend key vertically",
                                                            G_CALLBACK (vertical_extend_handler), NULL);
    gtk_grid_attach (GTK_GRID(*toolbar), vertical_extend_button, i++, 0, 1, 1);

    GtkWidget *vertical_shrink_button = toolbar_button_new ("vshrink-key-symbolic",
                                                            "Shrink key vertically",
                                                            G_CALLBACK (vertical_shrink_handler), NULL);
    gtk_grid_attach (GTK_GRID(*toolbar), vertical_shrink_button, i++, 0, 1, 1);

    GtkWidget *push_right_button = toolbar_button_new ("push-key-symbolic",
                                                       "Move and push keys to the right",
                                                       G_CALLBACK (push_right_handler), NULL);
    gtk_grid_attach (GTK_GRID(*toolbar), push_right_button, i++, 0, 1, 1);
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

// Provides information about an edge of a non rectangular multirow key. Its
// receives the multirow parent of the key and a segment for which we want to
// find the edge, and finally a boolean that specifies which side of the
// provided segment we want to look at.
//
// It sets the following 3 key segments:
//    edge_start: The first segment in the edge. Here is where width should be
//      edited to move the edge.
//
//    edge_prev_sgmt: Is set to the segment that provides the width of the
//      segment previous to edge_start or NULL if such segment does not exist.
//      It is used to know if we should merge this edge.
//
//    edge_end_sgmt: Is the next segment in the multirow cyclic linked list
//      after the edge. This one is never NULL, in the case there is no end
//      segment it will be set to the multirow parent.
//
//    min_width: Is set to the minimum width that edge_start can have so that at
//      least one segment of the multirow key has the minimum width.
//
//    Example call:
//
//    kv_locate_edge (kv, X, K, false, &edge_start, &edge_prev_sgmt, &edge_end_sgmt, &min_w);
//
//            +-----+
//            |  X  |       K: Segment provided as key_sgmt (clicked segment).
//            |   +-+       R: Segment returned in edge_start.
//            | S |         S: Segment returned in edge_prev_sgmt (note it is NOT
//       ++---+   |            edge_start of the previous edge, that would be X).
//       ||   R   |         X: Segment provided as multirow_parent and segment
//       ||     +-+            returned in edge_end_sgmt.
//       ||  K  |
//       ++-----+
//
void kv_locate_edge (struct keyboard_view_t *kv,
                     struct sgmt_t *multirow_parent, struct sgmt_t *key_sgmt, bool is_right_edge,
                     struct sgmt_t **edge_start, struct sgmt_t **edge_prev_sgmt, struct sgmt_t **edge_end_sgmt,
                     float *min_width)
{
    assert (edge_start != NULL && edge_prev_sgmt != NULL && edge_end_sgmt != NULL && min_width != NULL);

    // The start of an edge is marked by a segment aligned with the opposite
    // direction of the edge side we are looking for.
    enum multirow_key_align_t alignment = is_right_edge ?  MULTIROW_ALIGN_LEFT : MULTIROW_ALIGN_RIGHT;

    *edge_prev_sgmt = NULL;
    *edge_start = multirow_parent;
    if (key_sgmt != multirow_parent) {
        struct sgmt_t *curr_key = multirow_parent;
        struct sgmt_t *prev_sized = NULL;
        do {
            if (curr_key->type == KEY_MULTIROW_SEGMENT_SIZED && curr_key->align == alignment) {
                *edge_prev_sgmt = prev_sized;
                *edge_start = curr_key;
            }

            if (curr_key->type != KEY_MULTIROW_SEGMENT) {
                prev_sized = curr_key;
            }
            curr_key = curr_key->next_multirow;
        } while (curr_key != key_sgmt->next_multirow);
    }

    *edge_end_sgmt = multirow_parent;
    {
        struct sgmt_t *curr_key = key_sgmt->next_multirow;
        while (!is_multirow_parent (curr_key)) {
            if (curr_key->type == KEY_MULTIROW_SEGMENT_SIZED &&
                curr_key->align == alignment) {
                *edge_end_sgmt = curr_key;
                break;
            }
            curr_key = curr_key->next_multirow;
        }
    }

    float min_w = (*edge_start)->width;
    {
        struct sgmt_t *curr_key = *edge_start;
        do {
            if (curr_key->type != KEY_MULTIROW_SEGMENT) {
                min_w = MIN (min_w, curr_key->width);
            }
            curr_key = curr_key->next_multirow;
        } while (curr_key != *edge_end_sgmt);
    }
    *min_width = (*edge_start)->width - min_w + kv_get_min_key_width(kv);

#if 0
    // Debug code
    {
        struct sgmt_t *curr_key = multirow_parent;
        do {
            printf ("%p ", curr_key);
            curr_key = curr_key->next_multirow;
        } while (curr_key != multirow_parent);
        printf ("\n");
        printf ("sgmt %p ", key_sgmt);
        printf ("prev %p ", *edge_prev_sgmt);
        printf ("start: %p ", *edge_start);
        printf ("end: %p\n\n", *edge_end_sgmt);
    }
#endif
}

int kv_get_edge_len (struct sgmt_t *edge_start, struct sgmt_t *edge_end_sgmt)
{
    int len = 0;
    struct sgmt_t *curr_key = edge_start;
    do {
        len ++;
        curr_key = curr_key->next_multirow;
    } while (curr_key != edge_end_sgmt);
    return len;
}

struct multirow_glue_info_t {
    struct sgmt_t *key;
    float min_glue;
};

templ_sort (mg_sort, struct multirow_glue_info_t*, (*a)->min_glue > (*b)->min_glue)

void
save_edge_glue (mem_pool_t *pool,
                struct sgmt_t *edge_start, struct sgmt_t *edge_end_sgmt, bool is_right_edge,
                struct multirow_glue_info_t** info, int *info_len)
{
    assert (info != NULL && info_len != NULL);

    int len = kv_get_edge_len (edge_start, edge_end_sgmt);
    struct multirow_glue_info_t info_l[len];
    int num_info = 0;

    struct sgmt_t *curr_sgmt = edge_start;
    do {
        struct sgmt_t *new_glue_key = get_glue_key (is_right_edge, curr_sgmt);
        if (new_glue_key) {
            struct sgmt_t*parent = kv_get_multirow_parent (new_glue_key);

            int i;
            for (i=0; i<num_info; i++) {
                if (info_l[i].key == parent) break;
            }

            if (i == num_info) {
                info_l[num_info].key = parent;
                info_l[num_info].min_glue = get_sgmt_total_glue(new_glue_key);
                num_info++;

            } else {
                float total_glue = get_sgmt_total_glue(new_glue_key);
                if (total_glue < info_l[i].min_glue) {
                    info_l[i].min_glue = total_glue;
                }
            }
        }

        curr_sgmt = curr_sgmt->next_multirow;
    } while (curr_sgmt != edge_end_sgmt);

    //// Check if at least one glue information is non zero
    //bool non_zero_glue = false;
    //for (int i=0; i<num_info && !non_zero_glue; i++) {
    //    if (info_l[i].min_glue != 0) non_zero_glue = true;
    //}

    //if (non_zero_glue) {
        struct multirow_glue_info_t *sorted[num_info];
        for (int i=0; i < num_info; i++) sorted[i] = &info_l[i];
        mg_sort (sorted, num_info);

        *info_len = num_info;
        *info = mem_pool_push_array (pool, num_info, struct multirow_glue_info_t);
        for (int i=0; i<num_info; i++) (*info)[i] = *(sorted[i]);
    //} else {
    //    // Returning no glue information if all segments have zero min glue
    //    // creates the behavior of not adding glue if there was none before.
    //    *info_len = 0;
    //    *info = NULL;
    //}
}

void kv_resize_cleanup (struct keyboard_view_t *kv)
{
    if (kv->edge_glue) {
        mem_pool_destroy (&kv->resize_pool);
        // We know resize_pool isn't bootstrapped so we can do this safely.
        kv->resize_pool = ZERO_INIT(mem_pool_t);
        kv->edge_glue = NULL;
        kv->edge_glue_len = 0;
    }
}

void kv_locate_vedge (struct keyboard_view_t *kv,
                      struct sgmt_t *clicked_sgmt, struct row_t *clicked_row, double ptr_y, double sgmt_y,
                      struct sgmt_t **sgmt, struct sgmt_t **prev_multirow, struct row_t **row, bool *is_top)
{
    *is_top = false;
    *sgmt = clicked_sgmt;
    *row = clicked_row;
    *prev_multirow = NULL;

    int len = 1;
    while (!is_multirow_parent((*sgmt)->next_multirow)) {
        *prev_multirow = *sgmt;
        *sgmt = (*sgmt)->next_multirow;
        *row = (*row)->next_row;
        len++;
    }

    int idx = 0;
    struct sgmt_t *curr_sgmt = (*sgmt)->next_multirow;
    while (curr_sgmt != clicked_sgmt) {
        *prev_multirow = curr_sgmt;
        curr_sgmt = curr_sgmt->next_multirow;
        idx++;
        len++;
    }

    if (!is_multirow_key (*sgmt)) {
        *prev_multirow = *sgmt;
    }
    assert (*prev_multirow != NULL);

    if (len%2 == 1 && idx == len/2) {
        if (ptr_y < sgmt_y + clicked_row->height*kv->default_key_size/2) {
            *is_top = true;
        }

    } else if (idx < len/2) {
        *is_top = true;
    }

    if (*is_top) {
        *prev_multirow = *sgmt;
        *sgmt = (*sgmt)->next_multirow;
        *row = kv_get_row (kv, *sgmt);
    }
}

// Move an edge and handle the cases where the edge should merge with the
// previous or the end segments.
// NOTE: This does not modify the glue in any way, for this take a look at
// kv_change_edge_width().
static inline
void kv_resize_edge (struct sgmt_t *edge_prev_sgmt, struct sgmt_t *edge_start, struct sgmt_t *edge_end_sgmt,
                     float delta_w)
{
    edge_start->width += delta_w;

    if (edge_prev_sgmt != NULL) {
        if (edge_start->width == edge_prev_sgmt->width) {
            edge_start->type = KEY_MULTIROW_SEGMENT;
        } else {
            edge_start->type = KEY_MULTIROW_SEGMENT_SIZED;
        }
    }

    float last_width = edge_start->width;
    struct sgmt_t *curr_key = edge_start->next_multirow;
    while (curr_key != edge_end_sgmt) {
        if (curr_key->type != KEY_MULTIROW_SEGMENT) {
            curr_key->width += delta_w;
            last_width = curr_key->width;
        }
        curr_key = curr_key->next_multirow;
    }

    if (!is_multirow_parent(edge_end_sgmt)) {
        if (last_width == edge_end_sgmt->width) {
            edge_end_sgmt->type = KEY_MULTIROW_SEGMENT;
        } else {
            edge_end_sgmt->type = KEY_MULTIROW_SEGMENT_SIZED;
        }
    }
}

// Detects if an edge is left-visible, which means all of its segments are the
// first segment in a row.
bool kv_is_edge_left_visible (struct keyboard_view_t *kv,
                              struct sgmt_t *edge_start, struct sgmt_t *edge_end_sgmt)
{
    struct row_t *row = kv_get_row (kv, edge_start);
    struct sgmt_t *curr_sgmt = edge_start;
    do {
        if (row->first_key != curr_sgmt) {
            return false;
        }
        row = row->next_row;
        curr_sgmt = curr_sgmt->next_multirow;
    } while (curr_sgmt != edge_end_sgmt);
    return true;
}

// This function computes how much a change in an edge will make the left edge
// change. The reason it is not so straightforward is that if the edge is
// touching the left margin (it has internal glue == 0), then the point at which
// the change should stop happening must be computed from the minimum total glue
// of the other segments that are the first in a row.
// NOTE: This function assumes all edge segment are visible from the left edge,
// this is ensured by the function kv_is_edge_left_visible().
// TODO: Is there a simpler way to detect the left edge boundary?
void kv_compute_left_edge_change (struct keyboard_view_t *kv,
                                  struct sgmt_t *edge_start, struct sgmt_t *edge_end_sgmt,
                                  float old_width, float new_width,
                                  float *left_edge_adjust, float *glue_adjust)
{
    assert (left_edge_adjust != NULL && glue_adjust != NULL);

    float g = INFINITY;
    struct row_t *row = kv->first_row;
    struct sgmt_t *edge_sgmt = edge_start;
    int edge_len = kv_get_edge_len (edge_start, edge_end_sgmt);
    int edge_idx = 0;
    while (row != NULL) {
        if (row->first_key == edge_sgmt && edge_idx < edge_len) {
            edge_sgmt = edge_sgmt->next_multirow;
            edge_idx++;
        } else {
            g = MIN (g, get_sgmt_total_glue (row->first_key));
        }
        row = row->next_row;
    }

    float left_edge_bnd = g==INFINITY ?
        0 : edge_start->width + get_sgmt_total_glue(edge_start) - g;
    *left_edge_adjust = bnd_delta_update_inv (old_width, new_width, left_edge_bnd);
    *glue_adjust = old_width - new_width + *left_edge_adjust;
}

bool kv_key_has_glue (struct keyboard_view_t *kv,
                       bool is_right_edge, struct sgmt_t *sgmt)
{
    struct sgmt_t *curr_sgmt = sgmt;
    do {
        struct sgmt_t *glue_sgmt = get_glue_key (is_right_edge, curr_sgmt);
        if (glue_sgmt && get_sgmt_total_glue (glue_sgmt) != 0) {
            return true;
        }
        curr_sgmt = curr_sgmt->next_multirow;
    } while (curr_sgmt != sgmt);

    return false;
}

// Function called by the resize edge tool. Just like the resize segment tool we
// want the edge resize tool to be reversible.
//
// The implementation can be seen as a generalization of kv_change_sgmt_width().
// Where kv_change_sgmt_width() may do one extra step, this function may do
// glue_info_len steps. For an explanation of why multiple steps are required
// see the explanation in the comments of kv_change_sgmt_width() for why one
// extra step is required there.
void kv_change_edge_width (struct keyboard_view_t *kv,
                           struct sgmt_t *edge_prev_sgmt, struct sgmt_t *edge_start, struct sgmt_t *edge_end_sgmt,
                           bool is_right_edge, bool do_glue_adjust,
                           struct multirow_glue_info_t *glue_info, int glue_info_len,
                           float original_w, float new_width)
{
    float delta_w = new_width - kv->edge_start->width;
    float glue_adjust = -delta_w;

    // Maybe adjust left edge if the edited edge goes beyond the left margin
    bool did_left_edge_adjust = false;
    if (!is_right_edge) {
        if (kv_is_edge_left_visible (kv, edge_start, edge_end_sgmt)) {
            float left_edge_adjust;
            kv_compute_left_edge_change (kv, edge_start, edge_end_sgmt,
                                         edge_start->width, new_width,
                                         &left_edge_adjust, &glue_adjust);
            if (left_edge_adjust != 0) {
                kv_adjust_left_edge (kv, edge_start, left_edge_adjust);
                did_left_edge_adjust = true;
            }
        }
    }

    // If the edge being resized pushed some keys, then leave them at their
    // original positions.
    if (!did_left_edge_adjust && do_glue_adjust && delta_w < 0 && edge_start->width > original_w) {
        int i = 0;
        // Ignore glue_info entries if they haven't been pushed yet.
        while (i < glue_info_len && kv->edge_start->width < original_w + glue_info[i].min_glue) i++;

        // This iterates each glue_info entry and resizes the edge and adjusts
        // the glue to leave each key in its original position.
        while (i < glue_info_len &&
               new_width <= original_w + glue_info[i].min_glue) {
            float step = original_w + glue_info[i].min_glue - kv->edge_start->width;
            if (step != 0) {
                kv_resize_edge (edge_prev_sgmt, edge_start, edge_end_sgmt, step);
                kv_adjust_edge_glue (kv, edge_start, edge_end_sgmt, is_right_edge, -step);
                delta_w -= step;

                // The previous kv_adjust_edge_glue() leaves everything fixed in
                // place, this resets the user glue to 0 of keys that should be
                // still being pushed by the edge.
                int undo_i = i;
                while (undo_i < glue_info_len) {
                    glue_info[undo_i].key->user_glue = 0;
                    undo_i++;
                }
                kv_compute_glue (kv);
            }
            i++;
        }

        // If new_width > original_w this step sets the edge's width to
        // new_width, otherwise, it sets it to original_w so that later we
        // handle what's left in delta_w. It must be handled here, because there
        // may be multiple keys that will still collide with the edge.
        // @final_edge_step
        float step = MAX (new_width, original_w) - kv->edge_start->width;
        if (step != 0) {
            kv_resize_edge (edge_prev_sgmt, edge_start, edge_end_sgmt, step);
            kv_adjust_edge_glue (kv, edge_start, edge_end_sgmt, is_right_edge, -step);
            delta_w -= step;

            while (i < glue_info_len) {
                glue_info[i].key->user_glue = 0;
                i++;
            }
            kv_compute_glue (kv);
        }

        // delta_w may have changed, update glue_adjust
        glue_adjust = -delta_w;
    }

    // Handle two cases: 1) Growing edges (delta_w > 0). 2) The remaining
    // delta_w when shrinking an edge (delta_w < 0) to a new_width less than
    // original_w.
    // NOTE: In kv_change_sgmt_width(), instead of original_w we say
    // original_glue_plus_w. Here, the case of a shrinking edge where new_width
    // is grater than original_w is handled by @final_edge_step. In
    // kv_change_sgmt_width(), this final step is handled in
    // @normal_sgmt_resize_case, because there can only be one pushed key.
    if (delta_w != 0) {
        kv_resize_edge (edge_prev_sgmt, edge_start, edge_end_sgmt, delta_w);

        if (do_glue_adjust) {
            kv_adjust_edge_glue (kv, edge_start, edge_end_sgmt, is_right_edge, glue_adjust);
        }
    }
}

// Resize a segment and handle the cases where the segment should merge with the
// one before or with the one after.
// NOTE: This does not modify any glue in any way, for this take a look at
// kv_change_sgmt_width().
void kv_resize_sgmt (struct keyboard_view_t *kv, struct sgmt_t *prev_multirow,
                     struct sgmt_t *sgmt, float delta_w, bool edit_right_edge)
{
    sgmt->width += delta_w;

    if (prev_multirow != NULL) {
        if (sgmt->width == get_sgmt_width(prev_multirow)) {
            sgmt->type = KEY_MULTIROW_SEGMENT;
        } else {
            sgmt->type = KEY_MULTIROW_SEGMENT_SIZED;
        }
    }

    if (sgmt->type == KEY_MULTIROW_SEGMENT_SIZED) {
        sgmt->align = edit_right_edge ? MULTIROW_ALIGN_LEFT : MULTIROW_ALIGN_RIGHT;
    }

    if (is_multirow_key (sgmt) && !is_multirow_parent(sgmt->next_multirow)) {
        struct sgmt_t *end_sgmt = sgmt->next_multirow;
        if (sgmt->width == end_sgmt->width) {
            end_sgmt->type = KEY_MULTIROW_SEGMENT;
        } else {
            end_sgmt->type = KEY_MULTIROW_SEGMENT_SIZED;
            end_sgmt->align = edit_right_edge ? MULTIROW_ALIGN_LEFT : MULTIROW_ALIGN_RIGHT;
        }
    }
}

// This is the function called by the resize segment tool. A property we want to
// keep for tools is that while a modification is being made, the user should be
// able to get back to the original state. We call this property reversibility.
// Consider the following case:
//
//                 STATE A                         STATE B
//              +---+   +---+     SIMPLE      +-----------+---+
//              |   X   |   |   ---------->   |           X   |
//              |   +   | K |                 |   +-------+ K |
//              |   |   |   |   HAS 2 CASES   |   |       |   |
//              +---+   +---+   <----------   +---+       +---+
//
//              START POSITION                 KEY K IS PUSHED
//
//                           @segment_resize_img
//
// In this case the segment edge marked with X is dragged to the right, during
// this drag the user glue for K is reduced (adjusted with negative value), so
// that it looks like it's being consumed. After the user glue becomes 0, key K
// gets pushed to the right (which is implemented by the clamping of user glues
// to values >0) because we don't want to force the user to switch to another
// tool (push key right) if it's not necessary. The segment width at which this
// collision happens is stored in the original_glue_plus_w argument.
//
// The problem with the behavior of pushing K, is that now going back to state A
// isn't straight forward. When the edge moves back, the glue for K should not
// be adjusted until K reaches its original position (segment's width is equal
// to original_glue_plus_w). Beyond this value, the user glue should grow to
// keep K fixed in place.

void kv_change_sgmt_width (struct keyboard_view_t *kv,
                           struct sgmt_t *prev_multirow, struct sgmt_t *sgmt,
                           bool is_right_edge, bool do_glue_adjust,
                           struct row_t *row, float original_glue_plus_w,
                           float original_glue, float new_width)
{
    // It would seem like the straight forward implementation of both cases
    // described above would be a call to kv_resize_sgmt() followed by a
    // conditional call to kv_adjust_sgmt_glue() depending if the collision with
    // a key happened or not. Sadly, things are a bit more complex. Consider
    // the case from @segment_resize_img, after going from state A to state B,
    // suppose the user moves edge X in a single step so that the segment is
    // smaller than it originally was. The resulting state should be the
    // following:
    //
    //                              glue_adj
    //                              |-----|
    //                            +-+     +---+
    //                            | X     |K_1|
    //                            | +-+   |   |
    //                            |   |   |K_2|
    //                            +---+   +---+
    //                               STATE C
    //
    // The call to kv_adjust_sgmt_glue() should adjust the glue by glue_adj as
    // shown in state C, because in State B, the total glue for the segment K_1
    // was 0.  Normally kv_adjust_sgmt_glue() assumes the key whose glue is
    // being adjusted will remain static, but K should move to the position it
    // had in state A, before actually adjusting the glue.  The internal glue
    // for the segment K_2 in state B includes the distance by which K was
    // pushed to the right, then if glue_adj is less than K_2's internal_glue at
    // state B, the call to kv_adjust_sgmt_glue() will not notice that K_1 stops
    // being the supporting segment. This incorrectly sets K's user glue to
    // glue_adj, when it should have been set to K_2's original internal glue
    // (the one it had in state A).
    //
    // To handle this case segment resizing may happen in two stages. One that
    // puts K into it's original position, followed by an internal glue
    // recomputation. Then a second stage of a segment resize and glue
    // adjustment for the remainig value of the total change.

    float delta_w = new_width - sgmt->width;
    float glue_adjust = -delta_w;

    // Maybe adjust left edge if the edited edge goes beyond the left margin
    bool did_left_edge_adjust = false;
    if (!is_right_edge) {
        if (row->first_key == sgmt) { // Ensure sgmt is left-visible
            float left_edge_adjust;
            kv_compute_left_edge_change (kv, sgmt, sgmt->next_multirow,
                                         sgmt->width, new_width,
                                         &left_edge_adjust, &glue_adjust);
            if (left_edge_adjust != 0) {
                kv_adjust_left_edge (kv, sgmt, left_edge_adjust);
                did_left_edge_adjust = true;
            }
        }
    }

    // If the segment being resized pushed a key, then leave it at its original
    // position.
    float step_dw = bnd_delta_update_inv (sgmt->width, sgmt->width + delta_w, original_glue_plus_w);
    if (!did_left_edge_adjust && do_glue_adjust && delta_w < 0 && step_dw != 0) {
        kv_resize_sgmt (kv, prev_multirow, sgmt, step_dw, is_right_edge);
        kv_compute_glue (kv);
        delta_w -= step_dw;

        // delta_w may have changed, update glue_adjust
        glue_adjust = -delta_w;
    }

    // Handle two cases: 1) Growing edges (delta_w > 0). 2) The remaining
    // delta_w when shrinking an edge (delta_w < 0) to a new_width less than
    // original_glue_plus_w. @normal_sgmt_resize_case
    if (delta_w != 0) {
        kv_resize_sgmt (kv, prev_multirow, sgmt, delta_w, is_right_edge);

        if (do_glue_adjust) {
            kv_adjust_sgmt_glue (kv, get_glue_key (is_right_edge, sgmt), glue_adjust);
        }
    }
}

struct sgmt_t* kv_create_multirow_split (struct keyboard_view_t *kv,
                                        struct sgmt_t *start_sgmt, struct sgmt_t *end_sgmt,
                                        struct sgmt_t **start_sgmt_ptr,
                                        bool add_split_after,
                                        struct sgmt_t ***new_key_ptr)
{
    struct sgmt_t *new_key = kv_allocate_key (kv);
    new_key->type = KEY_UNASSIGNED;

    if (add_split_after) {
        struct sgmt_t *sgmt = start_sgmt;
        struct sgmt_t *new_sgmt = new_key;

        do {
            new_sgmt->next_sgmt = sgmt->next_sgmt;
            sgmt->next_sgmt = new_sgmt;

            sgmt = sgmt->next_multirow;
            if (sgmt != end_sgmt) {
                struct sgmt_t *tmp = kv_allocate_key (kv);
                tmp->type = KEY_MULTIROW_SEGMENT;
                tmp->next_multirow = new_sgmt->next_multirow;
                new_sgmt->next_multirow = tmp;

                new_sgmt = tmp;
            }

        } while (sgmt != end_sgmt);

        if (new_key_ptr != NULL) {
            *new_key_ptr = &start_sgmt->next_sgmt;
        }

    } else {
        struct sgmt_t *sgmt = start_sgmt;
        struct sgmt_t *new_sgmt = new_key;
        struct row_t *curr_row = kv_get_row (kv, start_sgmt);

        if (start_sgmt_ptr == NULL) {
            start_sgmt_ptr = kv_get_sgmt_ptr (curr_row, start_sgmt);
        }
        struct sgmt_t **sgmt_ptr = start_sgmt_ptr;

        do {
            *sgmt_ptr = new_sgmt;
            new_sgmt->next_sgmt = sgmt;

            sgmt = sgmt->next_multirow;
            if (sgmt != end_sgmt) {
                curr_row = curr_row->next_row;
                sgmt_ptr = kv_get_sgmt_ptr (curr_row, sgmt);

                struct sgmt_t *tmp = kv_allocate_key (kv);
                tmp->type = KEY_MULTIROW_SEGMENT;
                tmp->next_multirow = new_sgmt->next_multirow;
                new_sgmt->next_multirow = tmp;

                new_sgmt = tmp;
            }

        } while (sgmt != end_sgmt);

        if (new_key_ptr != NULL) {
            *new_key_ptr = start_sgmt_ptr;
        }
    }

    return new_key;
}

static inline
void compute_split_widths (struct keyboard_view_t *kv, float cursor_x,
                           float *left_width, float *right_width)
{
    *left_width = bin_floor ((cursor_x - kv->split_rect_x)/kv->default_key_size, 3);
    *left_width = CLAMP (*left_width, kv->left_min_width, kv->split_full_width - kv->right_min_width);
    *right_width = kv->split_full_width - *left_width;
}

void kv_set_rectangular_split (struct keyboard_view_t *kv, float x)
{
    float left_width, right_width;
    compute_split_widths (kv, x, &left_width, &right_width);

    if (kv->edit_right_edge) {
        kv->split_key->width = left_width;
        kv->new_key->width = right_width;

    } else {
        kv->split_key->width = right_width;
        kv->new_key->width = left_width;
    }
}

void kv_set_non_rectangular_split (struct keyboard_view_t *kv, float x)
{
    float split_key_width, new_key_width;
    if (kv->edit_right_edge) {
        compute_split_widths (kv, x, &split_key_width, &new_key_width);
    } else {
        compute_split_widths (kv, x, &new_key_width, &split_key_width);
    }

    float delta_w = split_key_width - kv->edge_start->width;
    if (delta_w != 0) {
        kv_resize_edge (kv->edge_prev_sgmt, kv->edge_start, kv->edge_end_sgmt, delta_w);
        kv->new_key->width = new_key_width;
    }
}

void kv_set_add_key_state (struct keyboard_view_t *kv, double event_x, double event_y)
{
    double x, y, left_margin;
    struct sgmt_t *sgmt, **sgmt_ptr;
    struct row_t *row;
    kv->locate_stat =
        kv_locate_sgmt (kv, event_x, event_y, &sgmt, &row, &sgmt_ptr, &x, &y, &left_margin, NULL);
    kv->added_key_row = row;

    if (kv->locate_stat == LOCATE_HIT_KEY) {
        double width, height;
        width = get_sgmt_width(sgmt)*kv->default_key_size;
        height = row->height*kv->default_key_size;

        kv->to_add_rect.x = x - kv->default_key_size*0.125;
        kv->to_add_rect.y = y;

        kv->to_add_rect.width = kv->default_key_size*0.25;
        kv->to_add_rect.height = height;

        if (event_x > x + width/2) {
            kv->added_key_ptr = &sgmt->next_sgmt;
            kv->to_add_rect.x += width;
            kv->added_key_user_glue = 0;
        } else {
            kv->added_key_ptr = sgmt_ptr;
            kv->added_key_user_glue = sgmt->internal_glue - 1;
        }

    } else if (kv->locate_stat == LOCATE_HIT_GLUE) {
        kv->added_key_ptr = sgmt_ptr;
        kv->to_add_rect.height = row->height*kv->default_key_size;
        kv->to_add_rect.y = y;

        float glue;
        if (*sgmt_ptr == row->first_key) {
            // Pointer is left of the keyboard

            kv->to_add_rect.width = kv->default_key_size;
            // NOTE: This glue is measured with respect to the left
            // margin. It can be negative.
            glue = bin_floor ((event_x - left_margin)/kv->default_key_size - 0.5, 3);
            glue = MIN (glue, (x - left_margin)/kv->default_key_size - 1);
            kv->to_add_rect.x = left_margin + glue*kv->default_key_size;

        } else if (*sgmt_ptr == NULL) {
            // Pointer is right of keyboard

            kv->to_add_rect.width = kv->default_key_size;
            glue = bin_floor ((event_x - x)/kv->default_key_size - 0.5, 3);
            glue = MAX (glue, 0);

            kv->to_add_rect.x = x + glue*kv->default_key_size;

        } else {
            // Pointer is inside the keyboard

            float total_glue = get_sgmt_user_glue (sgmt) + sgmt->internal_glue;
            float glue_x = x - total_glue*kv->default_key_size;

            if (total_glue < 1) {
                kv->to_add_rect.width = total_glue*kv->default_key_size;
                glue = 0;
            } else {
                kv->to_add_rect.width = MIN (1, total_glue)*kv->default_key_size;
                glue = bin_floor ((event_x - glue_x)/kv->default_key_size - 0.5, 3);
                glue = CLAMP (glue, 0, total_glue - 1);
            }

            kv->to_add_rect.x = glue_x + glue*kv->default_key_size;
        }

        kv->added_key_user_glue = glue;

    } else {
        kv->to_add_rect.width = kv->default_key_size;
        kv->to_add_rect.height = kv->default_key_size;

        if (kv_is_view_empty (kv)) {
            kv->added_key_user_glue = 0;
            kv->to_add_rect.x = left_margin - kv->default_key_size/2;
            kv->to_add_rect.y = y - kv->default_key_size/2;

        } else {
            float x_pos = bin_floor ((event_x - left_margin)/kv->default_key_size - 0.5, 3);
            kv->to_add_rect.x = left_margin + x_pos*kv->default_key_size;
            // NOTE: This glue is measured with respect to the left
            // margin. It can be negative.
            kv->added_key_user_glue = x_pos;

            if (kv->locate_stat == LOCATE_OUTSIDE_TOP) {
                kv->to_add_rect.y = y - kv->default_key_size;
            } else { // kv->locate_stat == LOCATE_OUTSIDE_BOTTOM
                kv->to_add_rect.y = y;
            }
        }
    }
}

struct sgmt_t* kv_insert_new_sgmt (struct keyboard_view_t *kv,
                                  enum locate_sgmt_status_t location, struct sgmt_t **sgmt_ptr)
{
    // Allocate a new row if necessary.
    struct row_t *new_row = NULL;
    if (kv_is_view_empty (kv) || location == LOCATE_OUTSIDE_TOP || location == LOCATE_OUTSIDE_BOTTOM) {
        new_row = kv_allocate_row (kv);
        if (kv_is_view_empty (kv) || location == LOCATE_OUTSIDE_TOP) {
            new_row->next_row = kv->first_row;
            kv->first_row = new_row;

        } else { // location == LOCATE_OUTSIDE_BOTTOM
            // Get last row
            struct row_t *last_row = kv->first_row;
            while (last_row->next_row != NULL) last_row = last_row->next_row;

            last_row->next_row = new_row;
            last_row = new_row;
        }

        sgmt_ptr = &new_row->first_key;
    }

    // Allocate the new key
    struct sgmt_t *new_key = kv_allocate_key (kv);
    new_key->type = KEY_UNASSIGNED;
    new_key->width = 1;

    // Insert the new key
    new_key->next_sgmt = *sgmt_ptr;
    *(sgmt_ptr) = new_key;

    return new_key;
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
        new_key->type = KEY_UNASSIGNED;
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

#define BUILD_GEOMETRY_FUNC(name) \
    void name(struct keyboard_view_t *kv)
typedef BUILD_GEOMETRY_FUNC(set_geometry_func_t);

// Simple default keyboard geometry.
// NOTE: Keycodes are used as defined in the linux kernel. To translate them
// into X11 keycodes offset them by 8 (x11_kc = kc+8).
BUILD_GEOMETRY_FUNC(kv_build_default_geometry)
{
    struct geometry_edit_ctx_t ctx;
    kv_geometry_ctx_init_append (kv, &ctx);

    kv_new_row (&ctx);
    kv_add_key (&ctx, KEY_ESC);
    kv_add_key (&ctx, KEY_F1);
    kv_add_key (&ctx, KEY_F2);
    kv_add_key (&ctx, KEY_F3);
    kv_add_key (&ctx, KEY_F4);
    kv_add_key (&ctx, KEY_F5);
    kv_add_key (&ctx, KEY_F6);
    kv_add_key (&ctx, KEY_F7);
    kv_add_key (&ctx, KEY_F8);
    kv_add_key (&ctx, KEY_F9);
    kv_add_key (&ctx, KEY_F10);
    kv_add_key (&ctx, KEY_F11);
    kv_add_key (&ctx, KEY_F12);
    kv_add_key (&ctx, KEY_NUMLOCK);
    kv_add_key (&ctx, KEY_SCROLLLOCK);
    kv_add_key (&ctx, KEY_INSERT);

    kv_new_row (&ctx);
    kv_add_key (&ctx, KEY_GRAVE);
    kv_add_key (&ctx, KEY_1);
    kv_add_key (&ctx, KEY_2);
    kv_add_key (&ctx, KEY_3);
    kv_add_key (&ctx, KEY_4);
    kv_add_key (&ctx, KEY_5);
    kv_add_key (&ctx, KEY_6);
    kv_add_key (&ctx, KEY_7);
    kv_add_key (&ctx, KEY_8);
    kv_add_key (&ctx, KEY_9);
    kv_add_key (&ctx, KEY_0);
    kv_add_key (&ctx, KEY_MINUS);
    kv_add_key (&ctx, KEY_EQUAL);
    kv_add_key_w (&ctx, KEY_BACKSPACE, 2);
    kv_add_key (&ctx, KEY_HOME);

    kv_new_row (&ctx);
    kv_add_key_w (&ctx, KEY_TAB, 1.5);
    kv_add_key (&ctx, KEY_Q);
    kv_add_key (&ctx, KEY_W);
    kv_add_key (&ctx, KEY_E);
    kv_add_key (&ctx, KEY_R);
    kv_add_key (&ctx, KEY_T);
    kv_add_key (&ctx, KEY_Y);
    kv_add_key (&ctx, KEY_U);
    kv_add_key (&ctx, KEY_I);
    kv_add_key (&ctx, KEY_O);
    kv_add_key (&ctx, KEY_P);
    kv_add_key (&ctx, KEY_LEFTBRACE);
    kv_add_key (&ctx, KEY_RIGHTBRACE);
    kv_add_key_w (&ctx, KEY_BACKSLASH, 1.5);
    kv_add_key (&ctx, KEY_PAGEUP);

    kv_new_row (&ctx);
    kv_add_key_w (&ctx, KEY_CAPSLOCK, 1.75);
    kv_add_key (&ctx, KEY_A);
    kv_add_key (&ctx, KEY_S);
    kv_add_key (&ctx, KEY_D);
    kv_add_key (&ctx, KEY_F);
    kv_add_key (&ctx, KEY_G);
    kv_add_key (&ctx, KEY_H);
    kv_add_key (&ctx, KEY_J);
    kv_add_key (&ctx, KEY_K);
    kv_add_key (&ctx, KEY_L);
    kv_add_key (&ctx, KEY_SEMICOLON);
    kv_add_key (&ctx, KEY_APOSTROPHE);
    kv_add_key_w (&ctx, KEY_ENTER, 2.25);
    kv_add_key (&ctx, KEY_PAGEDOWN);

    kv_new_row (&ctx);
    kv_add_key_w (&ctx, KEY_LEFTSHIFT, 2.25);
    kv_add_key (&ctx, KEY_Z);
    kv_add_key (&ctx, KEY_X);
    kv_add_key (&ctx, KEY_C);
    kv_add_key (&ctx, KEY_V);
    kv_add_key (&ctx, KEY_B);
    kv_add_key (&ctx, KEY_N);
    kv_add_key (&ctx, KEY_M);
    kv_add_key (&ctx, KEY_COMMA);
    kv_add_key (&ctx, KEY_DOT);
    kv_add_key (&ctx, KEY_SLASH);
    kv_add_key_w (&ctx, KEY_RIGHTSHIFT, 1.75);
    kv_add_key (&ctx, KEY_UP);
    kv_add_key (&ctx, KEY_END);

    kv_new_row (&ctx);
    kv_add_key_w (&ctx, KEY_LEFTCTRL, 1.5);
    kv_add_key_w (&ctx, KEY_LEFTMETA, 1.5);
    kv_add_key_w (&ctx, KEY_LEFTALT, 1.5);
    kv_add_key_w (&ctx, KEY_SPACE, 5.5);
    kv_add_key_w (&ctx, KEY_RIGHTALT, 1.5);
    kv_add_key_w (&ctx, KEY_RIGHTCTRL, 1.5);
    kv_add_key (&ctx, KEY_LEFT);
    kv_add_key (&ctx, KEY_DOWN);
    kv_add_key (&ctx, KEY_RIGHT);

    kv_end_geometry (&ctx);
}

BUILD_GEOMETRY_FUNC(multirow_test_geometry)
{
    struct geometry_edit_ctx_t ctx;
    kv_geometry_ctx_init_append (kv, &ctx);

    kv_new_row_h (&ctx, 1.5);
    struct sgmt_t *multi1 = kv_add_key (&ctx, KEY_A);
    kv_add_key (&ctx, KEY_1);
    //kv_add_key (&ctx, KEY_2);
    struct sgmt_t *multi4 = kv_add_key_w (&ctx, KEY_D, 2);

    kv_new_row_h (&ctx, 1.25);
    struct sgmt_t *multi2 = kv_add_key (&ctx, KEY_B);
    kv_add_multirow_sgmt (&ctx, multi1);
    kv_add_key_full (&ctx, KEY_3, 1, 1);
    kv_add_multirow_sized_sgmt (&ctx, multi4, 1, MULTIROW_ALIGN_LEFT);

    kv_new_row_h (&ctx, 1);
    kv_add_key (&ctx, KEY_4);
    kv_add_multirow_sgmt (&ctx, multi2);
    struct sgmt_t *multi3 = kv_add_key (&ctx, KEY_C);
    kv_add_multirow_sgmt (&ctx, multi4);

    kv_new_row_h (&ctx, 0.75);
    kv_add_key (&ctx, KEY_5);
    kv_add_key (&ctx, KEY_6);
    kv_add_multirow_sgmt (&ctx, multi3);
    kv_add_multirow_sized_sgmt (&ctx, multi4, 3, MULTIROW_ALIGN_RIGHT);

    kv_end_geometry (&ctx);
}

BUILD_GEOMETRY_FUNC(edge_resize_leave_original_pos_1)
{
    struct geometry_edit_ctx_t ctx;
    kv_geometry_ctx_init_append (kv, &ctx);

    kv_new_row_h (&ctx, 1);
    struct sgmt_t *m = kv_add_key_w (&ctx, KEY_A, 3);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sized_sgmt (&ctx, m, 2, MULTIROW_ALIGN_LEFT);
    kv_add_key (&ctx, KEY_1);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sized_sgmt (&ctx, m, 3, MULTIROW_ALIGN_RIGHT);
    kv_add_key_full (&ctx, KEY_2, 1, 1);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sized_sgmt (&ctx, m, 4, MULTIROW_ALIGN_RIGHT);
    kv_add_key_full (&ctx, KEY_3, 1, 2);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sized_sgmt (&ctx, m, 3, MULTIROW_ALIGN_LEFT);

    kv_end_geometry (&ctx);
}

BUILD_GEOMETRY_FUNC(edge_resize_leave_original_pos_2)
{
    struct geometry_edit_ctx_t ctx;
    kv_geometry_ctx_init_append (kv, &ctx);

    kv_new_row_h (&ctx, 1);
    struct sgmt_t *m1 = kv_add_key (&ctx, KEY_1);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, m1);
    kv_add_key_full (&ctx, KEY_A, 1, 1);
    kv_add_key_full (&ctx, KEY_B, 1, 1);
    kv_add_key_full (&ctx, KEY_C, 1, 1);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, m1);

    kv_end_geometry (&ctx);
}

BUILD_GEOMETRY_FUNC(edge_resize_test_geometry_1)
{
    struct geometry_edit_ctx_t ctx;
    kv_geometry_ctx_init_append (kv, &ctx);

    kv_new_row_h (&ctx, 1);
    struct sgmt_t *l = kv_add_key_full (&ctx, KEY_L, 1, 0);
    struct sgmt_t *m1 = kv_add_key_full (&ctx, KEY_1, 1, 1);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, l);
    struct sgmt_t *m2 = kv_add_key_full (&ctx, KEY_2, 1, 2.5);
    kv_add_multirow_sgmt (&ctx, m1);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, l);
    kv_add_multirow_sgmt (&ctx, m2);
    kv_add_multirow_sgmt (&ctx, m1);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, l);
    kv_add_multirow_sized_sgmt (&ctx, m1, 4, MULTIROW_ALIGN_RIGHT);

    kv_end_geometry (&ctx);
}

BUILD_GEOMETRY_FUNC(edge_resize_test_geometry_2)
{
    struct geometry_edit_ctx_t ctx;
    kv_geometry_ctx_init_append (kv, &ctx);

    kv_new_row_h (&ctx, 1);
    struct sgmt_t *m1 = kv_add_key_full (&ctx, KEY_1, 1, 1);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, m1);
    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, m1);

    kv_new_row_h (&ctx, 1);
    struct sgmt_t *m2 = kv_add_key_full (&ctx, KEY_2, 1, 0);
    kv_add_multirow_sgmt (&ctx, m1);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, m2);
    kv_add_multirow_sgmt (&ctx, m1);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, m2);
    kv_add_multirow_sgmt (&ctx, m1);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, m1);
    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, m1);
    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, m1);

    kv_end_geometry (&ctx);
}

BUILD_GEOMETRY_FUNC(edge_resize_test_geometry_3)
{
    struct geometry_edit_ctx_t ctx;
    kv_geometry_ctx_init_append (kv, &ctx);

    kv_new_row_h (&ctx, 1);
    struct sgmt_t *e = kv_add_key (&ctx, KEY_1);
    struct sgmt_t *k1 = kv_add_key_full (&ctx, KEY_A, 3, 2);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sized_sgmt (&ctx, e, 3, MULTIROW_ALIGN_LEFT);
    kv_add_multirow_sized_sgmt (&ctx, k1, 1, MULTIROW_ALIGN_RIGHT);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, e);
    kv_add_multirow_sized_sgmt (&ctx, k1, 2, MULTIROW_ALIGN_RIGHT);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, e);
    kv_add_multirow_sgmt (&ctx, k1);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, e);
    struct sgmt_t *k2 = kv_add_key_full (&ctx, KEY_B, 1, 2);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, e);
    kv_add_multirow_sized_sgmt (&ctx, k2, 2, MULTIROW_ALIGN_RIGHT);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, e);
    kv_add_multirow_sgmt (&ctx, k2);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sized_sgmt (&ctx, e, 1, MULTIROW_ALIGN_LEFT);
    kv_add_multirow_sized_sgmt (&ctx, k2, 3, MULTIROW_ALIGN_RIGHT);

    kv_end_geometry (&ctx);
}

BUILD_GEOMETRY_FUNC(adjust_left_edge_test_geometry)
{
    struct geometry_edit_ctx_t ctx;
    kv_geometry_ctx_init_append (kv, &ctx);

    kv_new_row_h (&ctx, 1);
    struct sgmt_t *m1 = kv_add_key_full (&ctx, KEY_1, 1, 0);

    kv_new_row_h (&ctx, 1);
    struct sgmt_t *m2 = kv_add_key_full (&ctx, KEY_2, 1, 1);
    kv_add_multirow_sgmt (&ctx, m1);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, m2);
    kv_add_multirow_sgmt (&ctx, m1);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sized_sgmt (&ctx, m1, 4, MULTIROW_ALIGN_RIGHT);

    kv_end_geometry (&ctx);
}

BUILD_GEOMETRY_FUNC(vertical_extend_test_geometry)
{
    struct geometry_edit_ctx_t ctx;
    kv_geometry_ctx_init_append (kv, &ctx);

    kv_new_row_h (&ctx, 1);
    struct sgmt_t *m = kv_add_key_full (&ctx, KEY_1, 1, 0);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sized_sgmt (&ctx, m, 0.5, MULTIROW_ALIGN_LEFT);
    kv_add_key_full (&ctx, KEY_2, 1, 1);

    kv_end_geometry (&ctx);
}

set_geometry_func_t* kv_geometries[] = {
        kv_build_default_geometry, // Default
        multirow_test_geometry,
        edge_resize_leave_original_pos_1,
        edge_resize_leave_original_pos_2,
        edge_resize_test_geometry_1,
        edge_resize_test_geometry_2,
        edge_resize_test_geometry_3,
        adjust_left_edge_test_geometry,
        vertical_extend_test_geometry,
    };

// FIXME: I was unable to easily find the height of the toolbar to ignore clicks
// when setting the tool. The grid widget kv->toolbar is the size of the full
// keyboard view, the size of the tool buttons in kv_set_full_toolbar() is 1px
// who knows why. I just hardcoded an approximate value.
#define KV_TOOLBAR_HEIGHT 25 //px

void kv_update (struct keyboard_view_t *kv, enum keyboard_view_commands_t cmd, GdkEvent *e)
{
    // To avoid segfaults when e==NULL without having to check if e==NULL every
    // time, create a dmmy event that has a type that will fail all checks.
    GdkEvent null_event = {0};
    if (e == NULL) {
        null_event.type = GDK_NOTHING;
        e = &null_event;
    }

    struct sgmt_t *button_event_key = NULL, *button_event_key_clicked_sgmt = NULL,
                 **button_event_key_ptr = NULL;
    bool button_event_key_is_rectangular = false;
    GdkRectangle button_event_key_rect = {0};
    if (e->type == GDK_BUTTON_PRESS || e->type == GDK_BUTTON_RELEASE) {
        GdkEventButton *btn_e = (GdkEventButton*)e;
        if (btn_e->y < KV_TOOLBAR_HEIGHT) {
            return;
        }

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
    struct sgmt_t *key_event_key = NULL;
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
            // Cycle to next geometry if Ctrl+T was pressed
            if (e->type == GDK_KEY_PRESS) {
                GdkEventKey *event = (GdkEventKey*)e;
                if (event->state & GDK_CONTROL_MASK && event->hardware_keycode - 8 == KEY_T) {
                    kv_clear (kv);
                    kv->geometry_idx = (kv->geometry_idx+1) % ARRAY_SIZE(kv_geometries);
                    kv_geometries[kv->geometry_idx](kv);
                }
            }

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
                // @select_is_release_based
                kv->selected_key = button_event_key;
                grab_input (NULL, NULL);
                kv->state = KV_EDIT_KEYCODE_KEYPRESS;

            } else if (kv->active_tool == KV_TOOL_SPLIT_KEY &&
                       e->type == GDK_BUTTON_RELEASE && button_event_key != NULL) {
                GdkEventButton *event = (GdkEventButton*)e;
                if (event->x < button_event_key_rect.x + button_event_key_rect.width/2) {
                    kv->edit_right_edge = false;
                } else {
                    kv->edit_right_edge = true;
                }

                if (button_event_key_is_rectangular) {
                    kv->split_key = button_event_key;
                    kv->split_full_width = button_event_key->width;
                    kv->split_rect_x = button_event_key_rect.x;
                    kv->left_min_width = kv_get_min_key_width (kv);
                    kv->right_min_width = kv->left_min_width;

                    if (!is_multirow_key (button_event_key)) {
                        kv->new_key = kv_allocate_key (kv);
                        kv->new_key->type = KEY_UNASSIGNED;

                        if (kv->edit_right_edge) {
                            kv->new_key->next_sgmt = button_event_key->next_sgmt;
                            button_event_key->next_sgmt = kv->new_key;
                            kv->new_key_ptr = &button_event_key->next_sgmt;

                        } else {
                            *button_event_key_ptr = kv->new_key;
                            kv->new_key->next_sgmt = button_event_key;
                            kv->new_key_ptr = button_event_key_ptr;

                            kv->new_key->user_glue = button_event_key->user_glue;
                            button_event_key->user_glue = 0;
                        }

                    } else {
                        kv->new_key =
                            kv_create_multirow_split (kv,
                                                      button_event_key,
                                                      button_event_key, // end segment
                                                      button_event_key_ptr,
                                                      kv->edit_right_edge,
                                                      &kv->new_key_ptr);
                    }

                    kv_set_rectangular_split (kv, event->x);
                    kv_compute_glue (kv);

                    kv->state = KV_EDIT_KEY_SPLIT;

                } else {

                    float split_key_min_width, new_key_min_width;
                    new_key_min_width = kv_get_min_key_width (kv);
                    kv_locate_edge (kv, button_event_key, button_event_key_clicked_sgmt, kv->edit_right_edge,
                                    &kv->edge_start, &kv->edge_prev_sgmt, &kv->edge_end_sgmt,
                                    &split_key_min_width);

                    kv->split_rect_x = kv_get_sgmt_x_pos (kv, kv->edge_start);
                    kv->split_key = button_event_key;

                    kv->new_key =
                        kv_create_multirow_split (kv,
                                                  kv->edge_start,
                                                  kv->edge_end_sgmt,
                                                  NULL,
                                                  kv->edit_right_edge,
                                                  &kv->new_key_ptr);

                    kv->split_full_width = kv->edge_start->width;

                    if (kv->edit_right_edge) {
                        kv->left_min_width = split_key_min_width;
                        kv->right_min_width = new_key_min_width;
                    } else {
                        kv->left_min_width = new_key_min_width;
                        kv->right_min_width = split_key_min_width;
                    }

                    kv_set_non_rectangular_split (kv, event->x);

                    // TODO: Is there a cleaner way to handle the glue when
                    // doing a split on a left edge?
                    if (!kv->edit_right_edge) {
                        kv->original_user_glue = button_event_key->user_glue;
                        float internal_glue = kv->edge_start->internal_glue;

                        kv_compute_glue (kv);

                        kv->new_key->user_glue =
                            kv->original_user_glue + internal_glue - kv->new_key->internal_glue;
                        button_event_key->user_glue = 0;
                    }
                    kv_compute_glue (kv);

                    kv->state = KV_EDIT_KEY_SPLIT_NON_RECTANGULAR;
                }


            } else if (kv->active_tool == KV_TOOL_DELETE_KEY &&
                       e->type == GDK_BUTTON_RELEASE && button_event_key != NULL) {
                kv_remove_key (kv, button_event_key_ptr);
                kv_remove_empty_rows (kv);
                kv_compute_glue (kv);
                kv_equalize_left_edge (kv);

            } else if (kv->active_tool == KV_TOOL_RESIZE_KEY &&
                       e->type == GDK_BUTTON_RELEASE && button_event_key != NULL) {

                GdkEventButton *event = (GdkEventButton*)e;
                if (event->x < button_event_key_rect.x + button_event_key_rect.width/2) {
                    kv->edit_right_edge = false;
                } else {
                    kv->edit_right_edge = true;
                }

                if (button_event_key_is_rectangular) {
                    if (is_multirow_key(button_event_key)) {
                        kv->edge_start = kv_get_multirow_parent(button_event_key);

                    } else {
                        kv->edge_start = button_event_key;
                    }

                    kv->edge_prev_sgmt = NULL;
                    kv->edge_end_sgmt = button_event_key;
                    kv->min_width = kv_get_min_key_width(kv);

                } else {
                    kv_locate_edge (kv, button_event_key, button_event_key_clicked_sgmt, kv->edit_right_edge,
                                    &kv->edge_start, &kv->edge_prev_sgmt, &kv->edge_end_sgmt, &kv->min_width);
                }

                save_edge_glue (&kv->resize_pool,
                                kv->edge_start, kv->edge_end_sgmt, kv->edit_right_edge,
                                &kv->edge_glue, &kv->edge_glue_len);

                kv->do_glue_adjust = kv_key_has_glue (kv, kv->edit_right_edge, kv->edge_start);
                kv->original_size = kv->edge_start->width;
                kv->clicked_pos = event->x;
                kv->state = KV_EDIT_KEY_RESIZE;

            } else if (kv->active_tool == KV_TOOL_RESIZE_SEGMENT &&
                       e->type == GDK_BUTTON_RELEASE && button_event_key != NULL) {
                GdkEventButton *event = (GdkEventButton*)e;
                double x;
                struct sgmt_t *sgmt;
                kv_locate_sgmt (kv, event->x, event->y, &sgmt, NULL, NULL, &x, NULL, NULL, NULL);
                if (event->x < x + get_sgmt_width (sgmt)*kv->default_key_size/2) {
                    kv->edit_right_edge = false;
                } else {
                    kv->edit_right_edge = true;
                }

                kv->resized_segment_prev = NULL;
                {
                    struct sgmt_t *curr_key = button_event_key;
                    while (curr_key != button_event_key_clicked_sgmt) {
                        kv->resized_segment_prev = curr_key;
                        curr_key = curr_key->next_multirow;
                    }
                }

                kv->resized_segment = button_event_key_clicked_sgmt;
                kv->original_size = get_sgmt_width (button_event_key_clicked_sgmt);
                kv->resized_segment->width = kv->original_size;
                kv->do_glue_adjust = kv_key_has_glue (kv, kv->edit_right_edge, kv->resized_segment);
                struct sgmt_t *end_sgmt = kv->resized_segment->next_multirow;
                if (end_sgmt->type == KEY_MULTIROW_SEGMENT) {
                    end_sgmt->width = kv->original_size;
                }

                // TODO: Some segment edges can't be resized because currently
                // non rectangular multirow key shapes are limited to aligning
                // segments either left or right. If arbitrary alignment is ever
                // implemented (@arbitrary_align) then when invalid_edge == true
                // we should use that new alignment.
                //
                // If some time passes and arbitrary alignment isn't
                // implemented, then we should notify the user about this
                // limitation in some way other than a printf(). Changing the
                // pointer to something like  ⃠ should work.
                bool invalid_edge = false;
                if (!button_event_key_is_rectangular) {
                    enum multirow_key_align_t
                        test_align = kv->edit_right_edge ? MULTIROW_ALIGN_RIGHT : MULTIROW_ALIGN_LEFT;

                    if (sgmt_check_align(kv->resized_segment, test_align) ||
                        (!is_multirow_parent(end_sgmt) && sgmt_check_align(end_sgmt, test_align))) {
                        printf ("Can't edit this segment's edge\n");
                        invalid_edge = true;
                    }
                }
                if (!invalid_edge) {
                    kv->state = KV_EDIT_KEY_RESIZE_SEGMENT;
                }

                kv->min_width = kv_get_min_key_width(kv);
                kv->clicked_pos = event->x;
                kv->resized_segment_row = kv_get_row (kv, kv->resized_segment);

                // Store original user glue
                {
                    struct sgmt_t *glue_key = get_glue_key (kv->edit_right_edge, kv->resized_segment);

                    if (glue_key != NULL) {
                        kv->resized_segment_original_user_glue = get_sgmt_user_glue(glue_key);
                        kv->resized_segment_original_glue =
                            kv->resized_segment_original_user_glue + glue_key->internal_glue;
                    }
                    kv->resized_segment_glue_plus_w = kv->original_size + kv->resized_segment_original_glue;
                }

            } else if (kv->active_tool == KV_TOOL_RESIZE_ROW &&
                       e->type == GDK_BUTTON_RELEASE && button_event_key != NULL) {
                GdkEventButton *event = (GdkEventButton*)e;
                double y;
                struct row_t *row;
                kv_locate_sgmt (kv, event->x, event->y, NULL, &row, NULL, NULL, &y, NULL, NULL);
                if (event->y < y + row->height*kv->default_key_size/2) {
                    kv->resize_row_top = true;
                } else {
                    kv->resize_row_top = false;
                }
                kv->resized_row = row;
                kv->original_size = row->height;
                kv->clicked_pos = event->y;
                kv->state = KV_EDIT_KEY_RESIZE_ROW;

            } else if (kv->active_tool == KV_TOOL_VERTICAL_EXTEND &&
                     e->type == GDK_BUTTON_RELEASE && button_event_key != NULL) {
                // TODO: This code could use some cleanup!
                struct row_t *clicked_row;
                struct sgmt_t *clicked_sgmt;
                double left_margin, y;
                GdkEventButton *event = (GdkEventButton*)e;
                kv_locate_sgmt (kv, event->x, event->y, &clicked_sgmt, &clicked_row,
                                NULL, NULL, &y, &left_margin, NULL);

                bool top;
                struct row_t *row;
                struct sgmt_t *sgmt, *prev_multirow;
                kv_locate_vedge (kv, clicked_sgmt, clicked_row, event->y, y, &sgmt, &prev_multirow, &row, &top);

                // Set _row_ to the row that will contain the new segment.
                row = top ? kv_get_prev_row (kv, row) : row->next_row;
                enum locate_sgmt_status_t new_sgmt_pos = top ? LOCATE_OUTSIDE_TOP : LOCATE_OUTSIDE_BOTTOM;
                if (row != NULL) { new_sgmt_pos = LOCATE_HIT_KEY; }

                float new_sgmt_glue = 0;
                float x_last = kv_get_sgmt_x_pos (kv, sgmt);
                struct sgmt_t **new_sgmt_ptr = NULL;
                if (new_sgmt_pos == LOCATE_HIT_KEY) {
                    // Set new_sgmt_ptr to the location of the pointer such that
                    // **new_sgmt_ptr is the first segment in _row_ whose right
                    // edge is beyond the left edge of sgmt. The new segment
                    // will tentatively be inserted before **new_sgmt_ptr.
                    new_sgmt_ptr = &row->first_key;
                    float x = left_margin;
                    while (*new_sgmt_ptr != NULL) {
                        float w = get_sgmt_total_glue(*new_sgmt_ptr) + get_sgmt_width (*new_sgmt_ptr);
                        x += w*kv->default_key_size;

                        if (x > x_last) break;

                        new_sgmt_ptr = &(*new_sgmt_ptr)->next_sgmt;
                    }

                    // In the case where **new_sgmt_ptr is a multirow key that
                    // extends to the left of sgmt, we move new_sgmt_ptr to the
                    // next segment.
                    if (*new_sgmt_ptr && is_multirow_key(*new_sgmt_ptr)) {
                        struct sgmt_t *curr_key =
                            top ? (*new_sgmt_ptr)->next_multirow : kv_get_prev_multirow (*new_sgmt_ptr);
                        while (curr_key != NULL) {
                            if (curr_key == sgmt) {
                                new_sgmt_ptr = &(*new_sgmt_ptr)->next_sgmt;
                                break;
                            }
                            curr_key = curr_key->next_sgmt;
                        }
                    }

                    float x_prev = 0;
                    struct sgmt_t *curr_sgmt = row->first_key;
                    while (curr_sgmt != *new_sgmt_ptr) {
                        x_prev += get_sgmt_total_glue(curr_sgmt) + get_sgmt_width (curr_sgmt);
                        curr_sgmt = curr_sgmt->next_sgmt;
                    }

                    new_sgmt_glue = MAX (0, bin_floor ((x_last - left_margin)/kv->default_key_size - x_prev, 3));
                }

                struct sgmt_t *new_sgmt = kv_insert_new_sgmt (kv, new_sgmt_pos, new_sgmt_ptr);
                struct sgmt_t *new_sgmt_prev;
                if (top) {
                    kv->keys_by_kc[sgmt->kc] = new_sgmt;
                    new_sgmt_prev = prev_multirow;
                    new_sgmt->width = sgmt->width;
                    new_sgmt->kc = sgmt->kc;
                    new_sgmt->type = sgmt->type;
                    sgmt->type = KEY_MULTIROW_SEGMENT;
                } else {
                    new_sgmt_prev = sgmt;
                    new_sgmt->type = KEY_MULTIROW_SEGMENT;
                }

                new_sgmt->next_multirow = new_sgmt_prev->next_multirow;
                new_sgmt_prev->next_multirow = new_sgmt;
                kv_adjust_sgmt_glue (kv, new_sgmt->next_sgmt, -get_sgmt_width(sgmt));
                kv_compute_glue (kv);

                // Update the user glue of sgmt to keep it in place
                if (new_sgmt_pos == LOCATE_OUTSIDE_TOP) {
                    new_sgmt->user_glue = sgmt->user_glue;
                } else if (new_sgmt_pos == LOCATE_HIT_KEY) {
                    // We call kv_compute_glue() twice because we implicitly changed
                    // sgmt's user glue to 0 and we need the correct internal glue
                    // to be able to now set it to new_sgmt_glue.
                    kv_adjust_sgmt_glue (kv, new_sgmt, new_sgmt_glue);
                    kv_compute_glue (kv);
                }

            } else if (kv->active_tool == KV_TOOL_VERTICAL_SHRINK &&
                     e->type == GDK_BUTTON_RELEASE && button_event_key != NULL) {
                double y;
                struct sgmt_t *clicked_sgmt;
                struct row_t *clicked_row;
                GdkEventButton *event = (GdkEventButton*)e;
                kv_locate_sgmt (kv, event->x, event->y, &clicked_sgmt, &clicked_row, NULL, NULL, &y, NULL, NULL);

                bool top;
                struct row_t *row;
                struct sgmt_t *sgmt, *prev_multirow;
                kv_locate_vedge (kv, clicked_sgmt, clicked_row, event->y, y, &sgmt, &prev_multirow, &row, &top);

                // If the segment being removed was a supporting segment, then
                // we must compute the new user glue as the minimum total glue
                // of the remaining segments.
                float new_user_glue = INFINITY;
                if (is_supporting_sgmt (sgmt)) {
                    struct sgmt_t *curr_sgmt = sgmt->next_multirow;
                    do {
                        new_user_glue = MIN (new_user_glue, get_sgmt_total_glue (curr_sgmt));

                        curr_sgmt = curr_sgmt->next_multirow;
                    } while (curr_sgmt != sgmt);

                } else {
                    new_user_glue = sgmt->user_glue;
                }

                if (top) {
                    kv->keys_by_kc[sgmt->kc] = sgmt->next_multirow;
                    if (sgmt->next_multirow->type != KEY_MULTIROW_SEGMENT_SIZED) {
                        sgmt->next_multirow->width = sgmt->width;
                    }
                    sgmt->next_multirow->type = sgmt->type;
                    sgmt->next_multirow->kc = sgmt->kc;
                    sgmt->next_multirow->user_glue = new_user_glue;

                } else {
                    sgmt->user_glue = new_user_glue;
                }

                if (sgmt->next_sgmt != NULL && get_sgmt_total_glue (sgmt->next_sgmt) != 0) {
                    kv_adjust_sgmt_glue (kv, sgmt->next_sgmt, get_sgmt_width (sgmt) + get_sgmt_total_glue(sgmt));
                }

                if (is_multirow_key (sgmt)) {
                    kv_unlink_multirow_sgmt (sgmt, prev_multirow);
                    if (!is_multirow_key (prev_multirow)) {
                        // If the key became single row, then update its internal glue
                        // as kv_compute_glue() will ignore it, and expect it to be 0.
                        prev_multirow->internal_glue = 0;
                    }

                } else {
                    kv->keys_by_kc[sgmt->kc] = NULL;
                }

                kv_remove_key_sgmt (kv, kv_get_sgmt_ptr (row, sgmt), row, NULL);
                kv_remove_empty_rows (kv);
                kv_compute_glue (kv);
                kv_equalize_left_edge (kv);


            } else if (kv->active_tool == KV_TOOL_ADD_KEY &&
                       e->type == GDK_MOTION_NOTIFY) {
                GdkEventMotion *event = (GdkEventMotion*)e;
                kv_set_add_key_state (kv, event->x, event->y);

                // Hide the add key rectangle if the pointer is in the toolbar
                kv->to_add_rect_hidden = event->y < KV_TOOLBAR_HEIGHT;

            } else if (kv->active_tool == KV_TOOL_ADD_KEY &&
                       e->type == GDK_BUTTON_RELEASE) {
                float glue_adj, new_glue;
                if (kv->added_key_user_glue < 0) {
                    kv_adjust_left_edge (kv, NULL, -kv->added_key_user_glue);
                    kv_compute_glue (kv);
                    glue_adj = -1;
                    new_glue = 0;
                } else {
                    glue_adj = -(kv->added_key_user_glue + 1);
                    new_glue = kv->added_key_user_glue;
                }

                struct sgmt_t *new_key =
                    kv_insert_new_sgmt (kv, kv->locate_stat, kv->added_key_ptr);
                new_key->user_glue = new_glue;
                kv_adjust_sgmt_glue (kv, new_key->next_sgmt, glue_adj);

                kv_compute_glue (kv);

                GdkEventButton *event = (GdkEventButton*)e;
                kv_set_add_key_state (kv, event->x, event->y);

            } else if (kv->active_tool == KV_TOOL_PUSH_RIGHT &&
                       e->type == GDK_BUTTON_RELEASE && button_event_key != NULL) {
                kv->clicked_pos = ((GdkEventButton*)e)->x;
                kv->push_right_key = button_event_key;
                kv->original_size = get_sgmt_user_glue (button_event_key);
                kv->state = KV_EDIT_KEY_PUSH_RIGHT;
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

            } else if (e->type == GDK_BUTTON_RELEASE) { // @select_is_release_based
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
                kv_set_rectangular_split (kv, event->x);

            } else if (e->type == GDK_BUTTON_RELEASE) {
                kv->state = KV_EDIT;

            } else if (e->type == GDK_KEY_PRESS) {
                if (key_event_kc - 8 == KEY_ESC) {
                    if (!kv->edit_right_edge) {
                        kv->split_key->user_glue = kv->new_key->user_glue;
                    }

                    kv_remove_key (kv, kv->new_key_ptr);
                    kv->split_key->width = kv->split_full_width;

                    kv->state = KV_EDIT;
                    kv_compute_glue (kv);
                }

            }
            break;

        case KV_EDIT_KEY_SPLIT_NON_RECTANGULAR:
            if (e->type == GDK_MOTION_NOTIFY) {
                GdkEventMotion *event = (GdkEventMotion*)e;
                kv_set_non_rectangular_split (kv, event->x);

            } else if (e->type == GDK_BUTTON_RELEASE) {
                kv->state = KV_EDIT;

            } else if (e->type == GDK_KEY_PRESS) {
                if (key_event_kc - 8 == KEY_ESC) {
                    if (!kv->edit_right_edge) {
                        kv->split_key->user_glue = kv->original_user_glue;
                    }

                    kv_remove_key (kv, kv->new_key_ptr);
                    kv_resize_edge (kv->edge_prev_sgmt, kv->edge_start, kv->edge_end_sgmt,
                                       kv->split_full_width - kv->edge_start->width);

                    kv->state = KV_EDIT;
                    kv_compute_glue (kv);
                }

            }
            break;

        case KV_EDIT_KEY_RESIZE:
            if (e->type == GDK_MOTION_NOTIFY) {
                GdkEventMotion *event = (GdkEventMotion*)e;
                float delta = bin_floor((event->x - kv->clicked_pos)/kv->default_key_size, 3);

                float new_width = kv->edit_right_edge ?
                    kv->original_size + delta : kv->original_size - delta;
                new_width = MAX (new_width, kv->min_width);

                if (new_width != kv->edge_start->width) {
                    kv_change_edge_width (kv, kv->edge_prev_sgmt, kv->edge_start, kv->edge_end_sgmt,
                                          kv->edit_right_edge, kv->do_glue_adjust,
                                          kv->edge_glue, kv->edge_glue_len,
                                          kv->original_size, new_width);
                    kv_compute_glue (kv);
                }

            } else if (e->type == GDK_BUTTON_RELEASE) {
                kv_resize_cleanup (kv);
                kv->state = KV_EDIT;

            } else if (e->type == GDK_KEY_PRESS) {
                if (key_event_kc - 8 == KEY_ESC) {
                    kv_change_edge_width (kv, kv->edge_prev_sgmt, kv->edge_start, kv->edge_end_sgmt,
                                          kv->edit_right_edge, kv->do_glue_adjust,
                                          kv->edge_glue, kv->edge_glue_len,
                                          kv->original_size, kv->original_size);

                    kv_compute_glue (kv);
                    kv_resize_cleanup (kv);
                    kv->state = KV_EDIT;
                }
            }

            break;

        case KV_EDIT_KEY_RESIZE_SEGMENT:
            if (e->type == GDK_MOTION_NOTIFY) {
                GdkEventMotion *event = (GdkEventMotion*)e;
                float delta = bin_floor((event->x - kv->clicked_pos)/kv->default_key_size, 3);

                float new_width = kv->edit_right_edge ?
                    kv->original_size + delta : kv->original_size - delta;
                new_width = MAX(new_width, kv->min_width);

                if (new_width != kv->resized_segment->width) {
                    kv_change_sgmt_width (kv, kv->resized_segment_prev, kv->resized_segment,
                                          kv->edit_right_edge, kv->do_glue_adjust,
                                          kv->resized_segment_row, kv->resized_segment_glue_plus_w,
                                          kv->resized_segment_original_glue, new_width);
                    kv_compute_glue (kv);
                }

            } else if (e->type == GDK_BUTTON_RELEASE) {
                kv->state = KV_EDIT;

            } else if (e->type == GDK_KEY_PRESS) {
                if (key_event_kc - 8 == KEY_ESC) {
                    kv_change_sgmt_width (kv, kv->resized_segment_prev, kv->resized_segment,
                                          kv->edit_right_edge, kv->do_glue_adjust,
                                          kv->resized_segment_row, kv->resized_segment_glue_plus_w,
                                          kv->resized_segment_original_glue, kv->original_size);

                    kv_compute_glue (kv);
                    kv->state = KV_EDIT;
                }
            }
            break;

        case KV_EDIT_KEY_RESIZE_ROW:
            if (e->type == GDK_MOTION_NOTIFY) {
                GdkEventMotion *event = (GdkEventMotion*)e;
                float delta = bin_floor((event->y - kv->clicked_pos)/kv->default_key_size, 3);

                float new_height = kv->resize_row_top ?
                    kv->original_size - delta : kv->original_size + delta;
                new_height = MAX(new_height, kv_get_min_key_width(kv));

                if (new_height != kv->resized_row->height) {
                    kv->resized_row->height = new_height;
                }

            } else if (e->type == GDK_BUTTON_RELEASE) {
                kv->state = KV_EDIT;

            } else if (e->type == GDK_KEY_PRESS) {
                if (key_event_kc - 8 == KEY_ESC) {
                    kv->resized_row->height = kv->original_size;
                    kv->state = KV_EDIT;
                }
            }
            break;

        case KV_EDIT_KEY_PUSH_RIGHT:
            if (e->type == GDK_MOTION_NOTIFY) {
                GdkEventMotion *event = (GdkEventMotion*)e;
                float delta = bin_floor((event->x - kv->clicked_pos)/kv->default_key_size, 3);
                float new_glue = MAX (0, kv->original_size + delta);

                if (kv->push_right_key->user_glue != new_glue) {
                    kv->push_right_key->user_glue = new_glue;
                    kv_compute_glue (kv);
                    kv_equalize_left_edge (kv);
                }

            } else if (e->type == GDK_BUTTON_RELEASE) {
                kv->state = KV_EDIT;

            } else if (e->type == GDK_KEY_PRESS) {
                if (key_event_kc - 8 == KEY_ESC) {
                    kv->push_right_key->user_glue = kv->original_size;
                    kv->state = KV_EDIT;
                    kv_compute_glue (kv);
                }
            }
            break;
    }

    gtk_widget_queue_draw (kv->widget);
}

gboolean key_press_handler (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    kv_update (app.keyboard_view, KV_CMD_NONE, event);
    return TRUE;
}

gboolean key_release_handler (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    kv_update (app.keyboard_view, KV_CMD_NONE, event);
    return TRUE;
}

gboolean kv_motion_notify (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    kv_update (app.keyboard_view, KV_CMD_NONE, event);
    return TRUE;
}

gboolean kv_button_press (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    GdkEventButton *button_event = (GdkEventButton*)event;
    if (button_event->type == GDK_2BUTTON_PRESS || button_event->type == GDK_3BUTTON_PRESS) {
        // Ignore double and triple clicks.
        return FALSE;
    }

    kv_update (app.keyboard_view, KV_CMD_NONE, event);
    return TRUE;
}

gboolean kv_button_release (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    kv_update (app.keyboard_view, KV_CMD_NONE, event);
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
    struct sgmt_t *key = keyboard_view_get_key (app.keyboard_view, x, y, &rect, NULL, NULL, NULL);
    if (key != NULL) {
        // For non rectangular multirow keys keyboard_view_get_key() returns the
        // rectangle of the segment that was hovered. This has the (undesired?)
        // effect of making the tooltip jump while moving the mouse over the
        // same key but across different segments. Because the tooltip area can
        // only be a rectangle, the only other option would be to set it to the
        // bounding box, which would cause the tooltip to not jump even when
        // changing across different keys (although the text inside would change
        // appropiately).
        if (app.keyboard_view->label_mode == KV_KEYCODE_LABELS) {
            gtk_tooltip_set_text (tooltip, keycode_names[key->kc]);

        } else { // KV_KEYSYM_LABELS
            char buff[64];
            xkb_keysym_t keysym = xkb_state_key_get_one_sym(app.keyboard_view->xkb_state, key->kc + 8);
            xkb_keysym_get_name(keysym, buff, ARRAY_SIZE(buff)-1);
            gtk_tooltip_set_text (tooltip, buff);
        }

        gtk_tooltip_set_tip_area (tooltip, &rect);
        show_tooltip = TRUE;

    } else {
        struct manual_tooltip_t *curr_tooltip = app.keyboard_view->first_tooltip;
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

// NOTE: The caller of keyboard_view_new() is responsible of calling
// keyboard_view_destroy() when the view is no longer needed.
struct keyboard_view_t* keyboard_view_new (GtkWidget *window)
{
    mem_pool_t *pool;
    {
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
        gtk_widget_set_has_tooltip (kv_widget, TRUE);

        // FIXME: Tooltips for children of a GtkOverlay appear to be broken (or
        // I was unable set them up properly). Only one query-tooltip signal is
        // sent to the overlay. Even if a children of the overlay has a tooltip,
        // it never receives the query-tooltip signal. It's as if tooltip
        // "events" don't trickle down to children.
        //
        // To work around this, we manually add tooltips for buttons in the
        // toolbar. Then the correct tooltip is chosen in the handler for the
        // query-tooltip signal, for the overlay.
        //
        // UPDATE (November 29, 2018):
        // I tried to fix this and although GtkOverlay lets events reach its
        // children by using gtk_overlay_set_overlay_pass_through(), it's broken
        // for the case of buttons inside overlays, so this code still seems to
        // be the best workaround. To see how the clean code should look, and an
        // attempt at working around this found Gtk bug (it was reported in
        // 2016), look at the overlay-tooltips-fix branch.
        //
        // Sigh, maybe just create a fk_tooltip() API...
        //
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

    kv->default_key_size = 56; // Should be divisible by 4 so everything is pixel perfect
    kv->geometry_idx = 0;
    kv_geometries[kv->geometry_idx](kv);

    kv->pool = pool;
    kv->state = KV_PREVIEW;
    kv_update (kv, KV_CMD_SET_MODE_EDIT, NULL);

    return kv;
}

void keyboard_view_destroy (struct keyboard_view_t *kv)
{
    mem_pool_destroy (&kv->keyboard_pool);
    mem_pool_destroy (&kv->tooltips_pool);
    mem_pool_destroy (&kv->resize_pool);
    mem_pool_destroy (kv->pool);
}

