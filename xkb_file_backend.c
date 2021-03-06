/*
 * Copiright (C) 2019 Santiago León O.
 */

// xkbcommon does this by calling XConvertCase once. The implementation of
// xkb_keysym_to_lower and xkb_keysym_to_upper call XConvertCase, so we are
// effectiveley calling it twice here.
//
// The reason we don't call XConvertCase directly is that I think the defining
// code for the XKB file format is now libxkbcommon not Xlib. Calling
// libxkbcommon is better to remain compatible in the future.
// :keysym_case_compatibility
bool sym_is_lower (xkb_keysym_t sym)
{
    xkb_keysym_t lower = xkb_keysym_to_lower(sym);
    xkb_keysym_t upper = xkb_keysym_to_upper(sym);

    if (lower == upper)
        return false;

    return (sym == lower ? true : false);
}

// :keysym_case_compatibility
bool sym_is_upper (xkb_keysym_t sym)
{
    xkb_keysym_t lower = xkb_keysym_to_lower(sym);
    xkb_keysym_t upper = xkb_keysym_to_upper(sym);

    if (lower == upper)
        return false;

    return (sym == upper ? true : false);
}

bool sym_is_keypad (xkb_keysym_t sym)
{
    return sym >= XKB_KEY_KP_Space && sym <= XKB_KEY_KP_Equal;
}


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

////////////////////////////////////////////////////////////////////////////////
//
// Internal representation for the compatibility section
//
// The reasoning behind
// puting it here and not in out keymap internal representation is discussed in
// :compatibility_section
//
// For other sections we parse and ignore what our internal representation
// doesn't care about. For the compatibility section we have a more accurate
// representation for all the information available in the xkb file. Later we
// translate it to our real representation that is less cluttered and hopefully
// will work on several platforms in the future.
//
// The reason for this extra step is that I want to keep xkb specific stuff that
// is maybe unnecessary contained here, and not let it trickle to our internal
// representation unless it's absoluteley necessary.
// :platform_specific_data_in_internal_representation
#define XKB_PARSER_COMPAT_CONDITIONS \
    XKB_PARSER_COMPAT_CONDITION(COMPAT_CONDITION_ANY_OF_OR_NONE,"AnyOfOrNone") \
    XKB_PARSER_COMPAT_CONDITION(COMPAT_CONDITION_NONE_OF,"NoneOf")             \
    XKB_PARSER_COMPAT_CONDITION(COMPAT_CONDITION_ANY_OF,"AnyOf")               \
    XKB_PARSER_COMPAT_CONDITION(COMPAT_CONDITION_ALL_OF,"AllOf")               \
    XKB_PARSER_COMPAT_CONDITION(COMPAT_CONDITION_EXACTLY,"Exactly")

enum xkb_parser_compat_condition_t {
#define XKB_PARSER_COMPAT_CONDITION(name,str) name,
    XKB_PARSER_COMPAT_CONDITIONS
#undef XKB_PARSER_COMPAT_CONDITION
    NUM_XKB_PARSER_COMPAT_CONDITIONS
};

char *xkb_parser_compat_condition_names[] = {
#define XKB_PARSER_COMPAT_CONDITION(name,str) str,
    XKB_PARSER_COMPAT_CONDITIONS
#undef XKB_PARSER_COMPAT_CONDITION
};

#undef XKB_PARSER_COMPAT_CONDITIONS

// We don't use the action_type_t enum because here we want to keep track of
// actions that have not been set and ones that have (maybe as NoAction()), so
// that we can later decide which one has priority if it was set from the
// compatibility section.
// :platform_specific_data_in_internal_representation
enum xkb_backend_action_type_t {
    XKB_BACKEND_KEY_ACTION_TYPE_UNSET = 0,
    XKB_BACKEND_KEY_ACTION_TYPE_MOD_SET,
    XKB_BACKEND_KEY_ACTION_TYPE_MOD_LATCH,
    XKB_BACKEND_KEY_ACTION_TYPE_MOD_LOCK,
    XKB_BACKEND_KEY_ACTION_TYPE_NO_ACTION
};

// This is different than key_action_t because it has more xkb specific data.
// The idea of the keyboard_layout.c internal representation is for it to be
// less cluttered than the one in xkbcomp or libxkbcommon and maybe be even
// mutiplatform in the future. Here we store what we parse but later we
// transform it into our internal representation.
//
// It may be the case in the future that some data from here must be preserved
// in our representation so we can get a working representation back, for this
// we wpuld require a new mechanism to be implemented in our internal
// representation (keyboard_layout.c).
// :platform_specific_data_in_internal_representation
struct xkb_backend_key_action_t {
    enum xkb_backend_action_type_t type;

    bool mod_map_mods;
    key_modifier_mask_t modifiers;

    bool clear_locks;
    bool latch_to_lock;
};

struct xkb_compat_interpret_t {
    bool any_keysym;
    key_modifier_mask_t keysym;

    bool repeat;
    bool locking;

    // This flag means real_modifiers has all real modifiers set. We use a flag
    // because computing it means querying the modifier registry 8 times. Maybe
    // it's too wasteful to do this every time the user has 'all' as argument to
    // the interpret condition.
    //
    // TODO: Maybe compute it once, then set it when parsing interpret
    // statements. The problem is I'm not sure at which point in the parsing
    // process we can be sure we know which real modifiers will be used. We
    // can't just register all real modifiers and OR their values. This may
    // exceed the limit of 16 modifiers if the user uses more than 8 virtual
    // modifiers (there are 8 real modifiers). Maybe leave this flag, and
    // compute the value for this mask at a point we are sure all used real
    // modifiers are registered?.
    bool all_real_modifiers;
    key_modifier_mask_t real_modifiers;
    enum xkb_parser_compat_condition_t condition;

    bool level_one_only;
    key_modifier_mask_t virtual_modifier;
    struct xkb_backend_key_action_t action;

    struct xkb_compat_interpret_t *next;
};

struct xkb_compat_t {
    // Interpret defaults
    // TODO: I beleive interpret structures can be initialized to the user's
    // defaults while parsing, so we don't need to store them here. Then if
    // this struct will just have interprets, we can remove it and put that
    // directly in xkb_parser_state_t.
    // :interpret_defaults
    bool level_one_only;
    bool repeat;
    bool locking;

    // Linked list of all interpret statements
    struct xkb_compat_interpret_t *interprets;

    // group and indicator statements are ignored, will they be required?
};

#define XKB_FILE_BACKEND_REAL_MODIFIER_NAMES_LIST \
    {"Shift", "Control", "Lock", "Mod1", "Mod2", "Mod3", "Mod4", "Mod5"}

enum xkb_parser_token_type_t {
    XKB_PARSER_TOKEN_IDENTIFIER,
    XKB_PARSER_TOKEN_KEY_IDENTIFIER,
    XKB_PARSER_TOKEN_LEVEL_IDENTIFIER,
    XKB_PARSER_TOKEN_GROUP_IDENTIFIER,
    XKB_PARSER_TOKEN_OPERATOR,
    XKB_PARSER_TOKEN_NUMBER,
    XKB_PARSER_TOKEN_STRING
};

struct vmodmap_element_t {
    char *name;
    key_modifier_mask_t encoding;
};

struct xkb_parser_state_t {
    mem_pool_t pool;

    struct scanner_t scnr;

    enum xkb_parser_token_type_t tok_type;
    string_t tok_value;
    int tok_value_int;

    struct binary_tree_t key_identifiers_to_keycodes;
    struct binary_tree_t indicator_definitions;

    struct keyboard_layout_t *keymap;

    char **real_modifiers;
    int real_modifiers_len;

    // :compatibility_section
    struct xkb_compat_t compatibility;

    // We don't know the mapping of real modifiers to keycodes until the
    // end of the symbols sections, so we can't resolve actions during parsing
    // of this section. Instead, it's done after parsing is complete.
    //
    // To compute the effective action between those in the compatibility
    // section and those in the symbols section we require the data from
    // xkb_backend_key_action_t not just key_action_t. We store all actions from
    // the symbols section here so we can then compute the effective action for
    // our internal representation.
    // :symbol_actions_array
    // TODO: I'm not 100% sure this is required, but right now it looks like it.
    // If it doesn't we can then remove this big array from here.
    struct xkb_backend_key_action_t symbol_actions[KEY_CNT][KEYBOARD_LAYOUT_MAX_LEVELS];
    key_modifier_mask_t symbol_vmods[KEY_CNT];

    // We could put this in our internal representation as a field in the key_t
    // structure, but I'm not sure I want to do that. From what it looks like,
    // modifier maps are only useful for compatibility interpret statement
    // resolution. In the end, the state of a modifier is only changed by
    // actions. As far as I recall from OSX's keymap format, it doesn't have the
    // concept of a modifier map. Better not clutter the main representation
    // with things that can be potentially platform specific.
    //
    // This array will contain masks that only have a single modifier bit set,
    // the parser must guarantee this is true.
    key_modifier_mask_t modifier_map[KEY_CNT];

    struct vmodmap_element_t vmodmap[KEYBOARD_LAYOUT_MAX_MODIFIERS];

    // Indicators can be bound to virtual modifiers, we use this intermediate
    // array to store parsed leds. After virtual modifier resolution we replace
    // virtual modifiers here for real ones, and those will me the ones added to
    // the resulting keymap.
    key_modifier_mask_t leds[KEYBOARD_LAYOUT_MAX_LEDS];
};

void xkb_parser_state_destory (struct xkb_parser_state_t *state)
{
    str_free (&state->tok_value);
    mem_pool_destroy (&state->pool);
    binary_tree_destroy (&state->key_identifiers_to_keycodes);
    binary_tree_destroy (&state->indicator_definitions);
}

// Shorthand error for when the only replacement being done is the current value
// of the token.
#define xkb_parser_error_tok(state,format) xkb_parser_error(state,format,str_data(&((state)->tok_value)))
GCC_PRINTF_FORMAT(2, 3)
void xkb_parser_error (struct xkb_parser_state_t *state, const char *format, ...)
{
    va_list args1, args2;
    va_start (args1, format);
    va_copy (args2, args1);

    size_t size = vsnprintf (NULL, 0, format, args1) + 1;
    va_end (args1);

    char *str = mem_pool_push_size (&state->pool, size);

    vsnprintf (str, size, format, args2);
    va_end (args2);

    scanner_set_error (&state->scnr, str);
}

// NOTE: key_identifier is expected to already be allocated inside state->pool,
// we do this to avoid double allocation/copying. The parser needs to copy it
// anyway to be able to call this at the end of a keycode assignment statement.
bool xkb_parser_define_key_identifier (struct xkb_parser_state_t *state, char *key_identifier, int kc)
{
    bool new_identifier_defined = false;

    if (!binary_tree_lookup (&state->key_identifiers_to_keycodes, key_identifier, NULL)) {
        binary_tree_insert (&state->key_identifiers_to_keycodes, key_identifier, kc);
        new_identifier_defined = true;

    } else {
        xkb_parser_error (state, "Key identifier '%s' already defined.", key_identifier);
    }

    return new_identifier_defined;
}

bool xkb_parser_key_identifier_lookup (struct xkb_parser_state_t *state, char *key_identifier, int *kc)
{
    assert (kc != NULL);

    bool identifier_found = false;

    struct binary_tree_node_t *node;
    if (binary_tree_lookup (&state->key_identifiers_to_keycodes, key_identifier, &node)) {
        identifier_found = true;
        *kc = node->value;
    }

    return identifier_found;
}

