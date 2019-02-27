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
};

bool parse_unicode_str (const char *str, uint32_t *cp)
{
    assert (str != NULL && cp != NULL);

    bool success = false;
    while (*str && is_space(str)) { str++; }
    if (str[0] == 'U' && str[1] == '+' && str[2] != '\0') {
        char *end;
        uint32_t value = strtol (str+2, &end, 16);
        if (*end == '\0') {
            *cp = value;
            success = true;
        }
    }

    return success;
}

bool codepoint_to_xkb_keysym (uint32_t cp, xkb_keysym_t *res)
{
    assert (res != NULL);

    bool is_cp_valid = true;

    // ASCII range excluding control characters
    if ((cp >= 0x20 && cp <= 0x7E) ||
        (cp >= 0xA0 && cp <= 0xFF)) {
        *res = cp;

    } else if (cp >= 0x100 && cp <= 0x10FFFF) {
        *res = cp | 0x1000000;

    } else {
        is_cp_valid = false;
    }

    return is_cp_valid;
}

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

struct keyboard_layout_t* keyboard_layout_new_default (void)
{
    mem_pool_t bootstrap = ZERO_INIT (mem_pool_t);
    struct keyboard_layout_t *keymap = mem_pool_push_size (&bootstrap, sizeof(struct keyboard_layout_t));
    *keymap = ZERO_INIT (struct keyboard_layout_t);
    keymap->pool = bootstrap;

    struct key_type_t *type_one_level;
    type_one_level = keyboard_layout_new_type (keymap, "ONE_LEVEL");
    keyboard_layout_type_set_level (type_one_level, 1, 0);

    struct key_type_t *type;
    type = keyboard_layout_new_type (keymap, "TWO_LEVEL");
    keyboard_layout_type_set_level (type, 1, 0);
    keyboard_layout_type_set_level (type, 2, 0);

    type = keyboard_layout_new_type (keymap, "ALPHABETIC");
    keyboard_layout_type_set_level (type, 1, 0);
    keyboard_layout_type_set_level (type, 2, 0);
    keyboard_layout_type_set_level (type, 3, 0);

    struct key_t *key = keyboard_layout_new_key (keymap, KEY_ESC, type_one_level);
    keyboard_layout_key_set_level (key, 1, XKB_KEY_Escape, NULL);

    return keymap;
}

enum xkb_parser_token_type_t {
    XKB_PARSER_TOKEN_IDENTIFIER,
    XKB_PARSER_TOKEN_KEY_IDENTIFIER,
    XKB_PARSER_TOKEN_OPERATOR,
    XKB_PARSER_TOKEN_NUMBER,
    XKB_PARSER_TOKEN_STRING
};

struct xkb_parser_state_t {
    mem_pool_t pool;

    struct scanner_t scnr;

    enum xkb_parser_token_type_t tok_type;
    string_t tok_value;

    struct keyboard_layout_t *keymap;
};

void xkb_parser_state_init (struct xkb_parser_state_t *state, char *xkb_str, struct keyboard_layout_t *keymap)
{
    *state = ZERO_INIT(struct xkb_parser_state_t);
    state->scnr.pos = xkb_str;
    state->keymap = keymap;
}

void xkb_parser_state_destory (struct xkb_parser_state_t *state)
{
    str_free (&state->tok_value);
}

