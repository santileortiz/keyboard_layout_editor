/*
 * Copiright (C) 2019 Santiago León O.
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
// way out of the scope of the application in terms of complexity both in
// implementation and ease of use.
//
// When we talk about a key we refer to a phisical key in a keyboard. A key that
// spans multiple rows is called a multirow key. We split multirow keys in
// rectangles that are located one per row, we call these rectangles key
// segments.
//
// The main data structure is a liked list of rows (row_t), each of which
// contains a linked list of key segments (sgmt_t). The key segments of a
// multirow key are grouped with the pointers next_multirow forming a cyclic
// linked list. In multirow keys the top segment is called the "key parent" or
// "parent segment" and is the segment that represents the full key, here is
// where the keycode will be stored.
//
// Alignment between segments of the same key is done by choosing an alignment
// mode for each segment (multirow_key_align_t). Right now we align either right
// or left edges, for a more detailed discussion of the limitations of this see
// @arbitrary_align. A segment will align its alignment mode edge with the same
// edge in the previous segment. The alignment mode of the parent segment is
// meaningless.
//
// Multirow keys are rigid, so the position of a segment in a row restricts the
// position of the other segments of the key, this means there may be some blank
// space before some of the segments of a multirow key. This distance is called
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
// supporting segment(s).
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

#define KV_GRAB_NOTIFY_CB(name) void name ()
typedef KV_GRAB_NOTIFY_CB(kv_grab_change_notify_cb_t);

#define KV_SELECT_KEY_CHANGE_NOTIFY_CB(name) void name (int kc)
typedef KV_SELECT_KEY_CHANGE_NOTIFY_CB(kv_select_key_change_notify_cb_t);

enum keyboard_view_state_t {
    KV_PREVIEW,

    KV_EDIT,
    KV_EDIT_KEYCODE_KEYPRESS,
    KV_EDIT_KEYCODE_MULTIPLE_KEYPRESS,
    KV_EDIT_KEYCODE_LOOKUP,
    KV_EDIT_KEY_SPLIT,
    KV_EDIT_KEY_SPLIT_NON_RECTANGULAR,
    KV_EDIT_KEY_RESIZE,
    KV_EDIT_KEY_RESIZE_SEGMENT,
    KV_EDIT_KEY_RESIZE_ROW,
    KV_EDIT_KEY_PUSH_RIGHT
};

enum keyboard_view_commands_t {
    KV_CMD_NONE,
    KV_CMD_SET_MODE_PREVIEW,
    KV_CMD_SET_MODE_EDIT
};

enum keyboard_view_tools_t {
    KV_TOOL_KEYCODE_KEYPRESS,
    KV_TOOL_KEYCODE_KEYPRESS_MULTIPLE,
    KV_TOOL_KEYCODE_LOOKUP,
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

enum keyboard_view_preview_mode_t {
    KV_PREVIEW_TEST,
    KV_PREVIEW_KEYS
};

enum key_render_type_t {
    KEY_DEFAULT,
    KEY_PRESSED,
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
// coordinates, segment search from pointer coordinates, serialization and
// parsing of keyboard view string representations. Also, it's necessary to keep
// an invariant that forbids keys with disjoint segments.
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

// Distances inside a keyboard view are NOT in pixels, they are relative to the
// value of kv->default_key_size. This value represents the width and height in
// pixels of a "normal" key (what we would say is 1 by 1 units in size for this
// keyboard). KV_DEFAULT_KEY_SIZE is the default value for kv->default_key_size.
#define KV_DEFAULT_KEY_SIZE 56

// All tools that change a distance in the keyboard view will change it in steps
// of kv->default_key_size/(2^KV_STEP_PRECISION) pixels.
// NOTE: To ensure everything is pixel perfect this value has to be an integer,
// be careful about that!.
#define KV_STEP_PRECISION 3

// Color palette
dvec4 color_blue = RGB_HEX(0x7f7fff);
dvec4 color_red = RGB_HEX(0xe34442);
dvec4 color_green = RGB_HEX(0x90de4d);

struct keyboard_view_t {
    mem_pool_t *pool;

    // Pool where all sgmt_t and row_t structures will be allocated
    mem_pool_t keyboard_pool;

    int geometry_idx;

    // This is the state used to keep track of available layout representations.
    string_t repr_path;
    string_t settings_file_path;
    struct kv_repr_store_t *repr_store;

    // Array of sgmt_t pointers indexed by keycode. Provides fast access to keys
    // from a keycode. It's about 6KB in memory, maybe too much?
    struct sgmt_t *keys_by_kc[KEY_MAX];
    struct sgmt_t *spare_keys;
    struct row_t *spare_rows;

    // This is the head of the row linked list. It's the entry point of the
    // whole keyboard view data structure.
    struct row_t *first_row;

    // xkbcommon state
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;

    // KEYCODE_LOOKUP state
    struct fk_popover_t keycode_lookup_popover;
    struct fk_searchable_list_t keycode_lookup_ui;

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
    GtkWidget *window;
    GtkWidget *widget;
    GtkWidget *toolbar;
    GtkWidget *repr_combobox;
    GtkWidget *repr_save_button;
    GtkWidget *repr_save_as_button;
    struct fk_popover_t repr_save_as_popover;
    GtkWidget *repr_save_as_popover_entry;

    float default_key_size; // In pixels
    int clicked_kc;
    struct sgmt_t *selected_key;
    struct sgmt_t *preview_keys_selection;
    enum keyboard_view_state_t state;
    enum keyboard_view_label_mode_t label_mode;
    enum keyboard_view_preview_mode_t preview_mode;
    enum keyboard_view_tools_t active_tool;

    GdkRectangle debug_rect;

    // This pattern looks a lot like OOP, I'm not sure if it's the right thing
    // to do. The reasoning behind this is that we would like keyboard_view to
    // be a easily reusable GTK widget, with low friction to people who want to
    // use it. Because the widget grabs/ungrabs keyboard input we want to notify
    // the caller about it so they can show this to the user somehow in the UI.
    // These callbacks are a way to decouple this functionality between the
    // caller app and the keyboard view widget. We want this because we use the
    // keyboard view at least in 2 apps the keyboard editor and the keyboard
    // viewer used for testing. I think this UI couls also be used outside of
    // this project.
    kv_grab_change_notify_cb_t *grab_notify_cb;
    kv_grab_change_notify_cb_t *ungrab_notify_cb;

    // TODO: Rearchitect everything to remove the need for this callback.
    // The current API available to the caller to control the keyboard view
    // consists basically of changing the mode through 'commands'
    // (keyboard_view_commands_t). I now think this was a mistake. It makes the
    // keyboard view have basically 2 state machinse mixed together, the one
    // used for view editing commands, and the one to keep these caller states.
    // This second state machine really belongs in the caller, not inside the
    // keyboard_view. Comming up with a set of "modes" that would work in
    // general will be impossible and limit the caller somehow anyway. It also
    // makes us have callbacks like the following to notify the caller when a
    // mode does something they need to know about, this is not good. Callbacks
    // make things complicated because you don't really know WHEN this callback
    // will happen, and bugs can happen very easily if we start calling stuff in
    // the callback that may trigger another callback.
    //
    // What I want to do now, is turn everything inside out and take all this
    // state code into the caller and out of the keyboard view. This means the
    // following:
    //   - Remove keyboard_view_commands_t
    //   - Allow the caller to modify the rendered string of each key and their
    //     tooltip.
    //   - Allow the caller to set the color of individual keys.
    //   - Expose a function that takes x and y coordinates and returns the
    //     keycode of the key at that position.
    //   - Allow easy iteration thorugh all keys.
    //   - Make easy the common case where we 'move' the previously colored key.
    //
    // The reasons why this wasn't the straight forward thing to do before were:
    //   - Changing the rendering string per key implied keeping an allocated
    //     string per key struct and being able to modify it dynamically, with
    //     only pools this was not feasible. Now we have pooled string which
    //     make it easy to allocate/deallocate them.
    //   - There was no need to have multiple colors at the same time. But now
    //     to show types I do want to have multiple colors at the same time.
    //
    // As to how this implementation will look like I'm leaning towards just
    // exposing a kv_key_t struct where the user will store the data they want,
    // and have internally an array of them indexed by keycode, then when
    // rendering this data is looked up and things are rendered accordingly. I
    // had thought about adding functions for setting each thing internally, but
    // this seems more boilerplate code. This way also avoids duplicating
    // information in each segment, and confusing the caller about the concept
    // of segments.
    kv_select_key_change_notify_cb_t *selected_key_change_cb;
};