void xkb_parser_next (struct xkb_parser_state_t *state)
{
    struct scanner_t *scnr = &state->scnr;

    char *identifier_chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.";

    scanner_consume_spaces (scnr);
    if (scnr->is_eof) {
        return;
    }

    // Scan out all comments
    while (scanner_str (scnr, "//")) {
        if (scnr->is_eof) {
            xkb_parser_error (state, "Stale '/' character");
        }
        scanner_to_char (scnr, '\n');

        scanner_consume_spaces (scnr);
        if (scnr->is_eof) {
            return;
        }
    }

    char *tok_start;
    if ((tok_start = scnr->pos) && scanner_char_any (scnr, identifier_chars)) {
        state->tok_type = XKB_PARSER_TOKEN_IDENTIFIER;
        while (scanner_char_any (scnr, identifier_chars));
        strn_set (&state->tok_value, tok_start, scnr->pos - tok_start);

        // Check if it's a level identifier, if so, change the type and set the
        // int value. :level_identifiers
        struct scanner_t special_identifier_scnr = ZERO_INIT(struct scanner_t);
        special_identifier_scnr.pos = str_data(&state->tok_value);
        char *bak_start = special_identifier_scnr.pos;
        int level;

        int num_numbers = 0;
        while (scanner_char_any (&special_identifier_scnr, "0123456789")) num_numbers++;

        if (num_numbers == str_len(&state->tok_value)) {
            state->tok_type = XKB_PARSER_TOKEN_NUMBER;
            special_identifier_scnr.pos = bak_start;
            scanner_int (&special_identifier_scnr, &state->tok_value_int);

        } else if ((special_identifier_scnr.pos = bak_start) &&
                   scanner_strcase (&special_identifier_scnr, "level") &&
                   scanner_int (&special_identifier_scnr, &level) &&
                   level > 0 && level <= KEYBOARD_LAYOUT_MAX_LEVELS) {

            state->tok_type = XKB_PARSER_TOKEN_LEVEL_IDENTIFIER;
            state->tok_value_int = level;

        } else if ((special_identifier_scnr.pos = bak_start) &&
                   scanner_strcase (&special_identifier_scnr, "group") &&
                   scanner_int (&special_identifier_scnr, &level) &&
                   level > 0 && level <= KEYBOARD_LAYOUT_MAX_GROUPS) {

            state->tok_type = XKB_PARSER_TOKEN_GROUP_IDENTIFIER;
            state->tok_value_int = level;

        } else {
            // TODO: Check the identifier value is one of the valid identifiers.
        }

    } else if (scanner_char (scnr, '<')) {
        state->tok_type = XKB_PARSER_TOKEN_KEY_IDENTIFIER;

        tok_start = scnr->pos;
        scanner_to_char (scnr, '>');
        if (scnr->is_eof) {
            xkb_parser_error (state, "Key identifier without closing '>'");
        } else {
            strn_set (&state->tok_value, tok_start, scnr->pos - 1 - tok_start);
        }

    } else if (scanner_char_any (scnr, "{}[](),;=+-!")) {
        state->tok_type = XKB_PARSER_TOKEN_OPERATOR;
        strn_set (&state->tok_value, scnr->pos-1, 1);

    } else if (scanner_char (scnr, '\"')) {
        state->tok_type = XKB_PARSER_TOKEN_STRING;

        tok_start = scnr->pos;
        scanner_to_char (scnr, '\"');
        if (scnr->is_eof) {
            xkb_parser_error (state, "String without matching '\"'");
        } else {
            strn_set (&state->tok_value, tok_start, scnr->pos - 1 - tok_start);
        }

    } else {
        xkb_parser_error (state, "Unexpected character %c (0x%x).", *scnr->pos, *scnr->pos);
    }

    // TODO: Get better error messages, show the line where we got stuck.
    if (!state->scnr.error) {
        //printf ("Type: %d, Value: %s\n", state->tok_type, str_data(&state->tok_value));
    }
}

// A token matches if types are equal and if value is not NULL then the values
// must match too.
static inline
bool xkb_parser_match_tok (struct xkb_parser_state_t *state, enum xkb_parser_token_type_t type, char *value)
{
    return state->tok_type == type &&
        (value == NULL || strcmp (str_data(&state->tok_value), value) == 0);
}

// Case insensitive version of xkb_parser_match_tok()
static inline
bool xkb_parser_match_tok_i (struct xkb_parser_state_t *state, enum xkb_parser_token_type_t type, char *value)
{
    return state->tok_type == type &&
        (value == NULL || strcasecmp (str_data(&state->tok_value), value) == 0);
}

void xkb_parser_expect_tok (struct xkb_parser_state_t *state, enum xkb_parser_token_type_t type, char *value)
{
    if (!xkb_parser_match_tok (state, type, value)) {
        if (state->tok_type != type) {
            // TODO: show identifier types as strings.
            if (value == NULL) {
                xkb_parser_error (state, "Expected Identifier of type '%d', got '%s' of type '%d'.",
                                  type, str_data(&state->tok_value), state->tok_type);
            } else {
                xkb_parser_error (state, "Expected Identifier '%s' of type '%d', got '%s' of type '%d'.",
                                  value, type, str_data(&state->tok_value), state->tok_type);
            }

        } else {
            assert (value != NULL);
            xkb_parser_error (state, "Expected '%s', got '%s'.", value, str_data(&state->tok_value));
        }
    }
}

// Advances one token, checks if it matches the expected token, if it doesn't
// the error is set.
static inline
void xkb_parser_consume_tok (struct xkb_parser_state_t *state, enum xkb_parser_token_type_t type, char *value)
{
    xkb_parser_next (state);
    xkb_parser_expect_tok (state, type, value);
}

void xkb_parser_block_start (struct xkb_parser_state_t *state, char *block_id)
{
    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, block_id);
    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_STRING, NULL);

    // TODO: Maybe pass a char** as argument and set it to the name?, ATM we
    // don't use the type name for anything.

    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "{");
}

void xkb_parser_skip_block (struct xkb_parser_state_t *state, char *block_id)
{
    xkb_parser_block_start (state, block_id);

    // Skip the content of the block
    int braces = 1;
    do {
        xkb_parser_next (state);
        if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "{")) {
            braces++;
        } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "}")) {
            braces--;
        }
    } while (!state->scnr.is_eof && !state->scnr.error && braces > 0);

    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";");
}

bool xkb_parser_is_real_modifier (struct xkb_parser_state_t *state, char *name)
{
    bool is_real_modifier = false;
    for (int i=0; i<state->real_modifiers_len; i++) {
        if (strcasecmp (state->real_modifiers[i], name) == 0) {
            is_real_modifier = true;
            break;
        }
    }

    return is_real_modifier;
}

key_modifier_mask_t xkb_parser_modifier_lookup (struct xkb_parser_state_t *state, char *name)
{
    key_modifier_mask_t result = 0;

    enum modifier_result_status_t status;
    result = keyboard_layout_get_modifier (state->keymap, str_data(&state->tok_value), &status);
    if (status == KEYBOARD_LAYOUT_MOD_UNDEFINED) {
        xkb_parser_error_tok (state, "Reference to undefined modifier '%s'.");
    }

    return result;
}

// NOTE: Strangeley enough xkbcomp accepts strings like none+Shift+Control as
// valid modifier masks, my guess is none will be just 0 and not affect the mask
// in any way (but I haven't checked). We interpret them this way here.
void xkb_parser_parse_modifier_mask (struct xkb_parser_state_t *state,
                                     char *end_operator, key_modifier_mask_t *modifier_mask)
{
    assert (modifier_mask != NULL);

    *modifier_mask = 0;
    do {
        xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, NULL);

        *modifier_mask |= xkb_parser_modifier_lookup (state, str_data(&state->tok_value));

        xkb_parser_next (state);
        if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, end_operator)) {
            break;

        } else if (!xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "+")) {
            xkb_parser_error (state, "Expected '%s' or '+', got '%s'.", end_operator, str_data(&state->tok_value));
        }

    } while (!state->scnr.error);
}

void xkb_parser_skip_until_operator (struct xkb_parser_state_t *state, char *operator)
{
    do {
        xkb_parser_next (state);
    } while (!state->scnr.error && !xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, operator));
}

void xkb_parser_indicator_definition (struct xkb_parser_state_t *state)
{
    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_NUMBER, NULL);
    int id = state->tok_value_int;

    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "=");

    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_STRING, NULL);
    char *name = pom_strdup (&state->pool, str_data(&state->tok_value));

    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";");

    if (!state->scnr.error) {
        // NOTE: We don't check that these id hasn't been defined before. I
        // tested lixkbcommon and it looks like they don't fail on this either.
        binary_tree_insert (&state->indicator_definitions, name, id);
    }
}

void xkb_parser_parse_keycodes (struct xkb_parser_state_t *state)
{
    state->scnr.eof_is_error = true;
    xkb_parser_block_start (state, "xkb_keycodes");

    do {
        xkb_parser_next (state);
        if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_KEY_IDENTIFIER, NULL)) {
            char *key_identifier = pom_strdup (&state->pool, str_data(&state->tok_value));

            xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "=");

            xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_NUMBER, NULL);
            int kc = state->tok_value_int - 8;

            xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";");

            xkb_parser_define_key_identifier (state, key_identifier, kc);

        } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "alias")) {
            xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_KEY_IDENTIFIER, NULL);
            string_t tmp_identifier = {0};
            str_cpy (&tmp_identifier, &state->tok_value);

            xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "=");

            bool ignore_alias = false;
            xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_KEY_IDENTIFIER, NULL);
            int kc;
            if (!xkb_parser_key_identifier_lookup (state, str_data(&state->tok_value), &kc)) {
                printf ("Ignoring alias for '%s' as key identifier '%s' is undefined.",
                        str_data(&tmp_identifier), str_data(&state->tok_value));
                ignore_alias = true;
            }

            xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";");

            if (!state->scnr.error && !ignore_alias) {
                char *new_identifier = pom_strdup (&state->pool, str_data(&tmp_identifier));
                xkb_parser_define_key_identifier (state, new_identifier, kc);
            }

            str_free (&tmp_identifier);

        } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "indicator")) {
            xkb_parser_indicator_definition (state);

        } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "virtual")) {
            // NOTE: We treat these the same as 'real' indicators. There is
            // pretty much no information about these, the only thing I coulod
            // find is that the first 4 ids are real and the rest are virtual,
            // so then why does the virtual keyword exist?. I will treat all
            // indicators the same.
            xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "indicator");
            xkb_parser_indicator_definition (state);

        } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "minimum") ||
                   xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "maximum")) {

            // Ignore these statements.
            xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "=");
            xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_NUMBER, NULL);
            xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";");

        } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "}")) {
            break;
        } else {
            printf ("Something else!!!\n");
        }

    } while (!state->scnr.error);

    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";");

    state->scnr.eof_is_error = false;
}

void xkb_parser_virtual_modifier_definition (struct xkb_parser_state_t *state)
{
    do {
        xkb_parser_next (state);
        if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, NULL)) {
            enum modifier_result_status_t status;
            keyboard_layout_new_modifier (state->keymap, str_data(&state->tok_value), &status);
            if (status == KEYBOARD_LAYOUT_MOD_MAX_LIMIT_REACHED) {
                // NOTE: This is not the actual XKB limit of 16, here we reached
                // the maximum possible of our internal representation
                // (currently 32).
                xkb_parser_error (state, "Too many modifier definitions.");

            } else if (status == KEYBOARD_LAYOUT_MOD_REDEFINITION) {
                // We really don't care about this, if a modifier is defined
                // multiple times, we just don't define it multiple times, the
                // first definition will be used. This is actually expected
                // because modifiers are defined twice, once in the types
                // section and another time in the compatibility section. We
                // 'could' define things per section but meh it's just bothering
                // the user to follow useless syntax. I think we shouldn't even
                // be defining modifiers in sections, seems unnecessary. The
                // only place I see modifier definitions being meaningful is
                // inside type definitions (they force the state of a modifier
                // to be off).
                // TODO: Delete this output state from modifier_result_status_t?
                // seems useless.
            }

            xkb_parser_next (state);
            if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";")) {
                break;

            } else if (!xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, ",")) {
                xkb_parser_error (state, "Expected ';' or ','");
            }

        } else {
            xkb_parser_error (state, "Expected modifier name");
        }

    } while (!state->scnr.error);
}

void xkb_parser_parse_types (struct xkb_parser_state_t *state)
{
    state->scnr.eof_is_error = true;
    xkb_parser_block_start (state, "xkb_types");

    do {
        xkb_parser_next (state);
        if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "virtual_modifiers")) {
            xkb_parser_virtual_modifier_definition (state);

        } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "type")) {

            xkb_parser_next (state);
            if (state->tok_type == XKB_PARSER_TOKEN_STRING) {
                struct key_type_t *new_type;
                char *type_name;
                key_modifier_mask_t type_modifier_mask;

                mem_pool_t type_data = ZERO_INIT(mem_pool_t);
                type_name = pom_strdup (&type_data, str_data(&state->tok_value));

                xkb_parser_next (state);
                if (!xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "{")) {
                    xkb_parser_error (state, "Expected type block");
                }

                // Parse the type's modifier mask.
                // NOTE: We assume the modifier mask is the first entry in the
                // type block. xkbcomp tries to compile types without this at
                // the start, but I think it will always fail anyway.
                xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "modifiers");
                xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "=");
                xkb_parser_parse_modifier_mask (state, ";", &type_modifier_mask);

                new_type = keyboard_layout_new_type (state->keymap, type_name, type_modifier_mask);

                // Parse the other type statements.
                do {
                    key_modifier_mask_t level_modifiers;
                    int level;

                    xkb_parser_next (state);
                    if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "map")) {
                        xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "[");

                        xkb_parser_parse_modifier_mask (state, "]", &level_modifiers);

                        xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "=");

                        xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_LEVEL_IDENTIFIER, NULL);
                        level = state->tok_value_int;

                        xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";");

                        if (level_modifiers == 0 && level != 1) {
                            // I checked and xkbcomp does something weird here.
                            // The 'none' modifier mask can't be assigned
                            // multiple times to different levels. At the same
                            // time, if there is no definition for Level 1 it
                            // gets assigned the 'none' mapping. Now, if we map
                            // 'none' to a level different than 1 and don't add
                            // a mapping for level 1, xkbcomp doesn't complain,
                            // but then I don't know which level gets mapped to
                            // the 'none' mask.
                            // TODO: Check how xkbcomp maps modifiers in this
                            // case.
                            //
                            // What we do here is reserve 'none' to Level1 by
                            // convention, and fail if another level tries to
                            // map the empty mask. As far as I know there is no
                            // reason to map 'none' multiple times or to a level
                            // different than 1. To me this looks like a
                            // consequence of the choosen syntax for level
                            // mappings that allows skipping levels.
                            // TODO: Check that no type in the xkb database maps
                            // the 'none' modifier mask to a level different
                            // than 1. If there is, then determine why, is it
                            // necessary in that case? or is it a bug in the xkb
                            // database?.
                            //
                            // I think a better syntax for level mappings could
                            // be something like
                            //
                            //      map = {none, Shift+Alt, Control};
                            //
                            // And to specify multiple mappings to a level use:
                            //
                            //      map = {none, (Shift+Alt, Lock), Control};
                            //
                            // This way 'none' can be mapped to a different
                            // level and we are not as strict as we are here
                            // where 'none' can't be mapped at all. But at the
                            // same time we make impossible to make uncontiguous
                            // assignments as the user never specifies the
                            // level, it's computed from the position in the
                            // list.
                            // :none_mapping_is_reserved_for_level1
                            xkb_parser_error (state, "Can't map 'none' to level %d. It's reserved for level 1.", level);

                        } else if (~type_modifier_mask & level_modifiers) {
                            // TODO: Tell the user which modifiers are the
                            // problematic ones.
                            xkb_parser_error (state,
                                              "Modifier map for level %d uses modifiers not in the mask for type '%s'.",
                                              level, str_data(&new_type->name));
                        }

                        if (!state->scnr.error) {
                            enum type_level_mapping_result_status_t status;
                            keyboard_layout_type_new_level_map (state->keymap,
                                                                new_type, level, level_modifiers,
                                                                &status);
                            if (status == KEYBOARD_LAYOUT_MOD_MAP_MAPPING_ALREADY_ASSIGNED) {
                                // TODO: Print the modifier mask niceley like
                                // Shift+Alt, not a hexadecimal value.
                                xkb_parser_error (state, "Modifier mask %x already assigned in type '%s'",
                                                  level_modifiers, str_data(&new_type->name));
                            }
                        }

                    } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "level_name") ||
                               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "preserve")) {
                        // TODO: We ignore these statements for now.
                        xkb_parser_skip_until_operator (state, ";");

                    } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "}")) {
                        break;

                    } else {
                        xkb_parser_error (state, "Invalid statement in key type");
                    }

                } while (!state->scnr.error);

                xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";");

                // No matter what we parse, level1 will have the mapping of no
                // modifiers.
                if (!state->scnr.error) {
                    keyboard_layout_type_new_level_map (state->keymap, new_type, 1, 0, NULL);
                    // We can ignore the error here because the only way we
                    // could succed here is if 'none' was already assigned to
                    // level 1.
                    // :none_mapping_is_reserved_for_level1
                }

                mem_pool_destroy (&type_data);
            }

        } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "}")) {
            break;

        } else {
            xkb_parser_error (state, "Invalid statement in types section");
        }

    } while (!state->scnr.error);

    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";");

    state->scnr.eof_is_error = false;
}

