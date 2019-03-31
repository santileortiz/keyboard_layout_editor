/*
 * Copiright (C) 2019 Santiago León O.
 */

// This file contains our internal representation for a keymap, this technically
// duplicates the code from <libxkbcommon>/src/keymap.h. I'm not just copying
// that file here because it's much more complex than what we need at the
// moment. For now I want to start small, and add features incrementally
// depending on how the UI evolves. I don't want to add a dependency on an file
// people isn't supposed to depend on (yet).

// TODO: XKB seems to support an arbitrary number of levels, in practice xkbcomp
// seems to complain if there are more than 8 levels. Maybe remove this in the
// future?. Some considerations to have:
//  - Removing this limit would mean any identifier matching regex
//    [lL][eE][vV][eE][lL][0-9]+ will be invalid. Currently used virtual modifier
//    identifiers don't match this, but LevelThree is awfully close to matching.
//    :level_identifiers
//  - Letting people choose an absurdly large number of levels could make us
//    allocate a lot of unnecessary memory.
//  - Maybe just leave it like this and bump it if necessary.
//
//  Update (Mar 30, 2019): It looks like xkbcomp does accept an arbitrary number
//  of levels by using number identifiers instead of numbers prefixed by
//  'level', this means we can stick to that syntax and remove this limit.
//  TODO: Make our xkb parser understand this syntax, then use this syntax in
//  our xkb file writer.
//  :level_limit
//
#define KEYBOARD_LAYOUT_MAX_LEVELS 8

// This has similar considerations than level identifiers, but unlike level
// identifiers that we actually need, I don't really want to support multiple
// groups unless it's necessary. This constant is used just to parse the
// identifiers and ignore everything in a group different than the first one.
#define KEYBOARD_LAYOUT_MAX_GROUPS 4

#define KEYBOARD_LAYOUT_MAX_MODIFIERS 16

typedef uint32_t key_modifier_mask_t;

struct key_type_t {
    char *name;
    int num_levels;
    key_modifier_mask_t modifier_mask;
    key_modifier_mask_t modifiers[KEYBOARD_LAYOUT_MAX_LEVELS];

    struct key_type_t *next;
};

enum action_type_t {
    KEY_ACTION_TYPE_NONE = 0,
    KEY_ACTION_TYPE_MOD_SET,
    KEY_ACTION_TYPE_MOD_LATCH,
    KEY_ACTION_TYPE_MOD_LOCK
};

struct key_action_t {
    enum action_type_t type;
    key_modifier_mask_t modifiers;
};

struct key_level_t {
    // Keysym encoding can be very confusing. Here we will use keysyms as
    // defined in Appendix A of "X11 Window System Protocol". Code wise, macro
    // definitions for these can come from several places, we will use the ones
    // found in <libxkbcommon>/xkbcommon/xkbcommon-keysyms.h, these are prefixed
    // by XKB_KEY_ but they are automatically generated from Xlib's macros that
    // are prefixed with XK_, as far as I can tell they are interchangeable.
    //
    // Encoding of keysyms is complex because it has to define values for keys
    // that don't have Unicode representation (like arrow keys, volume keys, F1,
    // Del, etc.). Also, X11's protocol specification comes from a pre-unicode
    // era, which made some legacy encodings to be included as blocks in X11's
    // keysym encoding, for backwards compatibility reasons these are still
    // available in the macro definitions.
    //
    // In broad terms, X11's specification encodes all non control unicode
    // codepoints from U+0000 to U+10FFFF. Instead of unicode control characters
    // X11 defines a block of "function keysyms" which include keys not
    // available in Unicode, like Alt, Control, Shift, arrows, hiragana and
    // katakana toggling among others. Wether control unicode characters can be
    // assigned to a key or not is entirely dependent on the implementation of
    // the X11 protocol, I think libxkbcommon "may" allow them but I haven't
    // tested this. Also, as a caveat not all keys we may consider function keys
    // are in X11's "function keysyms" block, some of them (like volume control
    // keysyms) are in the "vendor keysyms" block, for legacy reasons I guess.
    xkb_keysym_t keysym;

    struct key_action_t action;
};

struct key_t {
    int kc;
    struct key_type_t *type;
    struct key_level_t levels[KEYBOARD_LAYOUT_MAX_LEVELS];
};