void xkb_parser_next (struct xkb_parser_state_t *state)
{
    struct scanner_t *scnr = &state->scnr;

    // TODO: Do we want <ESC> and such symbols be identifiers or maybe a new
    // type XKB_PARSER_TOKEN_KEY_IDENTIFIER.
    char *identifier_chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.";

    // Scan out all comments
    while (scanner_str (scnr, "//")) {
        if (scnr->is_eof) {
            scanner_set_error (scnr, "Stale '/' character");
        }
        scanner_to_char (scnr, '\n');
    }

    scanner_consume_spaces (scnr);
    if (scnr->is_eof) {
        return;
    }

    char *tok_start;
    if ((tok_start = scnr->pos) && scanner_char_any (scnr, identifier_chars)) {
        state->tok_type = XKB_PARSER_TOKEN_IDENTIFIER;
        while (scanner_char_any (scnr, identifier_chars));
        strn_set (&state->tok_value, tok_start, scnr->pos - tok_start);

    } else if (scanner_char (scnr, '<')) {
        // TODO: There is a list of valid key identifiers somewhere as xkbcom
        // crashes when adding unknown ones. Where is this coming from? xkbcomp,
        // X11's spec or XKB's spec. For now we trust they will be valid.
        state->tok_type = XKB_PARSER_TOKEN_KEY_IDENTIFIER;

        tok_start = scnr->pos;
        scanner_to_char (scnr, '>');
        if (scnr->is_eof) {
            scanner_set_error (scnr, "Key identifier without closing '>'");
        } else {
            strn_set (&state->tok_value, tok_start, scnr->pos - tok_start);
            scanner_char (scnr, '>');
        }

    } else if (scanner_char_any (scnr, "{}[](),;=+-!")) {
        state->tok_type = XKB_PARSER_TOKEN_OPERATOR;
        strn_set (&state->tok_value, scnr->pos-1, 1);

    } else if ((tok_start = scnr->pos) && scanner_char_any (scnr, "0123456789")) {
        state->tok_type = XKB_PARSER_TOKEN_NUMBER;

        // TODO: Store the int value somewhere.
        int value;
        scanner_int (scnr, &value);
        strn_set (&state->tok_value, tok_start, scnr->pos - tok_start);

    } else if (scanner_char (scnr, '\"')) {
        state->tok_type = XKB_PARSER_TOKEN_STRING;

        tok_start = scnr->pos;
        scanner_to_char (scnr, '\"');
        if (scnr->is_eof) {
            scanner_set_error (scnr, "String without matching '\"'");
        } else {
            strn_set (&state->tok_value, tok_start, scnr->pos - tok_start);
            scanner_char (scnr, '\"');
        }

    } else {
        scanner_set_error (scnr, "Unexpected character");
    }

    // TODO: Get better error messages, show the line where we got stuck.
    if (!state->scnr.error) {
        //printf ("Type: %d, Value: %s\n", state->tok_type, str_data(&state->tok_value));
    }
}

void xkb_parser_block_start (struct xkb_parser_state_t *state, char *block_id)
{
    xkb_parser_next (state);
    if (state->tok_type != XKB_PARSER_TOKEN_IDENTIFIER || strcmp (str_data(&state->tok_value), block_id) != 0) {
        char *error = pprintf (&state->pool, "Expected block identifier '%s'", block_id);
        scanner_set_error (&state->scnr, error);
    }

    // TODO: Maybe pass a char** as argument and set it to the name?, ATM we
    // don't use this information for anything.
    xkb_parser_next (state);
    if (state->tok_type != XKB_PARSER_TOKEN_STRING) {
        scanner_set_error (&state->scnr, "Expected a block name");
    }

    xkb_parser_next (state);
    if (state->tok_type != XKB_PARSER_TOKEN_OPERATOR || strcmp (str_data(&state->tok_value), "{") != 0) {
        scanner_set_error (&state->scnr, "Expected '{'");
    }
}

void xkb_parser_skip_block (struct xkb_parser_state_t *state, char *block_id)
{
    xkb_parser_block_start (state, block_id);

    // Skip the content of the block
    int braces = 1;
    do {
        xkb_parser_next (state);
        if (state->tok_type == XKB_PARSER_TOKEN_OPERATOR && strcmp (str_data(&state->tok_value), "{") == 0) {
            braces++;
        } else if (state->tok_type == XKB_PARSER_TOKEN_OPERATOR && strcmp (str_data(&state->tok_value), "}") == 0) {
            braces--;
        }
    } while (!state->scnr.is_eof && !state->scnr.error && braces > 0);

    xkb_parser_next (state);
    if (state->tok_type != XKB_PARSER_TOKEN_OPERATOR || strcmp (str_data(&state->tok_value), ";") != 0) {
        scanner_set_error (&state->scnr, "Expected ';'");
    }
}

static inline
bool xkb_parser_match_tok (struct xkb_parser_state_t *state, enum xkb_parser_token_type_t type, char *value)
{
    return state->tok_type == type &&
        (value == NULL || strcmp (str_data(&state->tok_value), value) == 0);
}