bool xkb_parser_match_real_modifier_mask (struct xkb_parser_state_t *state,
                                          char *end_operator, key_modifier_mask_t *modifier_mask)
{
    assert (modifier_mask != NULL);

    bool success = false;
    *modifier_mask = 0;
    do {
        if (xkb_parser_is_real_modifier (state, str_data(&state->tok_value))) {
            *modifier_mask |= xkb_parser_modifier_lookup (state, str_data(&state->tok_value));

            xkb_parser_next (state);
            if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, end_operator)) {
                success = true;
                break;

            } else if (!xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "+")) {
                xkb_parser_error (state, "Expected '%s' or '+', got '%s'.", end_operator, str_data(&state->tok_value));

            } else {
                // NOTE: This is in the else to make clear that the following
                // condition must be true:
                //
                //    xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR,"+")
                //
                // This means we we expect a modifier to come next. This is
                // important because we don't want to consume a token past the
                // end operator.
                xkb_parser_next (state);
            }

        } else {
            xkb_parser_error_tok (state, "Expected a real modifier, got '%s'.");
        }

    } while (!state->scnr.error);

    return success;
}

bool xkb_parser_match_keysym (struct xkb_parser_state_t *state, xkb_keysym_t *keysym)
{
    assert (keysym != NULL);

    bool success = true;

    if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, NULL) ||
        (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_NUMBER, NULL) && state->tok_value_int < 10)) {

        xkb_keysym_t keysym_res = xkb_keysym_from_name (str_data(&state->tok_value), XKB_KEYSYM_NO_FLAGS);
        if (strcmp (str_data(&state->tok_value), "NoSymbol") != 0 && keysym_res == XKB_KEY_NoSymbol) {
            xkb_parser_error_tok (state, "Invalid keysym name '%s'.");
            success = false;
        } else {
            *keysym = keysym_res;
        }
    }

    return success;
}

void xkb_parser_parse_boolean_literal (struct xkb_parser_state_t *state, bool *value)
{
    assert (value != NULL);

    xkb_parser_next (state);
    if (xkb_parser_match_tok_i (state, XKB_PARSER_TOKEN_IDENTIFIER, "no") ||
        xkb_parser_match_tok_i (state, XKB_PARSER_TOKEN_IDENTIFIER, "false") ||
        xkb_parser_match_tok_i (state, XKB_PARSER_TOKEN_IDENTIFIER, "off")) {
        *value = false;

    } else if (xkb_parser_match_tok_i (state, XKB_PARSER_TOKEN_IDENTIFIER, "yes") ||
               xkb_parser_match_tok_i (state, XKB_PARSER_TOKEN_IDENTIFIER, "true") ||
               xkb_parser_match_tok_i (state, XKB_PARSER_TOKEN_IDENTIFIER, "on")) {
        *value = true;

    } else {
        xkb_parser_error_tok (state, "Invalid truth value for clearLocks: '%s'.");
    }
}

// NOTE: This does not set any default value in action, if a value is not parsed
// it's left as it was before.
void xkb_parser_parse_action (struct xkb_parser_state_t *state, struct xkb_backend_key_action_t *action)
{
    assert (state != NULL && action != NULL);

    xkb_parser_next (state);
    if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "SetMods")) {
        action->type = XKB_BACKEND_KEY_ACTION_TYPE_MOD_SET;

    } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "LatchMods")) {
        action->type = XKB_BACKEND_KEY_ACTION_TYPE_MOD_LATCH;

    } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "LockMods")) {
        action->type = XKB_BACKEND_KEY_ACTION_TYPE_MOD_LOCK;

    } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "NoAction")) {
        action->type = XKB_BACKEND_KEY_ACTION_TYPE_NO_ACTION;

    } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "SetGroup") ||
               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "LatchGroup") ||
               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "LockGroup") ||
               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "SetControls") ||
               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "LockControls") ||
               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "ISOLock") ||

               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "MovePtr") ||
               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "MovePointer") ||

               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "PtrBtn") ||
               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "PointerButton") ||

               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "LockPtrBtn") ||
               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "LockPointerButton") ||
               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "LockPtrButton") ||
               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "LockPointerBtn") ||

               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "SetPtrDflt") ||
               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "SetPointerDefault") ||

               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "ActionMessage") ||
               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "MessageAction") ||
               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "Message") ||

               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "Redirect") ||
               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "RedirectKey") ||

               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "Terminate") ||
               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "TerminateServer") ||

               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "SwitchScreen") ||

               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "DevBtn") ||
               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "DeviceBtn") ||
               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "DevButton") ||
               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "DeviceButton") ||

               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "LockDevBtn") ||
               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "LockDeviceBtn") ||
               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "LockDevButton") ||
               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "LockDeviceButton") ||

               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "Private")) {

        // Ignore all these actions.
        // TODO: Which of these are useful/required? If some are required, how do we
        // store them in our IR without adding a lot of stuff that won't be
        // available in other platforms?.
        action->type = XKB_BACKEND_KEY_ACTION_TYPE_UNSET;

    } else {
        xkb_parser_error_tok (state, "Invalid action name '%s'.");
    }

    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "(");
    if (action->type == XKB_BACKEND_KEY_ACTION_TYPE_UNSET) {
        // Skip all ignored actions
        xkb_parser_skip_until_operator (state, ")");
        *action = ZERO_INIT(struct xkb_backend_key_action_t);

    } else if (action->type == XKB_BACKEND_KEY_ACTION_TYPE_NO_ACTION) {
        xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, ")");

    } else {
        // Currently we only support modifier actions, so that's the kind of
        // action we parse here.

        // Because boolean arguments of an action can use the shorthand of the
        // name, they can have 3 syntaxes:
        //
        //      clearLocks              (1)
        //      ~clearLocks             (2)
        //      clearLocks=yes          (3)
        //
        // To distinguish (1) from (3) we need to get the next token. If it's a
        // ',' or ')' then the value of the flag is true and we are parsing (1),
        // if instead it's an '=' we need to keep parsing to get the value
        // because we are parsing (3).
        //
        // Now, the ',' and ')' are supposed to be consumed at the beginning of
        // the do loop, not inside the argument parsing. The
        // list_separator_consumed flag is used to know if we need to parse a
        // token at the begining of the loop or it was already done as part of
        // the previous iteration.
        //
        // This also happens when parsong the modifier mask:
        //
        //      modifiers = VMOD1 + VMOD2 ...
        //
        // To detect the end of the list we need to parse the next token, if
        // it's + then this still the mask, but if it's ',' or ')' then we need
        // to stop parsing the mask but we already parsed the next list
        // separator.
        //
        // :action_arguments_use_lookahead
        //
        // TODO: A cleaner approach to this would be to have a xkb_parser_peek()
        // function, that stores the next token somewhere else, then the
        // followng call to xkb_parser_next() instead of advancing the scanner,
        // copies the available peek into the normal tok_* variables in the
        // parser state.
        // :parser_peek_function

        bool list_separator_consumed = false;

        do {

            // :action_arguments_use_lookahead
            if (!list_separator_consumed) {
                xkb_parser_next (state);
            }
            list_separator_consumed = false;

            if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "modifiers")) {
                xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "=");

                // We could also use a xkb_parser_peek() function here.
                // :parser_peek_function
                xkb_parser_next (state);
                if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "modMapMods")) {
                    action->mod_map_mods = true;

                } else {

                    // NOTE: xkb_parser_parse_modifier_mask() isn't used here
                    // because end_operator can have 2 possible values either
                    // ',' or ')'. Also, we already parsed the first token in
                    // xkb_parser_next() above.
                    do {
                        if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, NULL)) {
                            action->modifiers |= xkb_parser_modifier_lookup (state, str_data(&state->tok_value));
                        }

                        xkb_parser_next (state);
                        if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, ",") ||
                            xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, ")") ) {
                            break;

                        } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "+")) {
                            xkb_parser_next (state);

                        } else {
                            xkb_parser_error_tok (state, "Expected ')', ',' or '+', got '%s'.");
                        }

                    } while (!state->scnr.error);

                    list_separator_consumed = true;
                }

            } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "~")) {
                xkb_parser_next (state);
                if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "clearLocks")) {
                    action->clear_locks = false;

                } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "latchToLock")) {
                    action->latch_to_lock = false;

                } else {
                    xkb_parser_error_tok (state, "Expected clearLocks or latchToLock, got '%s'");
                }

            } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "clearLocks")) {
                // This could also be nicer if we had :parser_peek_function
                xkb_parser_next (state);
                if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, ",") ||
                    xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, ")")) {
                    action->clear_locks = true;
                    list_separator_consumed = true;

                } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "=")) {
                    xkb_parser_parse_boolean_literal (state, &action->clear_locks);
                }

            } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "latchToLock")) {
                xkb_parser_next (state);
                if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, ",") ||
                    xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, ")")) {
                    action->latch_to_lock = true;
                    list_separator_consumed = true;

                } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "=")) {
                    xkb_parser_parse_boolean_literal (state, &action->latch_to_lock);
                }

            } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, ")")) {
                break;

            } else if (!xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, ",")) {
                xkb_parser_error_tok (state, "Expected ')' or ',', got '%s'.");
            }

        } while (!state->scnr.error);
    }
}

