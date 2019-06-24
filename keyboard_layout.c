/*
 * Copiright (C) 2019 Santiago León O.
 */

// This file contains our internal representation for a keymap, this technically
// duplicates the code from <libxkbcommon>/src/keymap.h. I'm not just copying
// that file here because it's much more complex than what we need at the
// moment. For now I want to start small, and add features incrementally
// depending on how the UI evolves. I don't want to add a dependency on an file
// people isn't supposed to depend on (yet).
//
// Also, because I'm trying to keep this representation simpler than the one
// from xkb, it may be multiplatform in the future.
//
// TODO: Maybe in the future there will be platform specific data that must be
// kept here so we don't break layouts, we would need to add a mechanism for
// backends to store custom data here that only they know how to write/read.
// :platform_specific_data_in_internal_representation

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

// NOTE: This is the technical maximum for the used type of key_modifier_mask_t,
// effectiveley a backend can have a smaller maximum value. For instance the xkb
// backend has a maximum of 16 modifiers.
#define KEYBOARD_LAYOUT_MAX_MODIFIERS 32

typedef uint32_t key_modifier_mask_t;

struct level_modifier_mapping_t {
    int level;
    key_modifier_mask_t modifiers;

    struct level_modifier_mapping_t *next;
};

struct key_type_t {
    char *name;
    key_modifier_mask_t modifier_mask;
    // NOTE: It's important to support multiple modifier masks to be assigned to
    // a single level, while forbidding the same modifier mask to be assigned to
    // multiple levels.
    // This linked list will be sorted in increasing value of level, and this
    // invariant is kept at insertion time.
    // :modifier_map_insertion
    struct level_modifier_mapping_t *modifier_mappings;

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

    if (!g_tree_lookup_extended (keymap->modifiers, name, NULL, NULL)) {
        if (num_modifiers < KEYBOARD_LAYOUT_MAX_MODIFIERS) {
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
            status_l = KEYBOARD_LAYOUT_MOD_MAX_LIMIT_REACHED;
        }

    } else {
        status_l = KEYBOARD_LAYOUT_MOD_REDEFINITION;
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
        // TODO: Maybe store this as a normal modifier inside the modifier
        // mapping tree?
        // :none_modifier
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

    // TODO: For now, types will not be destroyed or renamed, if they do, the
    // name will leak space inside the keymap's pool. This could be a good place
    // to use pooled strings.
    new_type->name = pom_strdup (&keymap->pool, name);
    new_type->modifier_mask = modifier_mask;

    // Get the end of the type linked list. Even though this does a linear
    // search, we don't expect a lot of types, it shouldn't be a big concern.
    // @performance
    struct key_type_t **pos = &keymap->types;
    while (*pos != NULL) {
        pos = &(*pos)->next;
    }

    // Add the new type to the end
    *pos = new_type;

    return new_type;
}

// NOTE: May return NULL if the name does not exist.
struct key_type_t* keyboard_layout_type_lookup (struct keyboard_layout_t *keymap, char *name)
{
    // TODO: Linear search for now. Is this really SO bad that we should use a
    // tree or a hash table? @performance
    struct key_type_t *curr_type = keymap->types;
    while (curr_type != NULL) {
        if (strcmp (curr_type->name, name) == 0) break;
        curr_type = curr_type->next;
    }

    return curr_type;
}

int keyboard_layout_type_get_num_levels (struct key_type_t *type)
{
    // NOTE: This assumes level numbers are contiguous. Also this traverses the
    // type linked list every time it's called. @performance
    int num_levels = 0;
    int last_level = 0;
    struct level_modifier_mapping_t *curr_modifier_mapping = type->modifier_mappings;
    while (curr_modifier_mapping != NULL) {
        if (last_level != curr_modifier_mapping->level) {
            last_level = curr_modifier_mapping->level;
            num_levels++;
        }
        curr_modifier_mapping = curr_modifier_mapping->next;
    }

    return num_levels;
}

enum type_level_mapping_result_status_t {
    KEYBOARD_LAYOUT_MOD_MAP_SUCCESS,
    KEYBOARD_LAYOUT_MOD_MAP_MAPPING_ALREADY_ASSIGNED
};

void keyboard_layout_type_new_level_map (struct keyboard_layout_t *keymap, struct key_type_t *type,
                                         int level, key_modifier_mask_t modifiers,
                                         enum type_level_mapping_result_status_t *status)
{
    assert (keymap != NULL && type != NULL);
    assert (level > 0 && "Levels must be grater than 0");