struct keyboard_layout_t {
    mem_pool_t pool;

    struct key_type_t *types;
    struct key_t *keys[KEY_CNT];

    // Map from modifier names to modifier masks
    GTree *modifiers;
};

enum modifier_result_status_t {
    KEYBOARD_LAYOUT_MOD_SUCCESS,
    KEYBOARD_LAYOUT_MOD_REDEFINITION,
    KEYBOARD_LAYOUT_MOD_MAX_LIMIT_REACHED,
    KEYBOARD_LAYOUT_MOD_UNDEFINED
};

key_modifier_mask_t keyboard_layout_new_modifier (struct keyboard_layout_t *keymap,
                                                  char *name, enum modifier_result_status_t *status)
{
    key_modifier_mask_t result = 0;
    enum modifier_result_status_t status_l;

    int num_modifiers = 0;
    if (keymap->modifiers == NULL) {
        keymap->modifiers = g_tree_new (strcasecmp_as_g_compare_func);
    } else {
        num_modifiers = g_tree_nnodes(keymap->modifiers);
    }

    if (num_modifiers < KEYBOARD_LAYOUT_MAX_MODIFIERS) {
        if (!g_tree_lookup_extended (keymap->modifiers, name, NULL, NULL)) {
            // FIXME: Sigh, I don't like this wrapping of values inside GTree,
            // it will make us leak memory when we add a way to delete
            // modifiers. Not leaking memory would require us keeping a linked
            // list of spare key_modifier_mask_t which seems overkill. What I
            // would REALLY like is have my own tree implementation that uses
            // macros and a pool. It should internally handle this spare node
            // linked list, and allow us to define an implementation that stores
            // the key_modifier_mask_t value itself, not a pointer to one.
            key_modifier_mask_t *value_ptr = mem_pool_push_size (&keymap->pool, sizeof(key_modifier_mask_t));
            *value_ptr = 1 << num_modifiers;
            char *stored_modifier_name = pom_strdup (&keymap->pool, name);
            g_tree_insert (keymap->modifiers, stored_modifier_name, value_ptr);
            result = *value_ptr;

            status_l = KEYBOARD_LAYOUT_MOD_SUCCESS;

        } else {
            status_l = KEYBOARD_LAYOUT_MOD_REDEFINITION;
        }

    } else {
        status_l = KEYBOARD_LAYOUT_MOD_MAX_LIMIT_REACHED;
    }

    if (status != NULL) {
        *status = status_l;
    }

    return result;
}

// NOTE: The return value will be 0 if name is 'none'. It's expected that 0 will
// be a valid modifier everywhere,representing no modifier.
key_modifier_mask_t keyboard_layout_get_modifier (struct keyboard_layout_t *keymap,
                                                  char *name, enum modifier_result_status_t *status)
{
    key_modifier_mask_t result = 0;
    enum modifier_result_status_t status_l = KEYBOARD_LAYOUT_MOD_UNDEFINED;

    void *value;
    if (g_tree_lookup_extended (keymap->modifiers, name, NULL, &value)) {
        status_l = KEYBOARD_LAYOUT_MOD_SUCCESS;
        result = *(key_modifier_mask_t*)value;

    } else if (strcasecmp (name, "none") == 0) {
        status_l = KEYBOARD_LAYOUT_MOD_SUCCESS;
        result = 0;
    }

    if (status != NULL) {
        *status = status_l;
    }

    return result;
}

struct key_type_t* keyboard_layout_new_type (struct keyboard_layout_t *keymap,
                                             char *name, key_modifier_mask_t modifier_mask)
{
    struct key_type_t *new_type = mem_pool_push_size (&keymap->pool, sizeof(struct key_type_t));
    *new_type = ZERO_INIT (struct key_type_t);

    // TODO: For now, types will not be destroyed, if they do, the name will
    // leak space inside the keymap's pool.
    new_type->name = pom_strdup (&keymap->pool, name);
    new_type->modifier_mask = modifier_mask;

    new_type->next = keymap->types;
    keymap->types = new_type;

    return new_type;
}

// NOTE: May return NULL if the name does not exist.
struct key_type_t* keyboard_layout_type_lookup (struct keyboard_layout_t *keymap, char *name)
{
    // TODO: Linear search for now. Is this really SO bad that we should use a
    // tree or a hash table?
    struct key_type_t *curr_type = keymap->types;
    while (curr_type != NULL) {
        if (strcmp (curr_type->name, name) == 0) break;
        curr_type = curr_type->next;
    }