void xkb_parser_parse_types (struct xkb_parser_state_t *state)
{
    state->scnr.eof_is_error = true;
    xkb_parser_block_start (state, "xkb_types");

    do {
        xkb_parser_next (state);
        if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "virtual_modifiers")) {
            do {
                xkb_parser_next (state);
                if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, NULL)) {
                    // TODO: Register the defined virtual modifiers.

                    xkb_parser_next (state);
                    if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";")) {
                        break;

                    } else if (!xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, ",")) {
                        scanner_set_error (&state->scnr, "Expected ';' or ','");
                    }

                } else {
                    scanner_set_error (&state->scnr, "Expected modifier name");
                }

            } while (!state->scnr.error);

        } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "type")) {

            string_t type_name = {0};
            xkb_parser_next (state);
            if (state->tok_type == XKB_PARSER_TOKEN_STRING) {
                str_cpy (&type_name, &state->tok_value);

                xkb_parser_next (state);
                if (!xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "{")) {
                    scanner_set_error (&state->scnr, "Expected type block");
                }

                // Consume the block's content
                do {
                    // TODO: Actually get the type information here.
                    xkb_parser_next (state);
                } while (!state->scnr.error && !xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "}"));

                xkb_parser_next (state);
                if (!xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";")) {
                    scanner_set_error (&state->scnr, "Expected ';'");
                }

                if (!state->scnr.error) {
                    keyboard_layout_new_type (state->keymap, str_data(&type_name));
                }

                str_free (&type_name);
            }

        } else {
            break;
        }

    } while (!state->scnr.error);

    if (!xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "}")) {
        scanner_set_error (&state->scnr, "Expected '}'");
    }

    xkb_parser_next (state);
    if (!xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";")) {
        scanner_set_error (&state->scnr, "Expected ';'");
    }

    state->scnr.eof_is_error = false;
}

// This parses a subset of the xkb file syntax into our internal representation
// keyboard_layout_t. We only care about parsing resolved layouts as returned by
// xkbcomp. Notable differences from a full xkb compiler are the lack of include
// statements and a more strict ordering of things.
struct keyboard_layout_t* keyboard_layout_new (char *xkb_str)
{
    mem_pool_t bootstrap = ZERO_INIT (mem_pool_t);
    struct keyboard_layout_t *keymap = mem_pool_push_size (&bootstrap, sizeof(struct keyboard_layout_t));
    *keymap = ZERO_INIT (struct keyboard_layout_t);
    keymap->pool = bootstrap;

    struct xkb_parser_state_t state;
    xkb_parser_state_init (&state, xkb_str, keymap);

    xkb_parser_next (&state);
    if (state.tok_type != XKB_PARSER_TOKEN_IDENTIFIER || strcmp (str_data(&state.tok_value), "xkb_keymap") != 0) {
        scanner_set_error (&state.scnr, "Expected 'xkb_keymap'");
    }

    xkb_parser_next (&state);
    if (state.tok_type != XKB_PARSER_TOKEN_OPERATOR || strcmp (str_data(&state.tok_value), "{") != 0) {
        scanner_set_error (&state.scnr, "Expected '{'");
    }

    xkb_parser_skip_block (&state, "xkb_keycodes");
    xkb_parser_parse_types (&state);
    xkb_parser_skip_block (&state, "xkb_compatibility");
    xkb_parser_skip_block (&state, "xkb_symbols");

    // Should we accept keymaps with geometry section? looks like xkbcomp does
    // not return one.
    //xkb_parser_skip_block (&state, "xkb_geometry");

    // Closing } of xkb_keymap block
    xkb_parser_next (&state);
    if (state.tok_type != XKB_PARSER_TOKEN_OPERATOR || strcmp (str_data(&state.tok_value), "}") != 0) {
        scanner_set_error (&state.scnr, "Expected '}'");
    }

    xkb_parser_next (&state);
    if (state.tok_type != XKB_PARSER_TOKEN_OPERATOR || strcmp (str_data(&state.tok_value), ";") != 0) {
        scanner_set_error (&state.scnr, "Expected ';'");
    }

    // TODO: Make sure we reach EOF here?

    if (state.scnr.error) {
        printf ("Error: %s\n", state.scnr.error_message);

    } else {
        printf ("SUCESS\n");
    }

    return keymap;
}

void keyboard_layout_destroy (struct keyboard_layout_t *keymap)
{
    mem_pool_destroy (&keymap->pool);
}