// I have read a LOT about this compatibility section and it still baffles me.
// The whole motivation behind it seems to be keeping compatibility between
// servers using XKB and XKB unaware clients.
//
// As far as I've been able to gather, this section exists so that the X server
// can quickly answer to requests from clients to make changes in the
// configuration. [1] Talks about the 'compatibility problem' where a client
// asks to remap a symbol that has an action bound to it, there is a problem
// beacuse core keymaps don't have actions, the server needs to traverse the
// FULL layout looking for the symbol, get the modifier state required, then
// search for that action and remap it too. Although inconvenient and slow it
// doesn't look impossible as Pascal describes it, maybe I'm missing something
// here. From [2] I can see why having different kinds of clients is
// problematic, and some data is required to map changes made from request
// between core and XKB states. Still, I don't see why this trickeled all the
// way down to being exposed in the XKB file format. Why not just support
// actions in the symbols section, then programatically generate this
// compatibility map data?, maybe we loose some data in the generation, maybe we
// need to get more implicit data from the user about the behavior of the
// translation, maybe it was too computationally expensive for the time, or
// maybe this stuck as the way to define actions because it's more user friendly
// for people hand editing XKB files, creating keymaps in the database, I don't
// know.
//
// Whatever the case may be, we are at a point in time where most (maybe more
// than 98%?) applications will be XKB aware, because they are written either in
// Gtk or Qt, X11 is being replaced and computers are crazy fast. I think it's
// time to try and get rid of this section and see how things go.
//
// Unfortunately the wole xkb database defines key actions using this section,
// so we need to get them from it. This code parses the compatibility section
// and stores it in the parser state, then translates all defined actions and
// puts them besides its corresponding symbol. This needs to be done in 2 steps
// because conflict resolution between interpret statements is done like in CSS:
// the more specific one wins.
//
// :compatibility_section
//
// [1] http://pascal.tsu.ru/en/xkb/gram-compat.html
// [2] https://www.x.org/releases/X11R7.7/doc/libX11/XKB/xkblib.html#The_Xkb_Compatibility_Map
void xkb_parser_parse_compat (struct xkb_parser_state_t *state)
{
    state->scnr.eof_is_error = true;
    xkb_parser_block_start (state, "xkb_compatibility");

    int braces = 1;
    do {
        xkb_parser_next (state);

        if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "virtual_modifiers")) {
            xkb_parser_virtual_modifier_definition (state);

        } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "interpret.useModMapMods")) {
            xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "=");

            xkb_parser_next (state);
            if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_LEVEL_IDENTIFIER, "level1") ||
                xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "levelone")) {
                state->compatibility.level_one_only = true;

            } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "anylevel") ||
                       xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "AnyLevel") ||
                       xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "any")) {
                state->compatibility.level_one_only = false;

            } else {
                xkb_parser_error_tok (state, "Invalid value for useModMapMods '%s'.");
            }

            xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";");

        } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "interpret.repeat")) {
            xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "=");
            xkb_parser_parse_boolean_literal (state, &state->compatibility.repeat);
            xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";");

        } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "interpret.locking")) {
            xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "=");
            xkb_parser_parse_boolean_literal (state, &state->compatibility.locking);
            xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";");

        } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "interpret")) {
            struct xkb_compat_interpret_t new_interpret_data = {0};
            // TODO: Set the correct defaults for the interpret. The correct
            // handling would be to use the configured defaults from the xkb
            // file, if a default is not present then use the same as xkbcomp
            // and libxkbcommon would use.
            // :interpret_defaults

            new_interpret_data.all_real_modifiers = false;

            xkb_parser_next (state);
            if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "Any")) {
                new_interpret_data.any_keysym = true;

            } else if (xkb_parser_match_keysym (state, &new_interpret_data.keysym)) {
                // keysym was set while evaluating the condition.

            } else {
                xkb_parser_error_tok (state, "Unexpected identifier '%s'.");
            }

            // Parse the interpret declaration (before the block)
            xkb_parser_next (state);
            if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "+")) {
                xkb_parser_next (state);

                bool next_is_condition = false;
                for (int i=0; i<ARRAY_SIZE(xkb_parser_compat_condition_names); i++) {
                    if (xkb_parser_match_tok (state,
                                              XKB_PARSER_TOKEN_IDENTIFIER,
                                              xkb_parser_compat_condition_names[i]))
                    {
                        next_is_condition = true;
                        new_interpret_data.condition = i;
                        break;
                    }
                }

                if (next_is_condition) {
                    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "(");

                    xkb_parser_next (state);
                    if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "all")) {
                        new_interpret_data.all_real_modifiers = true;
                        xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, ")");

                    } else if (xkb_parser_match_real_modifier_mask (state, ")", &new_interpret_data.real_modifiers)) {
                        // Real modifier parsing successful, continue.

                    } else {
                        xkb_parser_error_tok (state, "Expected real modifier or 'Any', got '%s'.");
                    }

                    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "{");
                    // GOTO :parse_interpret_block_statements

                } else if (xkb_parser_match_real_modifier_mask (state, "{", &new_interpret_data.real_modifiers)) {
                    // GOTO :parse_interpret_block_statements

                } else {
                    xkb_parser_error_tok (state, "Expected a real modifier or a condition, got '%s'.");
                }

            } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "{")) {
                // GOTO :parse_interpret_block_statements

            } else {
                xkb_parser_error_tok (state, "Expected + or {, got '%s'.");
            }

            // Parse the content of the interpret block
            // :parse_interpret_block_statements
            do {
                xkb_parser_next (state);

                if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "locking")) {
                    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "=");
                    xkb_parser_parse_boolean_literal (state, &new_interpret_data.locking);
                    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";");

                } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "repeat")) {
                    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "=");
                    xkb_parser_parse_boolean_literal (state, &new_interpret_data.repeat);
                    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";");

                } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "virtualModifier")) {
                    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "=");
                    xkb_parser_parse_modifier_mask (state, ";", &new_interpret_data.virtual_modifier);
                    if (!single_bit_set (new_interpret_data.virtual_modifier)) {
                        xkb_parser_error (state, "Expected single virtual modifier, more provided.");
                    }

                } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "action")) {

                    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "=");

                    xkb_parser_parse_action (state, &new_interpret_data.action);
                    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";");

                } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "useModMapMods")) {

                    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "=");

                    xkb_parser_next (state);
                    if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_LEVEL_IDENTIFIER, "level1") ||
                        xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "levelone")) {
                        new_interpret_data.level_one_only = true;

                    } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "anylevel") ||
                               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "AnyLevel") ||
                               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "any")) {
                        new_interpret_data.level_one_only = false;

                    } else {
                        xkb_parser_error_tok (state, "Invalid value for useModMapMods '%s'.");
                    }

                    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";");

                } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "}")) {
                    break;

                } else {
                    xkb_parser_error_tok (state, "Invalid statement '%s' inside interpret block.");
                }

            } while (!state->scnr.error);

            xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";");

            if (!state->scnr.error) {
                struct xkb_compat_interpret_t *new_interpret =
                    mem_pool_push_struct (&state->pool, struct xkb_compat_interpret_t);
                *new_interpret = new_interpret_data;

                new_interpret->next = state->compatibility.interprets;
                state->compatibility.interprets = new_interpret;
            }

        } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "group")) {
            // Ignore
            xkb_parser_skip_until_operator (state, ";");

        } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "indicator")) {
            xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_STRING, NULL);

            int ind_code = 1;
            char *ind_name = NULL;
            {
                struct binary_tree_node_t *node;
                binary_tree_lookup (&state->indicator_definitions, str_data(&state->tok_value), &node);
                if (node != NULL) {
                    ind_code = node->value;
                    ind_name = node->key;

                } else {
                    // If the definition for the modifier is missing we find the
                    // first unassigned indicator code and assign it there.
                    // TODO: Looks like libxkbcommon does this but I'm not sure.
                    // Check if this is the case.
                    int first_empty;
                    for (first_empty=0; first_empty<KEYBOARD_LAYOUT_MAX_LEDS; first_empty++) {
                        if (state->keymap->leds[first_empty] == 0x0) {
                            break;
                        }
                    }

                    char *name = pom_strdup (&state->pool, str_data(&state->tok_value));
                    if (first_empty < KEYBOARD_LAYOUT_MAX_LEDS) {
                        binary_tree_insert (&state->indicator_definitions, name, first_empty);

                    } else {
                        xkb_parser_error (
                            state,
                            "Late definition of indicator '%s' failed, not enough indicators left.",
                            name);
                    }
                }
            }

            xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "{");

            bool ignore_indicator_block = false;
            key_modifier_mask_t modifiers = 0x0;
            do {
                xkb_parser_next (state);

                if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "whichModState")) {
                    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "=");

                    xkb_parser_next (state);
                    if (xkb_parser_match_tok_i (state, XKB_PARSER_TOKEN_IDENTIFIER, "locked") ||
                        xkb_parser_match_tok_i (state, XKB_PARSER_TOKEN_IDENTIFIER, "effective")) {
                        // Do nothing.
                        // I tried changing this value using xkbcomp and it only
                        // worked whith these types. Using a latch modifier and
                        // changing this to latch just disabled the indicator.
                        // So, our IR won't have this concept, we assume that if
                        // we parsing was successful it then it works as a
                        // locked modifier.
                        // TODO: Maybe in libxkbcommon they do something? I need
                        // a way to easily load libxkbcommon keyboards for
                        // testing. Our libxkbcommon viewer currently doesn't
                        // show LED states.

                    } else if (xkb_parser_match_tok_i (state, XKB_PARSER_TOKEN_IDENTIFIER, "base") ||
                               xkb_parser_match_tok_i (state, XKB_PARSER_TOKEN_IDENTIFIER, "latched") ||
                               xkb_parser_match_tok_i (state, XKB_PARSER_TOKEN_IDENTIFIER, "any") ||
                               xkb_parser_match_tok_i (state, XKB_PARSER_TOKEN_IDENTIFIER, "none")) {
                        // We don't support these kinds of matching... I haven't
                        // seen any of them bieng used, nor I really understand
                        // what they do.
                        xkb_parser_error_tok (state, "Unsupported modifier state '%s' inside indicator block.");
                    } else {
                        xkb_parser_error_tok (state, "Unknown modifier state '%s' inside indicator block.");
                    }

                    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";");

                } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "modifiers")) {
                    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "=");
                    xkb_parser_parse_modifier_mask (state, ";", &modifiers);

                } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "!")) {
                    // TODO: The clearLocks flag in action arguments uses ~ in a
                    // similar way as ! is used here, are they interchageable?,
                    // can we have a single parse_boolean_option() function?.
                    // :unify_boolean_options
                    xkb_parser_next (state);
                    if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "allowExplicit") ||
                        xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "drivesKbd") ||
                        xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "ledDrivesKbd") ||
                        xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "ledDrivesKkeyboard") ||
                        xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "indicatorDrivesKbd") ||
                        xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "indicatorDrivesKeyboard")) {
                        // Ignore
                    } else {
                        xkb_parser_error_tok (state, "Invalid boolean flag '%s' inside indicator block.");
                    }
                    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";");

                } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "allowExplicit") ||
                           xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "drivesKbd") ||
                           xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "ledDrivesKbd") ||
                           xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "ledDrivesKkeyboard") ||
                           xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "indicatorDrivesKbd") ||
                           xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "indicatorDrivesKeyboard")) {
                    // Ignore
                    // :unify_boolean_options
                    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";");

                } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "controls") ||
                           xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "groups")) {
                    // Ignore
                    ignore_indicator_block = true;
                    xkb_parser_skip_until_operator (state, ";");

                } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "}")) {
                    break;

                } else {
                    xkb_parser_error_tok (state, "Invalid statement '%s' inside interpret block.");
                }

            } while (!state->scnr.error);

            xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";");

            if (!ignore_indicator_block) {
                if (!state->scnr.error && modifiers == 0x0) {
                    xkb_parser_error (state, "Missing modifier statement in indicator block.");
                }

                if (!state->scnr.error &&
                    (ind_code < 1 || KEYBOARD_LAYOUT_MAX_LEDS < ind_code)) {
                    xkb_parser_error (state, "Invalid code %d for indicator '%s', must be in range 1-%d.",
                                      ind_code, ind_name, KEYBOARD_LAYOUT_MAX_LEDS);
                }

                if (!state->scnr.error &&
                    state->leds[ind_code] != 0x0) {
                    // NOTE: libxkbcommon doesn't fail when this happens,
                    // xkbcomp does. I think it's better to fail here, other
                    // behaviors would be confusing.
                    xkb_parser_error (state, "Indicator code %d already assigned.", ind_code);
                }

                if (!state->scnr.error) {
                    state->leds[ind_code] = modifiers;
                }
            }

        } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "}")) {
            break;

        } else {
            xkb_parser_error_tok (state, "Invalid statement '%s' in compatibility section.");
        }

    } while (!state->scnr.error && braces > 0);

    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";");

    state->scnr.eof_is_error = false;
}

void xkb_parser_symbol_list (struct xkb_parser_state_t *state,
                             xkb_keysym_t *symbols, int size,
                             int *num_symbols_found)
{
    assert (size == KEYBOARD_LAYOUT_MAX_LEVELS);
    assert (*num_symbols_found == 0);

    do {
        xkb_parser_next (state);
        xkb_keysym_t keysym;
        if (xkb_parser_match_keysym (state, &keysym)) {
            symbols[*num_symbols_found] = keysym;
            (*num_symbols_found)++;
        }

        // TODO: I think xkbcomp parses numbers grater than 9 as a keysym
        // value. This is very counterintuitive, do we want to support this?
        // maybe multicharacter keysyms are more useful.

        xkb_parser_next (state);
        if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "]")) {
            break;
        } else {
            xkb_parser_expect_tok (state, XKB_PARSER_TOKEN_OPERATOR, ",");
        }

    } while (!state->scnr.error && *num_symbols_found < size);
}