    return curr_type;
}

void keyboard_layout_type_set_level (struct key_type_t *type, int level, key_modifier_mask_t modifiers)
{
    assert (level > 0 && "Levels must be grater than 0");

    type->modifiers[level-1] = modifiers;
    type->num_levels = MAX (type->num_levels, level);
}

struct key_t* keyboard_layout_new_key (struct keyboard_layout_t *keymap, int kc, struct key_type_t *type)
{
    struct key_t *key = keymap->keys[kc];
    if (key == NULL) {
        key = mem_pool_push_size (&keymap->pool, sizeof (struct key_t));
        *key = ZERO_INIT (struct key_t);
        key->kc = kc;
        keymap->keys[kc] = key;
    }

    key->type = type;

    return key;
}

void keyboard_layout_key_set_level (struct key_t *key, int level, xkb_keysym_t keysym, struct key_action_t *action)
{
    assert (level > 0 && "Levels must be grater than 0");

    struct key_level_t *lvl = &key->levels[level-1];
    lvl->keysym = keysym;

    if (action != NULL) {
        lvl->action = *action;
    }
}

void keyboard_layout_destroy (struct keyboard_layout_t *keymap)
{
    mem_pool_destroy (&keymap->pool);
    // TODO: We should add a way of adding single callbacks to a pool.
    g_tree_destroy (keymap->modifiers);
}

struct keyboard_layout_t* keyboard_layout_new_default (void)
{
    mem_pool_t bootstrap = ZERO_INIT (mem_pool_t);
    struct keyboard_layout_t *keymap = mem_pool_push_size (&bootstrap, sizeof(struct keyboard_layout_t));
    *keymap = ZERO_INIT (struct keyboard_layout_t);
    keymap->pool = bootstrap;

    struct key_type_t *type_one_level;
    type_one_level = keyboard_layout_new_type (keymap, "ONE_LEVEL", 0);
    keyboard_layout_type_set_level (type_one_level, 1, 0);

    struct key_type_t *type;
    type = keyboard_layout_new_type (keymap, "TWO_LEVEL", 0);
    keyboard_layout_type_set_level (type, 1, 0);
    keyboard_layout_type_set_level (type, 2, 0);

    type = keyboard_layout_new_type (keymap, "ALPHABETIC", 0);
    keyboard_layout_type_set_level (type, 1, 0);
    keyboard_layout_type_set_level (type, 2, 0);
    keyboard_layout_type_set_level (type, 3, 0);

    struct key_t *key = keyboard_layout_new_key (keymap, KEY_ESC, type_one_level);
    keyboard_layout_key_set_level (key, 1, XKB_KEY_Escape, NULL);

    return keymap;
}

// NOTE: This can return NULL if parsing fails.
bool xkb_file_parse (char *xkb_str, struct keyboard_layout_t *keymap);
void xkb_file_write (struct keyboard_layout_t *keymap, string_t *res);
struct keyboard_layout_t* keyboard_layout_new_from_xkb (char *xkb_str)
{
    mem_pool_t bootstrap = ZERO_INIT (mem_pool_t);
    struct keyboard_layout_t *keymap = mem_pool_push_size (&bootstrap, sizeof(struct keyboard_layout_t));
    *keymap = ZERO_INIT (struct keyboard_layout_t);
    keymap->pool = bootstrap;

    if (!xkb_file_parse (xkb_str, keymap)) {
        keyboard_layout_destroy (keymap);
        keymap = NULL;

    } else {
        string_t out;
        xkb_file_write (keymap, &out);
        printf ("%s\n", str_data(&out));
    }

    return keymap;
}

// TODO: Add a function that prunes all unused components and compacts limited
// resources, for instance in xkb there can't be more than 16 modifiers. Should
// this be done in the specific backend?, or maybe let the backend set the
// resource limits and we create a single function here that takes them into
// account? (this will only be an issue in the future when we may have multiple
// backends). Right now limited resources include modifiers and levels, groups
// would be limited too but I don't want to support them unless they are
// necessary. Levels could be made unlimited if we use a linked list but
// modifiers will always have a limit as they are a bit mask.
// :keyboard_layout_compact, :level_limit