    enum type_level_mapping_result_status_t status_l;
    // First check that this mask isn't being used already
    // :modifier_map_insertion
    bool mask_found = false;
    struct level_modifier_mapping_t *curr_modifier_mapping = type->modifier_mappings;
    while (curr_modifier_mapping != NULL) {
        if (curr_modifier_mapping->modifiers == modifiers) {
            mask_found = true;
            break;
        }
        curr_modifier_mapping = curr_modifier_mapping->next;
    }

    if (!mask_found) {
        // Find the position where it will be inserted
        // :modifier_map_insertion
        struct level_modifier_mapping_t **pos = &type->modifier_mappings;
        while (*pos != NULL && (*pos)->level < level) {
            pos = &(*pos)->next;
        }

        struct level_modifier_mapping_t *new_mapping =
            mem_pool_push_struct (&keymap->pool, struct level_modifier_mapping_t);
        new_mapping->level = level;
        new_mapping->modifiers = modifiers;
        new_mapping->next = *pos;
        *pos = new_mapping;

        status_l = KEYBOARD_LAYOUT_MOD_MAP_SUCCESS;

    } else {
        status_l = KEYBOARD_LAYOUT_MOD_MAP_MAPPING_ALREADY_ASSIGNED;
    }

    if (status != NULL) {
        *status = status_l;
    }
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
    *lvl = ZERO_INIT (struct key_level_t);
    lvl->keysym = keysym;

    if (action != NULL) {
        lvl->action = *action;
    }
}

void keyboard_layout_destroy (struct keyboard_layout_t *keymap)
{
    if (keymap == NULL) return;

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
    keyboard_layout_type_new_level_map (keymap, type_one_level, 1, 0, NULL);

    struct key_type_t *type;
    type = keyboard_layout_new_type (keymap, "TWO_LEVEL", 0);
    keyboard_layout_type_new_level_map (keymap, type, 1, 0, NULL);
    keyboard_layout_type_new_level_map (keymap, type, 2, 0, NULL);

    type = keyboard_layout_new_type (keymap, "ALPHABETIC", 0);
    keyboard_layout_type_new_level_map (keymap, type, 1, 0, NULL);
    keyboard_layout_type_new_level_map (keymap, type, 2, 0, NULL);
    keyboard_layout_type_new_level_map (keymap, type, 3, 0, NULL);

    struct key_t *key = keyboard_layout_new_key (keymap, KEY_ESC, type_one_level);
    keyboard_layout_key_set_level (key, 1, XKB_KEY_Escape, NULL);

    return keymap;
}

// NOTE: This can return NULL if parsing fails.
// TODO: If parsing fails, somehow return the error message so we can then show
// it to the user.
bool xkb_file_parse (char *xkb_str, struct keyboard_layout_t *keymap);
void xkb_file_write (struct keyboard_layout_t *keymap, string_t *res, struct status_t *status);
struct keyboard_layout_t* keyboard_layout_new_from_xkb (char *xkb_str)
{
    mem_pool_t bootstrap = ZERO_INIT (mem_pool_t);
    struct keyboard_layout_t *keymap = mem_pool_push_size (&bootstrap, sizeof(struct keyboard_layout_t));
    *keymap = ZERO_INIT (struct keyboard_layout_t);
    keymap->pool = bootstrap;

    if (!xkb_file_parse (xkb_str, keymap)) {
        keyboard_layout_destroy (keymap);
        keymap = NULL;
    }

    return keymap;
}

bool keyboard_layout_is_valid (struct keyboard_layout_t *keymap, struct status_t *status)
{
    bool is_valid = true;

    // Check levels in types are contiguous.
    // For more information about why we don't allow non contiguous level
    // mappings see :none_mapping_is_reserved_for_level1
    struct key_type_t *curr_type = keymap->types;
    while (curr_type != NULL) {
        int level = 1;
        struct level_modifier_mapping_t *curr_mapping = curr_type->modifier_mappings;
        while (curr_mapping != NULL) {
            if (curr_mapping->level != level) {
                if (curr_mapping->level == level+1) {
                    level++;
                } else {
                    status_error (status, "Type '%s' has non contiguous levels\n", curr_type->name);
                    is_valid = false;
                    break;
                }
            }
            curr_mapping = curr_mapping->next;
        }

        curr_type = curr_type->next;
    }

    // TODO: Add the following checks to this function:
    //    - Check that we don't use a modifier that isn't in the modifiers
    //      definition tree.
    //
    //    - The XKB backend has a smaller maximum for modifiers than this
    //      internal representation. Maybe make this function receive the
    //      maximum value of modifiers and add a check that we actually used
    //      less than that?. This should be checked after calling the currently
    //      unexistant :keyboard_layout_compact function.
    //
    // Which other checks are useful?


    return is_valid;
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