void xkb_parser_parse_symbols (struct xkb_parser_state_t *state)
{
    state->scnr.eof_is_error = true;
    xkb_parser_block_start (state, "xkb_symbols");

    do {
        xkb_parser_next (state);
        if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "key")) {
            xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_KEY_IDENTIFIER, NULL);
            int kc;
            if (!xkb_parser_key_identifier_lookup (state, str_data(&state->tok_value), &kc)) {
                xkb_parser_error_tok (state, "Undefined key identifier '%s'.");
            }

            xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "{");

            struct key_type_t *type = NULL;
            xkb_keysym_t symbols[KEYBOARD_LAYOUT_MAX_LEVELS];
            struct xkb_backend_key_action_t actions[KEYBOARD_LAYOUT_MAX_LEVELS];
            for (int i=0; i<KEYBOARD_LAYOUT_MAX_LEVELS; i++) {
                actions[i] = ZERO_INIT(struct xkb_backend_key_action_t);

                // :symbols_initialization
                symbols[i] = XKB_KEY_NoSymbol;
            }

            int num_symbols = 0;
            do {
                bool consumed_list_separator = false;

                xkb_parser_next (state);
                if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "[")) {
                    // This is a shorthand, the type will be guessed afterwards,
                    // and there are no actions set here.
                    xkb_parser_symbol_list (state, symbols, ARRAY_SIZE(symbols), &num_symbols);
                    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "}");
                    break;

                } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "type")) {
                    int group = 1;
                    xkb_parser_next (state);
                    if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "[")) {
                        xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_GROUP_IDENTIFIER, NULL);
                        group = state->tok_value_int;
                        xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "]");

                        xkb_parser_next (state);
                    }

                    xkb_parser_expect_tok (state, XKB_PARSER_TOKEN_OPERATOR, "=");
                    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_STRING, NULL);

                    // I don't think multiple groups will be supported unless
                    // there is a good usecase.
                    if (group == 1) {
                        type =
                            keyboard_layout_type_lookup (state->keymap, str_data(&state->tok_value));
                        if (type == NULL) {
                            xkb_parser_error_tok (state, "Unknown type '%s'.");
                        }
                    }

                } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "symbols")) {
                    int group = 1;
                    xkb_parser_next (state);
                    if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "[")) {
                        xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_GROUP_IDENTIFIER, NULL);
                        group = state->tok_value_int;
                        xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "]");

                        xkb_parser_next (state);
                    }

                    xkb_parser_expect_tok (state, XKB_PARSER_TOKEN_OPERATOR, "=");
                    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "[");
                    if (group == 1) {
                        xkb_parser_symbol_list (state, symbols, ARRAY_SIZE(symbols), &num_symbols);

                    } else {
                        // For now we only support a single group per key.
                        // :single_group_per_key
                        xkb_parser_skip_until_operator (state, "]");
                    }

                } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "actions")) {
                    // Maybe we should use this instead of the compat block. But
                    // I would need to lookup documentation for it because it's
                    // almost never used in freedesktop's keymap database.
                    // Inside /usr/share/X11/xkb/symbols this is only used
                    // inside 'shift', 'capslock' and 'level5'.
                    // TODO: I think loading a layout that includes any of the
                    // above symbols will show this data as compat declarations,
                    // but I'm not sure. Check we won't get these from xkbcomp.

                    int group = 1;
                    xkb_parser_next (state);
                    if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "[")) {
                        xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_GROUP_IDENTIFIER, NULL);
                        group = state->tok_value_int;
                        xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "]");

                        xkb_parser_next (state);
                    }

                    xkb_parser_expect_tok (state, XKB_PARSER_TOKEN_OPERATOR, "=");
                    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "[");
                    if (group == 1) {
                        // Parse actions list
                        int num_actions_found = 0;
                        do {
                            struct xkb_backend_key_action_t action = {0};
                            xkb_parser_parse_action (state, &action);
                            if (!state->scnr.error) {
                                actions[num_actions_found++] = action;
                            }

                            xkb_parser_next (state);
                            if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "]")) {
                                break;
                            } else {
                                xkb_parser_expect_tok (state, XKB_PARSER_TOKEN_OPERATOR, ",");
                            }

                        } while (!state->scnr.error && num_actions_found < ARRAY_SIZE(actions));

                    } else {
                        // :single_group_per_key
                        xkb_parser_skip_until_operator (state, "]");
                    }

                } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "vmods") ||
                           xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "virtualmodifiers") ||
                           xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "virtualmods")) {
                    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "=");
                    do {
                        key_modifier_mask_t vmod_mask = 0x0;

                        xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, NULL);
                        if (!xkb_parser_is_real_modifier (state, str_data(&state->tok_value))) {
                            enum modifier_result_status_t status = 0;
                            vmod_mask =
                                keyboard_layout_get_modifier (state->keymap, str_data(&state->tok_value), &status);

                            if (status == KEYBOARD_LAYOUT_MOD_UNDEFINED) {
                                vmod_mask =
                                    keyboard_layout_new_modifier (state->keymap, str_data(&state->tok_value), &status);
                                if (status == KEYBOARD_LAYOUT_MOD_MAX_LIMIT_REACHED) {
                                    // NOTE: This is not the actual XKB limit of 16, here we reached
                                    // the maximum possible of our internal representation
                                    // (currently 32).
                                    xkb_parser_error_tok (
                                        state,
                                        "Virtual modifier %s, can't be defined. Maximum number of modifiers reached.");

                                }
                            }

                        } else {
                            xkb_parser_error_tok (state, "Expected virtual modifier, got '%s'.");
                        }

                        if (!state->scnr.error) {
                            state->symbol_vmods[kc] |= vmod_mask;
                        }

                        xkb_parser_next (state);
                        if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "+")) {
                            continue;
                        } else {
                            consumed_list_separator = true;
                            break;
                        }
                    } while (!state->scnr.error);

                }

                // When parsing the virtualmodifiers field we will advance into
                // the list delimiter, in that case we don't want to advance
                // again here.
                // :parser_peek_function
                if (!consumed_list_separator) {
                    xkb_parser_next (state);
                }

                if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "}")) {
                    break;
                } else {
                    xkb_parser_expect_tok (state, XKB_PARSER_TOKEN_OPERATOR, ",");
                }

            } while (!state->scnr.error);

            xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";");

            // This ensures the only case type==NULL is when num_symbols==0,
            // then we will leave the type for the key unassigned.
            if (type == NULL && num_symbols > 0) {
                // The following logic comes from:
                //  <libxkbcommon_repo>/src/xkbcommon/xkbcomp/symbols.c:FindAutomaticType()
                if (num_symbols == 1) {
                    type = keyboard_layout_type_lookup (state->keymap, "ONE_LEVEL");

                } else if (num_symbols == 2) {
                    if (sym_is_lower(symbols[0]) && sym_is_upper(symbols[1])) {
                        type = keyboard_layout_type_lookup (state->keymap, "ALPHABETIC");

                    } else if (sym_is_keypad(symbols[0]) || sym_is_keypad(symbols[1])) {
                        type = keyboard_layout_type_lookup (state->keymap, "KEYPAD");

                    } else {
                        type = keyboard_layout_type_lookup (state->keymap, "TWO_LEVEL");
                    }

                } else if (num_symbols <= 4) {
                    if (sym_is_lower(symbols[0]) && sym_is_upper(symbols[1])) {
                        // NOTE: symbols[3] is safe because symbols was
                        // initialized to XKB_KEY_NoSymbol.
                        // :symbols_initialization
                        if (sym_is_lower(symbols[2]) && sym_is_upper(symbols[3])) {
                            type = keyboard_layout_type_lookup (state->keymap, "FOUR_LEVEL_ALPHABETIC");

                        } else {
                            type = keyboard_layout_type_lookup (state->keymap, "FOUR_LEVEL_SEMIALPHABETIC");
                        }

                    } else {
                        if (sym_is_keypad(symbols[0]) || sym_is_keypad(symbols[1])) {
                            type = keyboard_layout_type_lookup (state->keymap, "FOUR_LEVEL_KEYPAD");

                        } else {
                            type = keyboard_layout_type_lookup (state->keymap, "FOUR_LEVEL");
                        }
                    }

                } else {
                    // Looks like 'None' is set as type in this case, I still
                    // don't know what implications this would have. We do it
                    // differently so we always have a type.
                    type = keyboard_layout_type_lookup (state->keymap, "TWO_LEVEL");
                }
            }

            // If everything looks fine then create the new key and assign data
            // to each level.
            if (!state->scnr.error) {
                struct key_t *new_key =
                    keyboard_layout_new_key (state->keymap, kc, type);

                if (type != NULL) {
                    // If there are more declared symbols for the key than levels in
                    // the type we just ignore the extra symbols.
                    int num_levels = keyboard_layout_type_get_num_levels (type);
                    for (int i=0; i<num_levels; i++) {
                        keyboard_layout_key_set_level (new_key, i+1, symbols[i], NULL);

                        // We set the action of our internal represntation to
                        // NULL but store the resulting actions in this array in
                        // the state. After parsing is complete we will compute
                        // the effective actions between those in the
                        // compatibility sections and these explicit ones, then
                        // store the result in our internal representation.
                        // :symbol_actions_array
                        state->symbol_actions[kc][i] = actions[i];
                    }
                }
            }

        } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "modifier_map")) {
            int map_keycode = 0;
            key_modifier_mask_t map_modifier = 0;

            xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, NULL);

            if (!xkb_parser_is_real_modifier (state, str_data(&state->tok_value))) {
                xkb_parser_error_tok (state, "Expected a real modifier, got '%s'.");
            } else {
                map_modifier = xkb_parser_modifier_lookup (state, str_data(&state->tok_value));
            }

            xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "{");

            xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_KEY_IDENTIFIER, NULL);
            if (!xkb_parser_key_identifier_lookup (state, str_data(&state->tok_value), &map_keycode)) {
                xkb_parser_error_tok (state, "Undefined key identifier '%s'.");
            }
            if (state->modifier_map[map_keycode] != 0) {
                // Turns out some layouts in the database are buggy and do this.
                // What libxkbcommon does is print an error message and
                // overwrite the previous modifier_map value. We could comment
                // this out and get that behavior, but what should really happen
                // is that we fix those layouts. I'm not going to comment this
                // out for now, instead I will just not support layouts that
                // trigger this.
                xkb_parser_error_tok (state, "Keycode '%s' has already a modifier mapped to it.");
            }

            xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "}");
            xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";");

            if (!state->scnr.error) {
                state->modifier_map[map_keycode] = map_modifier;
            }

        } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "name")) {
            // Ignore name statement.
            xkb_parser_skip_until_operator (state, ";");

        } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "}")) {
            break;

        } else {
            xkb_parser_error (state, "Invalid statement in symbols section");
        }

    } while (!state->scnr.error);

    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";");

    state->scnr.eof_is_error = false;
}

// :predefined_real_modifiers
key_modifier_mask_t xkb_get_real_modifiers_mask (struct keyboard_layout_t *keymap)
{
    key_modifier_mask_t real_modifiers = 0;

    char *real_modifier_names[] = XKB_FILE_BACKEND_REAL_MODIFIER_NAMES_LIST;
    for (int i=0; i<ARRAY_SIZE(real_modifier_names); i++) {
        enum modifier_result_status_t status = 0;
        key_modifier_mask_t mask = keyboard_layout_get_modifier (keymap, real_modifier_names[i], &status);
        assert (status == KEYBOARD_LAYOUT_MOD_SUCCESS);
        real_modifiers |= mask;
    }

    return real_modifiers;
}

// Translate between the full xkb action and the one used by our internal
// representation.
static inline
struct key_action_t xkb_parser_translate_to_ir_action (struct xkb_backend_key_action_t *action,
                                                       key_modifier_mask_t mod_map)
{
    struct key_action_t ir_action = {0};

    // Map the unset type to no action
    if (action->type == XKB_BACKEND_KEY_ACTION_TYPE_NO_ACTION) {
        ir_action.type = KEY_ACTION_TYPE_NONE;
    } else {
        ir_action.type = action->type;
    }

    // Resolve the effective modifiers of the action.
    if (action->mod_map_mods) {
        ir_action.modifiers = mod_map;
    } else {
        ir_action.modifiers = action->modifiers;
    }

    return ir_action;
}

// Returns the more 'specific' interpret statement. If we consider them equal
// then we return the new one.
//
// I haven't read much about the formal definition of "specificity". I will make
// guesses and define what looks more specific to me. This could break some
// layouts, but I think doing this and then seeing what breaks and fixing it is
// the fastest thing to do.
//
// Also, I don't see all the functionality of interpret statements being used in
// the actual database, so probably this guesswork, plus some small fixes will
// allow us to cover all existing layouts.
struct xkb_compat_interpret_t*
xkb_backend_interpret_compare (struct xkb_compat_interpret_t *old, struct xkb_compat_interpret_t *new)
{
    struct xkb_compat_interpret_t *winner = new;
    if (old->any_keysym != new->any_keysym) {
        // An interpret that uses a keysym is more specific.
        winner = new->any_keysym ? old : new;

    } else {
        // Both interprets have any_keysym with the same value. Check the
        // modifier matching.

        if (old->all_real_modifiers != new->all_real_modifiers) {
            // An interpret that uses modifiers besides "All" is more specific.
            winner = new->all_real_modifiers ? old : new;

        } else {
            if (old->condition != new->condition) {
                // If interprets have different conditions, we choose the one
                // whith the more 'specific' condition. The order they were
                // defined in the enum defines their specificity.
                winner = old->condition < new->condition ? new : old;

            } else {
                // We consider these interprets equal. We leave the fallback
                // value.
                // NOTE: We could keep going. Maybe count which interpret has
                // more real modifiers?. I will stop here for now. The idea is
                // to not overcomplicate this so it can be easily changed in the
                // future, if it needs fixing.
            }
        }
    }

    return winner;
}

MOD_MASK_BINARY_TREE_FOREACH_CB(populate_vmod_map_foreach)
{
    key_modifier_mask_t mask = node->value;
    struct xkb_parser_state_t *state = (struct xkb_parser_state_t *)data;

    if (!xkb_parser_is_real_modifier (state, node->key)) {
        uint32_t idx = bit_mask_perfect_hash (mask);
        state->vmodmap[idx].name = node->key;
        state->vmodmap[idx].encoding = 0x0;
    }
}

struct interpret_vmod_definition_t {
    int kc;
    key_modifier_mask_t vmods;

