/*
 * Copiright (C) 2019 Santiago León O.
 */

// This file contains our internal representation for a keymap, this technically
// duplicates the code from <libxkbcommon>/src/keymap.h. I'm not just copying
// that file here because it's much more complex than what we need at the
// moment. For now I want to start small, and add features incrementally
// depending on how the UI evolves. I don't want to add a dependency on an file
// people isn't supposed to depend on (yet).

#define KEYBOARD_LAYOUT_MAX_LEVELS 8

typedef uint32_t key_modifier_mask_t;

struct key_type_t {
    char *name;
    int num_levels;
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
    char symbol[5]; // NULL terminated UTF-8 character
    string_t action;
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
};

struct keyboard_layout_t* keyboard_layout_new_default (void)
{
    mem_pool_t bootstrap = {0};
    struct keyboard_layout_t *keymap = mem_pool_push_size (&bootstrap, sizeof(struct keyboard_layout_t));
    keymap->pool = bootstrap;

    return keymap;
}

void keyboar_layout_destroy (struct keyboard_layout_t *keymap)
{
    mem_pool_destroy (&keymap->pool);
}
