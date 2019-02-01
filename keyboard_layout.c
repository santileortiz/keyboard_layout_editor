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
};

struct key_type_t* keyboard_layout_new_type (struct keyboard_layout_t *keymap, char *name)
{
    struct key_type_t *new_type = mem_pool_push_size (&keymap->pool, sizeof(struct key_type_t));
    *new_type = ZERO_INIT (struct key_type_t);

    // TODO: For now, types will not be destroyed, if they do, the name will
    // leak space inside the keymap's pool.
    new_type->name = pom_strdup (&keymap->pool, name);

    new_type->next = keymap->types;
    keymap->types = new_type;

    return new_type;
}

void keyboar_layout_type_set_level (struct key_type_t *type, int level, key_modifier_mask_t modifiers)
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
        key->kc = kc;
        keymap->keys[kc] = key;
    }

    key->type = type;

    return key;
}

void keyboard_layout_key_set_level (struct key_t *key, int level, char *symbol, struct key_action_t *action)
{
    struct key_level_t *lvl = &key->levels[level];
    strcpy (lvl->symbol, symbol);

    if (action != NULL) {
        lvl->action = *action;
    }
}

struct keyboard_layout_t* keyboard_layout_new_default (void)
{
    mem_pool_t bootstrap = ZERO_INIT (mem_pool_t);
    struct keyboard_layout_t *keymap = mem_pool_push_size (&bootstrap, sizeof(struct keyboard_layout_t));
    *keymap = ZERO_INIT (struct keyboard_layout_t);
    keymap->pool = bootstrap;

    struct key_type_t *type_one_level;
    type_one_level = keyboard_layout_new_type (keymap, "ONE_LEVEL");
    keyboar_layout_type_set_level (type_one_level, 1, 0);

    struct key_type_t *type;
    type = keyboard_layout_new_type (keymap, "TWO_LEVEL");
    keyboar_layout_type_set_level (type, 1, 0);
    keyboar_layout_type_set_level (type, 2, 0);

    type = keyboard_layout_new_type (keymap, "ALPHABETIC");
    keyboar_layout_type_set_level (type, 1, 0);
    keyboar_layout_type_set_level (type, 2, 0);
    keyboar_layout_type_set_level (type, 3, 0);

    struct key_t *key = keyboard_layout_new_key (keymap, KEY_ESC, type);
    keyboard_layout_key_set_level (key, 1, "E", NULL);

    return keymap;
}

void keyboar_layout_destroy (struct keyboard_layout_t *keymap)
{
    mem_pool_destroy (&keymap->pool);
}