    struct interpret_vmod_definition_t *next;
};

void add_interpret_vmod_definition (struct xkb_parser_state_t *state,
                                    struct interpret_vmod_definition_t **interpret_vmod_definition,
                                    struct interpret_vmod_definition_t **interpret_vmod_definition_end,
                                    int kc, key_modifier_mask_t vmod_mask)
{
    assert (interpret_vmod_definition != NULL);

    if ((*interpret_vmod_definition_end) == NULL) {
        *interpret_vmod_definition = mem_pool_push_struct (&state->pool, struct interpret_vmod_definition_t);
        **interpret_vmod_definition = ZERO_INIT (struct interpret_vmod_definition_t);

        (*interpret_vmod_definition_end) = *interpret_vmod_definition;
        (*interpret_vmod_definition_end)->kc = kc;
    }

    if ((*interpret_vmod_definition_end)->kc != kc) {
        struct interpret_vmod_definition_t *new_vmod_definition =
            mem_pool_push_struct (&state->pool, struct interpret_vmod_definition_t);
        *new_vmod_definition = ZERO_INIT (struct interpret_vmod_definition_t);

        (*interpret_vmod_definition_end)->next = new_vmod_definition;
        (*interpret_vmod_definition_end) = new_vmod_definition;
        (*interpret_vmod_definition_end)->kc = kc;
    }

    (*interpret_vmod_definition_end)->vmods |= vmod_mask;
}

MOD_MASK_BINARY_TREE_FOREACH_CB(reverse_mapping_create_foreach)
{
    char **reverse_modifier_definition = (char**)data;

    key_modifier_mask_t mask = node->value;
    if (mask != 0) {
        int pos = 0;
        while (!(mask&0x1) && pos < KEYBOARD_LAYOUT_MAX_MODIFIERS) {
            pos++;
            mask >>= 1;
        }

        if (reverse_modifier_definition[pos]) {
            printf ("Modfier mapping is not 1:1, keymap seems to be corrupted.\n");

        } else if (pos == KEYBOARD_LAYOUT_MAX_MODIFIERS) {
            printf ("Invalid modifier mask, keymap seems to be corrupted.\n");

        } else {
            reverse_modifier_definition[pos] = node->key;
        }
    }
    // else {
    // The 'none' modifier can't be represented with the current reverse mapping
    // data structure. We ignore it and later when writing masks we handle it as
    // a special case.
    // :none_modifier
    // }
}

//NOTE: This expects reverse_modifier_definition to have at least
//KEYBOARD_LAYOUT_MAX_MODIFIERS elements allocated.
void create_reverse_modifier_name_map (struct keyboard_layout_t *keymap, char **reverse_modifier_definition)
{
    for (int i=0; i<KEYBOARD_LAYOUT_MAX_MODIFIERS; i++) {
        reverse_modifier_definition[i] = NULL;
    }

    mod_mask_binary_tree_foreach (&keymap->modifiers, reverse_mapping_create_foreach, reverse_modifier_definition);
}

// DELETE ME {
void xkb_file_write_modifier_mask_reverse (char **reverse_modifier_definition, string_t *str, key_modifier_mask_t mask)
{
    int bit_pos = 0;
    if (mask == 0) {
        // The current representation used for the reverse map does not allow a
        // representation for the 'none' keyboard mask.
        // :none_modifier
        str_cat_c (str, "none");

    } else {
        while (mask != 0) {
            if (mask & 0x1) {
                str_cat_c (str, reverse_modifier_definition[bit_pos]);

                if (mask>>1 != 0) {
                    str_cat_c (str, " + ");
                }
            }

            bit_pos++;
            mask >>= 1;
        }
    }
}
// }

// NOTE: This assumes vmodmap has at least KEYBOARD_LAYOUT_MAX_MODIFIERS
// elements and that elements are mapped by the result of bit_mask_perfect_hash
// applied to the virtual modifier.
key_modifier_mask_t remove_vmods (struct vmodmap_element_t *vmodmap,
                                  key_modifier_mask_t real_modifiers, key_modifier_mask_t mask)
{
    key_modifier_mask_t only_real_modifiers = 0x0;
    key_modifier_mask_t action_mods = mask;
    while (action_mods) {
        key_modifier_mask_t next_bit_mask = action_mods & -action_mods;

        if (real_modifiers & next_bit_mask) {
            only_real_modifiers |= next_bit_mask;

        } else {
            uint32_t idx = bit_mask_perfect_hash (next_bit_mask);
            if (vmodmap[idx].name != NULL && vmodmap[idx].encoding) {
                only_real_modifiers |= vmodmap[idx].encoding;
            }
        }

        action_mods = action_mods & (action_mods-1);
    }

    return only_real_modifiers;
}

void xkb_parser_simplify_layout (struct xkb_parser_state_t *state, string_t *vmod_map_log)
{
    struct xkb_compat_t *compatibility = &state->compatibility;

    key_modifier_mask_t real_modifiers = xkb_get_real_modifiers_mask (state->keymap);

    // This linked list will be populated by the :compute_winning_interprets
    // algorithm, so that later the one that computes virtual modifier
    // definitions (:virtual_modifier_definition) uses it.
    struct interpret_vmod_definition_t *interpret_vmod_definition = NULL;
    struct interpret_vmod_definition_t *interpret_vmod_definition_end = NULL;

    //////////////////////////////////////////////////////////////////////
    // Compute winning interprets and resolve key level actions from them.
    // :compute_winning_interprets
    //
    // NOTE: It's important that keycodes are iterated in ascending order.
    // The next algorithm :virtual_modifier_definition and
    // add_interpret_vmod_definition() assume things work this way.
    for (int kc=0; kc<KEY_CNT; kc++) {
        struct key_t *curr_key = state->keymap->keys[kc];

        if (curr_key != NULL) {
            // Here we decide wich levels of the key have not been set
            // explicitly in the symbols section. These levels may be then set
            // by a matching interpret statement.
            int num_unset_levels = 0;
            int unset_levels[KEYBOARD_LAYOUT_MAX_LEVELS];
            int num_levels = keyboard_layout_type_get_num_levels (curr_key->type);
            for (int j=0; j<num_levels; j++) {
                if (state->symbol_actions[kc][j].type == XKB_BACKEND_KEY_ACTION_TYPE_UNSET) {
                    unset_levels[num_unset_levels++] = j;

                } else {
                    // End resolution of actions set explicitly in the symbols
                    // section. Translate them into into the real actions in the
                    // internal representation.
                    //
                    // We can do this here before resolving interpret statements
                    // because explicit actions will always override them.
                    // :symbol_actions_array
                    curr_key->levels[j].action =
                        xkb_parser_translate_to_ir_action (&state->symbol_actions[kc][j],
                                                           state->modifier_map[kc]);
                }
            }

            if (num_unset_levels > 0 && compatibility->interprets != NULL) {
                // This array contains the winning interpret statement for each of
                // the levels that may be modified according to the unset_levels
                // array. Although this is of size KEYBOARD_LAYOUT_MAX_LEVELS only
                // the first num_unset_levels values are used. A winning interpret
                // here corresponds to the level of the content of unset_levels at
                // the same index.
                struct xkb_compat_interpret_t *winning_interpret[KEYBOARD_LAYOUT_MAX_LEVELS];
                for (int i=0; i<num_unset_levels; i++) {
                    winning_interpret[i] = NULL;
                }

                // Iterate over all interpret statements and update the winning
                // one for each one of the unset levels.
                struct xkb_compat_interpret_t *curr_interpret = compatibility->interprets;
                while (curr_interpret != NULL) {
                    // Of the levels in the unset_levels array, set a boolean flag
                    // in the keysym_match array if this interpret may affect it.
                    // Same as with the winning_interpret array above, only the
                    // first num_unset_levels are important.
                    bool keysym_match[KEYBOARD_LAYOUT_MAX_LEVELS];
                    for (int i=0; i<num_unset_levels; i++) {
                        keysym_match[i] = false;
                    }

                    for (int j=0; j<num_unset_levels; j++) {
                        int curr_level = unset_levels[j];

                        // NOTE: Looks like NoSymbol doesn't match any interpret
                        // statements, not even when using the 'Any' keysym.
                        if (curr_key->levels[curr_level].keysym != 0x0 /*NoSymbol*/ &&
                            (curr_interpret->any_keysym ||
                             curr_interpret->keysym == curr_key->levels[curr_level].keysym)) {
                            keysym_match[j] = true;
                            // we can't break here because we want to know all
                            // levels that match.
                        }
                    }

                    // Determine if interpret's modifiers match.
                    //
                    // I didn't get these interpretations from the actual
                    // sourcecode for xkb. Instead this is my interpretation of
                    // the descriptions given in [1]. I added them as comments
                    // for reference.
                    //
                    // [1] http://pascal.tsu.ru/en/xkb/gram-compat.html

                    // To handle the 'All' modifiers value inside conditions we
                    // set the modifiers used here to a mask that contains all
                    // of them and was computed at the start of the function.
                    key_modifier_mask_t interpret_modifiers =
                        curr_interpret->all_real_modifiers ? real_modifiers : curr_interpret->real_modifiers;

                    bool modifiers_match = false;
                    if (curr_interpret->condition == COMPAT_CONDITION_ANY_OF_OR_NONE) {
                        // "Actually means that the real modifiers field doesn't
                        // make sense; this condition means that the keycode can
                        // have any of modifiers specified in the interpretation
                        // or none of them so it always is true." [1]
                        modifiers_match = true;

                    } else if (curr_interpret->condition == COMPAT_CONDITION_NONE_OF) {
                        // "Keycode must have no one of specified modifiers." [1]
                        if (~(state->modifier_map[kc] & interpret_modifiers)) {
                            modifiers_match = true;
                        }

                    } else if (curr_interpret->condition == COMPAT_CONDITION_ANY_OF) {
                        // "Keycode must have at least one of specified modifiers." [1]
                        if ((state->modifier_map[kc] & interpret_modifiers)) {
                            modifiers_match = true;
                        }

                    } else if (curr_interpret->condition == COMPAT_CONDITION_ALL_OF) {
                        // "Keycode must have all specified modifiers." [1]
                        //
                        // This condition seems to imply that a keycode can have
                        // multiple modifiers assigned to it, but I don't see
                        // how this is possible. Maybe virtual modifiers play a
                        // role here?.
                        if ((state->modifier_map[kc] & interpret_modifiers) == interpret_modifiers) {
                            modifiers_match = true;
                        }

                    } else if (curr_interpret->condition == COMPAT_CONDITION_EXACTLY) {
                        // "Similar to [the AllOf condition]; keycode must have
                        // all specified modifiers but must have no one of other
                        // modifiers." [1]
                        if (state->modifier_map[kc] == interpret_modifiers) {
                            modifiers_match = true;
                        }

                    } else {
                        invalid_code_path;
                    }

                    // This matches per key
                    // TODO: We can make things faster by not computing keysym
                    // matches if modifiers don't match.
                    if (modifiers_match) {
                        for (int j=0; j<num_unset_levels; j++) {
                            // Different than modifier matching, keysym matching
                            // matches per level.
                            if (keysym_match[j]) {
                                if (winning_interpret[j] != NULL) {
                                    // Choose the more "specific" interpret statement as
                                    // the winner for this level.
                                    winning_interpret[j] =
                                        xkb_backend_interpret_compare(winning_interpret[j],
                                                                      curr_interpret);

                                } else {
                                    winning_interpret[j] = curr_interpret;
                                }
                            }

                        }
                    }

                    curr_interpret = curr_interpret->next;
                }

                // Set the winning interpret actions into our internal
                // representation for curr_key.
                for (int j=0; j<num_unset_levels; j++) {
                    int curr_level = unset_levels[j];

                    if (winning_interpret[j] != NULL) {
                        curr_key->levels[curr_level].action =
                            xkb_parser_translate_to_ir_action (&winning_interpret[j]->action,
                                                               state->modifier_map[kc]);

                        // Build the data structure required for virtual
                        // modifier definition computation.
                        // :virtual_modifier_definition
                        add_interpret_vmod_definition (
                            state,
                            &interpret_vmod_definition,
                            &interpret_vmod_definition_end,
                            kc, winning_interpret[j]->virtual_modifier);
                    }
                }
            }
        }
    }

    ////////////////////////////////////////////////////
    // Compute the definitions of all virtual modifiers.
    // :virtual_modifier_definition

    // Initialize state->vmodmap from the definitions currently in the keymap.
    mod_mask_binary_tree_foreach (&state->keymap->modifiers, populate_vmod_map_foreach, state);

    struct interpret_vmod_definition_t *curr_interpret_vmod_definition = interpret_vmod_definition;
    for (int kc=0; kc<KEY_CNT; kc++) {
        struct key_t *curr_key = state->keymap->keys[kc];

        if (curr_key != NULL) {
            // Decide where we are going to look for virtual modifier
            // definitions. The symbols section definition overrides everything
            // if there is a 'vmods' statement, or there is an 'actions'
            // statement.
            bool symbols_vmod_override = false;
            if (state->symbol_vmods[kc] != 0x0) {
                symbols_vmod_override = true;
            } else {
                int num_levels = keyboard_layout_type_get_num_levels (curr_key->type);
                for (int j=0; j<num_levels; j++) {
                    if (state->symbol_actions[kc][j].type != XKB_BACKEND_KEY_ACTION_TYPE_UNSET) {
                        symbols_vmod_override = true;
                        break;
                    }
                }
            }

            // Have key_mods contain the virtual modifiers that the mapping of
            // this key (kc) will define.
            key_modifier_mask_t key_vmods = 0x0;
            if (symbols_vmod_override) {
                key_vmods = state->symbol_vmods[kc];
            } else {
                if (curr_interpret_vmod_definition && curr_interpret_vmod_definition->kc == kc) {
                    key_vmods = curr_interpret_vmod_definition->vmods;
                    curr_interpret_vmod_definition = curr_interpret_vmod_definition->next;
                }
            }

            // Iterate bits of key_vmods, lookup the element in state->vmodmap
            // corresponding to each bit, then set the real modifier mapped to
            // kc in its definition.
            while (key_vmods) {
                if (state->modifier_map[kc]) {
                    key_modifier_mask_t next_bit_mask = key_vmods & -key_vmods;
                    uint32_t idx = bit_mask_perfect_hash (next_bit_mask);
                    assert (single_bit_set(state->modifier_map[kc]));
                    state->vmodmap[idx].encoding |= state->modifier_map[kc];
                }

                key_vmods = key_vmods & (key_vmods-1);
            }
        }
    }

    // If the caller wants to see the virtual modifier encoding, print it.
    if (vmod_map_log != NULL) {
        char *reverse_modifier_name_map[KEYBOARD_LAYOUT_MAX_MODIFIERS];
        create_reverse_modifier_name_map (state->keymap, reverse_modifier_name_map);

        str_cat_c (vmod_map_log, "Virtual modifier encoding:\n");

        for (int i=0; i<KEYBOARD_LAYOUT_MAX_MODIFIERS; i++) {
            if (state->vmodmap[i].name != NULL && state->vmodmap[i].encoding) {
                str_cat_printf (vmod_map_log, " %s: ", state->vmodmap[i].name);

                string_t str = {0};
                xkb_file_write_modifier_mask_reverse (reverse_modifier_name_map, &str, state->vmodmap[i].encoding);
                str_cat_printf (vmod_map_log, "%s\n", str_data(&str));
                str_free (&str);
            }
        }
    }

    // Transform modifier masks in actions to masks with only real modifiers.
    for (int kc=0; kc<KEY_CNT; kc++) {
        struct key_t *curr_key = state->keymap->keys[kc];

        if (curr_key != NULL) {
            int num_levels = keyboard_layout_type_get_num_levels (curr_key->type);
            for (int j=0; j<num_levels; j++) {
                curr_key->levels[j].action.modifiers =
                    remove_vmods (state->vmodmap, real_modifiers, curr_key->levels[j].action.modifiers);
            }
        }
    }

    // Translate all modifier masks in types to masks with only real modifiers.
    struct key_type_t *curr_type = state->keymap->types;
    while (curr_type) {
        curr_type->modifier_mask = remove_vmods (state->vmodmap, real_modifiers, curr_type->modifier_mask);

        struct level_modifier_mapping_t *curr_modifier_mapping = curr_type->modifier_mappings;
        while (curr_modifier_mapping) {
            curr_modifier_mapping->modifiers =
                remove_vmods (state->vmodmap, real_modifiers, curr_modifier_mapping->modifiers);

            curr_modifier_mapping = curr_modifier_mapping->next;
        }
        curr_type = curr_type->next;
    }

    for (int i=0; i<KEYBOARD_LAYOUT_MAX_LEDS; i++) {
        key_modifier_mask_t modifiers =
            remove_vmods (state->vmodmap, real_modifiers, state->leds[i]);

        if (modifiers != 0x0) {
            keyboard_layout_new_led (state->keymap, i, modifiers);
        }
    }
}

// TODO: At some point this seemed useful to be put into commpon.h, I'm not sure
// anymore. Isn't it better in these cases to create a linked list and after
// we're done adding stuff create the static array?.
// NOTE: This must be zero initialized
struct ptrarr_t {
    void **data;
    size_t len;
    size_t size;
};

bool ptrarr_push (struct ptrarr_t *arr, void *ptr)
{
    if (arr->size == 0) {
        arr->size = 10;
        arr->data = malloc (sizeof(void*)*arr->size);

    } else if (arr->len == arr->size-1) {
        void *tmp = realloc (arr->data, sizeof(void*)*arr->size*2);
        if (tmp == NULL) {
            printf ("Realloc failed\n");
            return false;
        } else {
            arr->data = tmp;
            arr->size *= 2;
        }
    }

    arr->data[arr->len] = ptr;
    arr->len++;
    return true;
}

bool ptrarr_free (struct ptrarr_t *arr)
{
    free (arr->data);
    return true;
}

void scan_metadata_value (mem_pool_t *pool, struct scanner_t *scnr, char **val)
{
    assert (val != NULL);

    scanner_consume_spaces(scnr);
    if (scanner_char(scnr, ':')) {
        scanner_consume_spaces (scnr);
        char *start = scnr->pos;
        scanner_to_char (scnr, '\n');
        *val = pom_strndup (pool, start, scnr->pos - start - 1);
    }
}

// This parses a subset of the xkb file syntax into our internal representation
// keyboard_layout_t. We only care about parsing resolved layouts as returned by
// xkbcomp. Notable differences from a full xkb compiler are the lack of include
// statements and a more strict ordering of things.
// TODO: Use status_t here.
bool xkb_file_parse_verbose (char *xkb_str, struct keyboard_layout_t *keymap, string_t *log)
{
    struct xkb_parser_state_t state = {0};
    {
        state.scnr.pos = xkb_str;
        state.keymap = keymap;

        char *real_modifiers[] = XKB_FILE_BACKEND_REAL_MODIFIER_NAMES_LIST;
        state.real_modifiers = real_modifiers;
        state.real_modifiers_len = ARRAY_SIZE(real_modifiers);

        // Here we predefine all 8 real modifiers so that our parser always assigns
        // them the same modifier mask. This is useful because all our layouts will
        // only have real modifiers, then doing this ensures that things get printed
        // in the same order every time. Also, we don't need to check if a real
        // modifier is defined, it will always be. The only reason not to do this
        // was because of the modifier limit of 16 in XKB, it was possible that the
        // layout got to this limit by defining lots of virtual modifiers. This
        // doesn't happen now because our IR supports 32 modifiers and none of the
        // base layouts tries to define this many. In fact, layouts created by out
        // writer will have at most 8 modifiers because we only support real
        // modifiers.
        // :predefined_real_modifiers
        for (int i=0; i<ARRAY_SIZE(real_modifiers); i++) {
            enum modifier_result_status_t status = 0;
            keyboard_layout_new_modifier (keymap, real_modifiers[i], &status);
            assert (status == KEYBOARD_LAYOUT_MOD_SUCCESS);
        }
    }


    // Parse metadata comments
    // NOTE: In general we will ignore all the comments in the tokenizer
    // xkb_parser_next() here we use th scanner directly a little bit to get the
    // information not stored in xkb.
    {
        struct scanner_t metadata_scaner = {0};
        metadata_scaner.pos = xkb_str;

        struct scanner_t *scnr = &metadata_scaner;
        struct keyboard_layout_info_t *info = &keymap->info;
        scnr->eof_is_error = true;
        while (scanner_str (scnr, "//")) {
            scanner_consume_spaces (scnr);

            if (scanner_strcase (scnr, "name")) {
                scan_metadata_value (&keymap->pool,scnr, &info->name);

            } else if (scanner_strcase (scnr, "description")) {
                scan_metadata_value (&keymap->pool,scnr, &info->description);

            } else if (scanner_strcase (scnr, "short description")) {
                scan_metadata_value (&keymap->pool,scnr, &info->short_description);

            } else if (scanner_strcase (scnr, "languages")) {
                // TODO: Check we actually get iso639 codes. Debian has them in
                // /usr/share/iso-codes/json/ or /usr/share/xml/iso-codes/, the
                // XML verison seems to be deprecated.
                scanner_consume_spaces(scnr);
                if (scanner_char(scnr, ':')) {
                    struct ptrarr_t languages = {0};
                    while (*scnr->pos != '\n') {
                        scanner_consume_spaces (scnr);
                        char *start = scnr->pos;
                        scanner_to_any_char (scnr, ",\n");

                        info->num_languages++;
                        char* lang = pom_strndup (&keymap->pool, start, scnr->pos - start - 1);
                        ptrarr_push (&languages, lang);
                    }

                    info->languages = pom_push_size (&keymap->pool, sizeof(char*)*info->num_languages);
                    int i;
                    for (i=0; i<info->num_languages; i++) {
                        info->languages[i] = languages.data[i];
                    }
                    ptrarr_free (&languages);
                }
            }
        }
        scnr->eof_is_error = false;
    }

    xkb_parser_consume_tok (&state, XKB_PARSER_TOKEN_IDENTIFIER, "xkb_keymap");
    xkb_parser_consume_tok (&state, XKB_PARSER_TOKEN_OPERATOR, "{");

    xkb_parser_parse_keycodes (&state);
    xkb_parser_parse_types (&state);
    xkb_parser_parse_compat (&state);
    xkb_parser_parse_symbols (&state);

    // Skip the geometry block if there is one otherwise parse the end of the
    // keymap block.
    // TODO: We don't just call xkb_parser_skip_block (&state, "xkb_geometry")
    // because this saction may or may not be here, so we don't requiere it
    // here, but if it is there then we want to skip it. If we could peek ahead
    // then we could use that and make this much more concise. For now I just
    // took the xkb_parser_block_start() and xkb_parser_skip_block() functions
    // and copied them here.
    // :parser_peek_function
    xkb_parser_next (&state);
    if (xkb_parser_match_tok (&state, XKB_PARSER_TOKEN_IDENTIFIER, "xkb_geometry")) {
        xkb_parser_consume_tok (&state, XKB_PARSER_TOKEN_STRING, NULL);
        xkb_parser_consume_tok (&state, XKB_PARSER_TOKEN_OPERATOR, "{");

        // Skip the content of the block
        int braces = 1;
        do {
            xkb_parser_next (&state);
            if (xkb_parser_match_tok (&state, XKB_PARSER_TOKEN_OPERATOR, "{")) {
                braces++;
            } else if (xkb_parser_match_tok (&state, XKB_PARSER_TOKEN_OPERATOR, "}")) {
                braces--;
            }
        } while (!state.scnr.is_eof && !state.scnr.error && braces > 0);
        xkb_parser_consume_tok (&state, XKB_PARSER_TOKEN_OPERATOR, ";");

    } else if (xkb_parser_match_tok (&state, XKB_PARSER_TOKEN_OPERATOR, "}")) {
        xkb_parser_consume_tok (&state, XKB_PARSER_TOKEN_OPERATOR, ";");
        // TODO: Make sure we reach EOF here?

    } else {
        xkb_parser_error_tok (&state, "Expected geometry block or the end of the keymap, got %s.");
    }

    bool success = true;
    if (state.scnr.error) {
        if (log != NULL) {
            str_cat_printf (log,
                            "%d: %s %s\n",
                            state.scnr.line_number+1, ECMA_RED("error:"), state.scnr.error_message);
        }
        success = false;

    } else {
        // Here we translate the compatibility section's actions into actions
        // that are stored the way symbols are stored.
        xkb_parser_simplify_layout (&state, log);

        // Make a validation of the successfully parsed layout. A layout can be
        // valid syntactically but have issues semantically.
        success = keyboard_layout_is_valid (keymap, NULL);
    }

    xkb_parser_state_destory (&state);

    // TODO: This parser does not care if some elements in the internal
    // representation are unused. We will maybe need to add functionality into
    // keyboard_layout.c that compacts layout elements. For example if the xkb
    // file defines 16 modifiers but only uses 10 of those, we don't want to
    // tell the user there are no modifiers left if they try to define a new
    // one. This will also become an issue when we start creating functions that
    // remove IR components like keyboard_layout_remove_type(). I don't like the
    // idea of traversing the full IR every time a change happens, I like more
    // the option of letting things be unused, then if we reach a limit we call
    // a single compact function, if that still leaves no space for what the
    // user wants we show an error. This will centralize the complexity of
    // compaction in a single place and not across all IR state modification
    // functions, and will also run this maybe expensive computation less often.
    // :keyboard_layout_compact
    keyboard_layout_compact (keymap);

    return success;
}
bool xkb_file_parse(char *xkb_str, struct keyboard_layout_t *keymap)
{
    return xkb_file_parse_verbose(xkb_str,keymap,NULL);
}

struct modifier_map_element_t {
    xkb_keycode_t kc;
    // Mask that combines all used modifiers in the key
    key_modifier_mask_t key_modifiers;

    // Bit position of the assigned real modifier
    bool mapped;
    int real_modifier;
    struct modifier_map_element_t *next;
};

struct xkb_writer_state_t {
    mem_pool_t pool;

    // An array of modifier names indexed by the bit position of the mask. This
    // is the inverse of the mapping in the internal representation that maps
    // names to modifier masks in the internal representation.
    // NOTE: This will point into the internal representation, which is fine
    // because we won't destroy the keymap representation inside xkb_file_write.
    // TODO: Maybe change this for a tree so we can represent the 'none'
    // modifier mask?
    // :none_modifier
    char *reverse_modifier_definition[KEYBOARD_LAYOUT_MAX_MODIFIERS];

    // OR of all real modifiers in XKB
    key_modifier_mask_t real_modifiers;
};

void xkb_file_write_modifier_mask (struct xkb_writer_state_t *state, string_t *str, key_modifier_mask_t mask)
{
    int bit_pos = 0;
    if (mask == 0) {
        // The current representation used for the reverse map does not allow a
        // representation for the 'none' keyboard mask.
        // :none_modifier
        str_cat_c (str, "none");

    } else {
        while (mask != 0) {
            if (mask & 0x1) {
                str_cat_c (str, state->reverse_modifier_definition[bit_pos]);

                if (mask>>1 != 0) {
                    str_cat_c (str, " + ");
                }
            }

            bit_pos++;
            mask >>= 1;
        }
    }
}

struct print_modifiers_foreach_clsr_t {
    struct xkb_writer_state_t *state;
    string_t *xkb_str;
    bool is_first;
};

MOD_MASK_BINARY_TREE_FOREACH_CB(print_modifiers_foreach)
{
    struct print_modifiers_foreach_clsr_t *clsr =
        (struct print_modifiers_foreach_clsr_t*)data;

    key_modifier_mask_t mask = node->value;

    // Print only virtual modifiers
    if (!(clsr->state->real_modifiers & mask)) {
        if (clsr->is_first == true) {
            clsr->is_first = false;
        } else {
            str_cat_c (clsr->xkb_str, ",");
        }

        str_cat_c (clsr->xkb_str, node->key);
    }
}

void xkb_file_write_modifier_action_arguments (struct xkb_writer_state_t *state, string_t *xkb_str,
                                               struct key_action_t *action)
{
    str_cat_c (xkb_str, "modifiers=");
    xkb_file_write_modifier_mask (state, xkb_str, action->modifiers);
}

// TODO: The index of these names represents the mapping I have seen used in
// Linux, is there a place in the kernel where we can get these names?. If there
// is, we should autogenerate this array, like we do with kernel_keycode_names.
char *indicator_names[] = {
    "Caps Lock",
    "Num Lock",
    "Scroll Lock",
    "Compose",
    "Kana",
    "Sleep",
    "Suspend",
    "Mute",
    "Misc",
    "Mail",
    "Charging",
    "Shift Lock",
    "Group 2",
    "Mouse Keys"
};

char* get_writer_keycode_name (int kc)
{
    char *res = "";

    if (xkb_keycode_names[kc] != NULL) {
        res = xkb_keycode_names[kc];

    } else if (kernel_keycode_names[kc]) {
        res = kernel_keycode_names[kc];
    }

    return res;
}

// As far as I've been able to understand, the keycode section is basically
// useless. It's only purpose is to assign more semantically meaningful names to
// keycodes. There is no list of specific key identifiers, instead the keymap
// database is the one that attempts to provide meaning to them. As far as I
// can tell any 0 to 4 sequence of alphanumeric characters can be a key
// identifier. We define keycode names in our output by the name used in the
// default keycode section of an xkb file, see xkb_keycode_names.h. As a
// fallback if there is no name in this list, we use the kernel symbol name but
// this may cause problems because they are longer thatn 4 characters. Hopefully
// this doesn't happen?...
//
// I previously tried to name keycodes by their kernel keycode (keycode KEY_ESC
// will be <1>). This worked fine with the 4 character limit because the kernel
// currently expects a maximum of 768 keycode macros, and I don't think they
// will go beyond 999. The problem with this approach is that debugging the
// output of the writer and comparing it to the original xkb file becomes
// cumbersome. Also, I'm not sure this section is actually taken into account by
// all the code in the operating system's stack that handles layouts.
//
// In general it would be better if the xkb file format would use the kernel
// symbol names. We would be relying on these macros never changing in the
// future (which I think is a safe assumption to make about the kernel
// development team).
void xkb_file_write_keycodes (struct xkb_writer_state_t *state,
                              struct keyboard_layout_t *keymap,
                              string_t *xkb_str)
{
    str_cat_c (xkb_str, "xkb_keycodes \"keys_k\" {\n");
    str_cat_c (xkb_str, "    minimum = 8;\n");
    str_cat_c (xkb_str, "    maximum = 255;\n");

    // TODO: This could be faster if keys were in a linked list. But more cache
    // unfriendly?. If it ever becomes an issue, see which one is better.
    int i=0;
    for (i=0; i<KEY_CNT; i++) {
        if (keymap->keys[i] != NULL) {
            str_cat_printf (xkb_str, "    <%s> = %d", get_writer_keycode_name(i), i+8);

            if (kernel_keycode_names[i] != NULL) {
                str_cat_printf (xkb_str, "; // %s\n", kernel_keycode_names[i]);

            } else {
                str_cat_c (xkb_str, ";\n");
            }
        }
    }

    // Print led definitions
    str_cat_c (xkb_str, "\n");
    for (int i=0; i<KEYBOARD_LAYOUT_MAX_LEDS; i++) {
        if (keymap->leds[i] != 0x0) {
            str_cat_printf (xkb_str, "    indicator %d = \"%s\";\n", i+1, indicator_names[i]);
        }
    }
    str_cat_c (xkb_str, "};\n"); // end of keycodes section
}

void xkb_file_write_types (struct xkb_writer_state_t *state,
                           struct keyboard_layout_t *keymap,
                           string_t *xkb_str)
{
    str_cat_c (xkb_str, "xkb_types \"keys_t\" {\n");

    str_cat_c (xkb_str, "    virtual_modifiers ");
    // NOTE: We print modifier definitions from the internal representation's
    // tree and not from the reverse mapping because we want them in alphabetic
    // order so their ordering does not depend on the value of the mask assigned
    // to it.
    struct print_modifiers_foreach_clsr_t clsr = {0};
    clsr.is_first = true;
    clsr.xkb_str = xkb_str;
    clsr.state = state;
    mod_mask_binary_tree_foreach (&keymap->modifiers, print_modifiers_foreach, &clsr);
    str_cat_c (xkb_str, ";\n\n");

    struct key_type_t *curr_type = keymap->types;
    while (curr_type != NULL) {
        str_cat_printf (xkb_str, "    type \"%s\" {\n", str_data(&curr_type->name));
        str_cat_c (xkb_str, "        modifiers = ");
        xkb_file_write_modifier_mask (state, xkb_str, curr_type->modifier_mask);
        str_cat_c (xkb_str, ";\n");

        struct level_modifier_mapping_t *curr_modifier_mapping = curr_type->modifier_mappings;
        while (curr_modifier_mapping != NULL) {
            // We don't parse types that assign the 'none' modifier to a level
            // different than 1 (see :none_mapping_is_reserved_for_level1).
            // Still, it's possible that we get a modifier mapping with
            // 'none' as modifier here and not be level 1. This happens if the
            // original keymap had a virtual modifier and that virtual modifier
            // was never defined so it maps to the 'none' modifier. We skip
            // those cases here in the writer.
            if (curr_modifier_mapping->level == 1 || curr_modifier_mapping->modifiers != 0x0) {
                str_cat_c (xkb_str, "        map[");
                xkb_file_write_modifier_mask (state, xkb_str, curr_modifier_mapping->modifiers);
                str_cat_printf (xkb_str, "] = Level%d;\n", curr_modifier_mapping->level);
            }

            curr_modifier_mapping = curr_modifier_mapping->next;
        }

        // According to some documentation level names are required. When
        // testing it looks like xkbcomp only checks there is at least one name,
        // it doesn't check all mapped levels have a name. In any case, we
        // create generic names for all of them. Maybe in the future let the
        // user name them?.
        int num_levels = keyboard_layout_type_get_num_levels (curr_type);
        for (int i=0; i<num_levels; i++) {
            str_cat_printf (xkb_str, "        level_name[Level%d] = \"Level %d\";\n", i+1, i+1);
        }
        str_cat_c (xkb_str, "    };\n");

        curr_type = curr_type->next;
    }
    str_cat_c (xkb_str, "};\n");
}

void xkb_file_write_compat (struct xkb_writer_state_t *state,
                            struct keyboard_layout_t *keymap,
                            string_t *xkb_str)
{
    str_cat_c (xkb_str, "xkb_compatibility \"keys_c\" {\n");
    for (int i=0; i<KEYBOARD_LAYOUT_MAX_LEDS; i++) {
        if (keymap->leds[i] != 0x0) {
            str_cat_printf (xkb_str, "    indicator \"%s\" {\n", indicator_names[i]);

            str_cat_c (xkb_str, "        !allowExplicit;\n");
            str_cat_c (xkb_str, "        modifiers = ");
            xkb_file_write_modifier_mask (state, xkb_str, keymap->leds[i]);
            str_cat_c (xkb_str, ";\n");
            str_cat_c (xkb_str, "        whichModState = locked;\n");

            str_cat_c (xkb_str, "    };\n");
        }
    }
    str_cat_c (xkb_str, "};\n");
}

void xkb_file_write_symbols (struct xkb_writer_state_t *state,
                             struct keyboard_layout_t *keymap,
                             string_t *xkb_str,
                             bool use_action_statements)
{
    str_cat_c (xkb_str, "xkb_symbols \"keys_s\" {\n");
    for (int i=0; i<KEY_CNT; i++) {
        struct key_t *curr_key = keymap->keys[i];
        if (curr_key != NULL) {
            int num_levels = keyboard_layout_type_get_num_levels (curr_key->type);

            str_cat_printf (xkb_str, "    key <%s> {\n", get_writer_keycode_name(i));

            str_cat_printf (xkb_str, "        type[Group1]= \"%s\",\n", str_data(&curr_key->type->name));

            str_cat_c (xkb_str, "        symbols[Group1]= [ ");
            for (int j=0; j<num_levels; j++) {
                char keysym_name[64];
                xkb_keysym_get_name (curr_key->levels[j].keysym, keysym_name, ARRAY_SIZE(keysym_name));
                str_cat_printf (xkb_str, "%s", keysym_name);

                if (j < num_levels-1) {
                    str_cat_c (xkb_str, ", ");
                }
            }
            str_cat_c (xkb_str, " ]");

            // NOTE: Looks like in elementary at least, installing a symbols
            // component where a key has all actions as NoAction() makes it not
            // produce any symbol. I don't think this is the expected behavior
            // of the XKB file format. Also, just having any actions statement
            // in the symbols section causes VERY weird behaviors (for example
            // pressing space opens the applications menu). Which is why we now
            // let the caller decide if they want actions here or not.
            // :actions_in_symbols_cause_problems
            if (use_action_statements) {
                str_cat_c (xkb_str, ",\n");
                str_cat_c (xkb_str, "        actions[Group1]= [ ");
                for (int j=0; j<num_levels; j++) {
                    struct key_action_t *action = &curr_key->levels[j].action;

                    if (action->type == KEY_ACTION_TYPE_MOD_SET) {
                        str_cat_printf (xkb_str, "SetMods(");
                        xkb_file_write_modifier_action_arguments (state, xkb_str, action);

                    } else if (action->type == KEY_ACTION_TYPE_MOD_LATCH) {
                        str_cat_printf (xkb_str, "LatchMods(");
                        xkb_file_write_modifier_action_arguments (state, xkb_str, action);

                    } else if (action->type == KEY_ACTION_TYPE_MOD_LOCK) {
                        str_cat_printf (xkb_str, "LockMods(");
                        xkb_file_write_modifier_action_arguments (state, xkb_str, action);

                    } else if (action->type == KEY_ACTION_TYPE_NONE) {
                        str_cat_printf (xkb_str, "NoAction(");

                    } else {
                        invalid_code_path;
                    }

                    str_cat_printf (xkb_str, ")");

                    if (j < num_levels-1) {
                        str_cat_c (xkb_str, ", ");
                    }
                }
                str_cat_c (xkb_str, " ]\n");

            } else {
                str_cat_c (xkb_str, "\n");
            }

            str_cat_c (xkb_str, "    };\n");
        }
    }

    str_cat_c (xkb_str, "};\n");
}

// NOTE: If an error happens while writing, xkb_str will have the output of what
// we could do. We do this for debugging purposes. This also means that you must
// pass a status struct to know if the output is valid or not. Not doing so can
// have unexpected results.
void xkb_file_write (struct keyboard_layout_t *keymap, string_t *xkb_str, struct status_t *status)
{
    assert (xkb_str != NULL);
    str_set (xkb_str, "");

    // TODO: When we have a compact function, we should call it before creating
    // the output string. :keyboard_layout_compact

    struct xkb_writer_state_t state = {0};
    state.real_modifiers = xkb_get_real_modifiers_mask (keymap);
    // Create a reverse mapping of the modifier mapping in the internal
    // representation.
    create_reverse_modifier_name_map (keymap, state.reverse_modifier_definition);

    // TODO: Print our extra informtion as comments.

    str_cat_c (xkb_str, "xkb_keymap {\n");

    xkb_file_write_keycodes (&state, keymap, xkb_str);
    str_cat_c (xkb_str, "\n");

    xkb_file_write_types (&state, keymap, xkb_str);
    str_cat_c (xkb_str, "\n");

    xkb_file_write_compat (&state, keymap, xkb_str);
    str_cat_c (xkb_str, "\n");

    xkb_file_write_symbols (&state, keymap, xkb_str, true);
    str_cat_c (xkb_str, "\n");


    str_cat_c (xkb_str, "};\n\n"); // end of keymap
}

