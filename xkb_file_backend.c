/*
 * Copiright (C) 2019 Santiago León O.
 */

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

enum xkb_parser_token_type_t {
    XKB_PARSER_TOKEN_IDENTIFIER,
    XKB_PARSER_TOKEN_KEY_IDENTIFIER,
    XKB_PARSER_TOKEN_LEVEL_IDENTIFIER,
    XKB_PARSER_TOKEN_GROUP_IDENTIFIER,
    XKB_PARSER_TOKEN_OPERATOR,
    XKB_PARSER_TOKEN_NUMBER,
    XKB_PARSER_TOKEN_STRING
};

struct xkb_parser_state_t {
    mem_pool_t pool;

    struct scanner_t scnr;

    enum xkb_parser_token_type_t tok_type;
    string_t tok_value;
    int tok_value_int;

    GTree *key_identifiers_to_keycodes;

    struct keyboard_layout_t *keymap;

    char **predefined_modifiers;
    int predefined_modifiers_len;
};

void xkb_parser_state_init (struct xkb_parser_state_t *state, char *xkb_str, struct keyboard_layout_t *keymap)
{
    *state = ZERO_INIT(struct xkb_parser_state_t);
    state->scnr.pos = xkb_str;
    state->keymap = keymap;

    state->key_identifiers_to_keycodes = g_tree_new (strcmp_as_g_compare_func);
}

void xkb_parser_state_destory (struct xkb_parser_state_t *state)
{
    str_free (&state->tok_value);
    mem_pool_destroy (&state->pool);
    g_tree_destroy (state->key_identifiers_to_keycodes);
}

// NOTE: key_identifier is expected to already be allocated inside state->pool,
// we do this to avoid double allocation/copying. The parser needs to copy it
// anyway to be able to call this at the end of a keycode assignment statement.
bool xkb_parser_define_key_identifier (struct xkb_parser_state_t *state, char *key_identifier, int kc)
{
    bool new_identifier_defined = false;

    if (!g_tree_lookup_extended (state->key_identifiers_to_keycodes, key_identifier, NULL, NULL)) {
        // GTree only accepts pointers as values, a tree that points to integers
        // would avoid some pointer lookups.
        int *value_ptr = mem_pool_push_size (&state->pool, sizeof(int));
        *value_ptr = kc;
        g_tree_insert (state->key_identifiers_to_keycodes, key_identifier, value_ptr);
        new_identifier_defined = true;

    } else {
        char *error_msg =
            pprintf (&state->pool, "Key identifier '%s' already defined.", key_identifier);
        scanner_set_error (&state->scnr, error_msg);
    }

    return new_identifier_defined;
}

bool xkb_parser_key_identifier_lookup (struct xkb_parser_state_t *state, char *key_identifier, int *kc)
{
    assert (kc != NULL);

    bool identifier_found = false;

    void *value;
    if (g_tree_lookup_extended (state->key_identifiers_to_keycodes, key_identifier, NULL, &value)) {
        identifier_found = true;
        *kc = *(int*)value;
    }

    return identifier_found;
}

void xkb_parser_next (struct xkb_parser_state_t *state)
{
    struct scanner_t *scnr = &state->scnr;

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
        // TODO: There is a list of valid key identifiers somewhere as xkbcom
        // crashes when adding unknown ones. Where is this coming from? xkbcomp,
        // X11's spec or XKB's spec. For now we trust they will be valid.
        state->tok_type = XKB_PARSER_TOKEN_KEY_IDENTIFIER;

        tok_start = scnr->pos;
        scanner_to_char (scnr, '>');
        if (scnr->is_eof) {
            scanner_set_error (scnr, "Key identifier without closing '>'");
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
            scanner_set_error (scnr, "String without matching '\"'");
        } else {
            strn_set (&state->tok_value, tok_start, scnr->pos - 1 - tok_start);
        }

    } else {
        scanner_set_error (scnr, "Unexpected character");
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

void xkb_parser_expect_tok (struct xkb_parser_state_t *state, enum xkb_parser_token_type_t type, char *value)
{
    if (!xkb_parser_match_tok (state, type, value)) {
        if (state->tok_type != type) {
            // TODO: show identifier types as strings.
            char *error_msg =
                pprintf (&state->pool, "Expected Identifier of type '%d', got '%d'.", type, state->tok_type);
            scanner_set_error (&state->scnr, error_msg);

        } else {
            assert (value != NULL);
            char *error_msg =
                pprintf (&state->pool, "Expected '%s', got '%s'.", value, str_data(&state->tok_value));
            scanner_set_error (&state->scnr, error_msg);
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

bool xkb_parser_is_predefined_mod (struct xkb_parser_state_t *state, char *name)
{
    bool res = false;
    for (int i=0; i<state->predefined_modifiers_len; i++) {
        if (strcasecmp (state->predefined_modifiers[i], name) == 0) {
            res = true;
            break;
        }
    }

    return res;
}

key_modifier_mask_t xkb_parser_modifier_lookup (struct xkb_parser_state_t *state, char *name)
{
    key_modifier_mask_t result = 0;

    enum modifier_result_status_t status;
    result = keyboard_layout_get_modifier (state->keymap, str_data(&state->tok_value), &status);
    if (status == KEYBOARD_LAYOUT_MOD_UNDEFINED) {
        if (xkb_parser_is_predefined_mod (state, name)) {
            // :lazy_add_predefined_modifiers
            // NOTE: This should not fail
            result = keyboard_layout_new_modifier (state->keymap, name, &status);
            assert (status == KEYBOARD_LAYOUT_MOD_SUCCESS);

        } else {
            char *error_msg =
                pprintf (&state->pool, "Reference to undefined modifier '%s'.", str_data(&state->tok_value));
            scanner_set_error (&state->scnr, error_msg);
        }
    }

    return result;
}

// NOTE: Strangeley enough xkbcomp accepts strings like none+Shift+Control as
// valid modifier masks, my guess is none will be just 0 and not affect the mask
// in any way (but I haven't checked). We interpret them this way here.
void xkb_parser_modifier_mask (struct xkb_parser_state_t *state,
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
            char *error_msg =
                pprintf (&state->pool, "Expected '%s' or '+', got '%s'.",
                         end_operator, str_data(&state->tok_value));
            scanner_set_error (&state->scnr, error_msg);
        }

    } while (!state->scnr.error);
}

void xkb_parser_skip_until_operator (struct xkb_parser_state_t *state, char *operator)
{
    do {
        xkb_parser_next (state);
    } while (!state->scnr.error && !xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, operator));
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

        } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "minimum") ||
                   xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "maximum") ||
                   xkb_parser_match_tok (state, XKB_PARSER_TOKEN_KEY_IDENTIFIER, "indicator")) {

            // Ignore these statements.
            xkb_parser_skip_until_operator (state, ";");

        } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "}")) {
            break;
        }

    } while (!state->scnr.error);

    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";");

    state->scnr.eof_is_error = false;
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
                    enum modifier_result_status_t status;
                    keyboard_layout_new_modifier (state->keymap, str_data(&state->tok_value), &status);
                    if (status == KEYBOARD_LAYOUT_MOD_MAX_LIMIT_REACHED) {
                        scanner_set_error (&state->scnr, "Too many modifier definitions, maximum is 16.");

                    } else if (status == KEYBOARD_LAYOUT_MOD_REDEFINITION &&
                               !xkb_parser_is_predefined_mod (state, str_data(&state->tok_value))) {
                        printf ("Modifier '%s' defined multiple times.\n", str_data(&state->tok_value));
                    }

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

            xkb_parser_next (state);
            if (state->tok_type == XKB_PARSER_TOKEN_STRING) {
                struct key_type_t *new_type;
                char *type_name;
                key_modifier_mask_t type_modifier_mask;

                mem_pool_t type_data = ZERO_INIT(mem_pool_t);
                type_name = pom_strdup (&type_data, str_data(&state->tok_value));

                xkb_parser_next (state);
                if (!xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "{")) {
                    scanner_set_error (&state->scnr, "Expected type block");
                }

                // Parse the type's modifier mask.
                // NOTE: We assume the modifier mask is the first entry in the
                // type block. xkbcomp tries to compile types without this at
                // the start, but I think it will always fail anyway.
                xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "modifiers");
                xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "=");
                xkb_parser_modifier_mask (state, ";", &type_modifier_mask);

                new_type = keyboard_layout_new_type (state->keymap, type_name, type_modifier_mask);

                // Parse the other type statements.
                do {
                    key_modifier_mask_t level_modifiers;
                    int level;

                    xkb_parser_next (state);
                    if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "map")) {
                        xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "[");

                        xkb_parser_modifier_mask (state, "]", &level_modifiers);

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
                            char *error_msg =
                                pprintf (&state->pool,
                                         "Can't map 'none' to level %d. It's reserved for level 1.\n", level);
                            scanner_set_error (&state->scnr, error_msg);

                        } else if (~type_modifier_mask & level_modifiers) {
                            // TODO: Tell the user which modifiers are the
                            // problematic ones.
                            char *error_msg =
                                pprintf (&state->pool,
                                         "Modifier map for level %d uses modifiers not in the mask for type '%s'.\n",
                                         level, new_type->name);
                            scanner_set_error (&state->scnr, error_msg);
                        }

                        if (!state->scnr.error) {
                            enum type_level_mapping_result_status_t status;
                            keyboard_layout_type_new_level_map (state->keymap,
                                                                new_type, level, level_modifiers,
                                                                &status);
                            if (status == KEYBOARD_LAYOUT_MOD_MAP_MAPPING_ALREADY_ASSIGNED) {
                                // TODO: Print the modifier mask niceley like
                                // Shift+Alt, not a hexadecimal value.
                                char *error_msg =
                                    pprintf (&state->pool,
                                             "Modifier mask %x already assigned in type '%s'",
                                             level_modifiers, new_type->name);
                                scanner_set_error (&state->scnr, error_msg);
                            }
                        }

                    } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "level_name") ||
                               xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "preserve")) {
                        // TODO: We ignore these statements for now.
                        xkb_parser_skip_until_operator (state, ";");

                    } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "}")) {
                        break;

                    } else {
                        scanner_set_error (&state->scnr, "Invalid statement in key type");
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
            scanner_set_error (&state->scnr, "Invalid statement in types section");
        }

    } while (!state->scnr.error);

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
        if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, NULL) ||
            (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_NUMBER, NULL) && state->tok_value_int < 10)) {
            symbols[*num_symbols_found] =
                xkb_keysym_from_name (str_data(&state->tok_value), XKB_KEYSYM_NO_FLAGS);
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
                char *error_msg =
                    pprintf (&state->pool, "Undefined key identifier '%s.", str_data(&state->tok_value));
                scanner_set_error (&state->scnr, error_msg);
            }

            xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, "{");

            struct key_type_t *type = NULL;
            xkb_keysym_t symbols[KEYBOARD_LAYOUT_MAX_LEVELS];
            int num_symbols = 0;
            do {
                xkb_parser_next (state);
                if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "[")) {
                    // This is a shorthand used, in this case the type will be
                    // guessed afterwards, and there are no actions set here.
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
                            char *error_msg =
                                pprintf (&state->pool, "Unknown type '%s.", str_data(&state->tok_value));
                            scanner_set_error (&state->scnr, error_msg);

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
                    xkb_parser_skip_until_operator (state, ";");
                    scanner_set_error (&state->scnr, "Parsing of actions in key definitions not implemented yet");
                }

                xkb_parser_next (state);
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
                if (num_symbols == 1) {
                    type = keyboard_layout_type_lookup (state->keymap, "ONE_LEVEL");
                } else if (num_symbols == 2) {
                    type = keyboard_layout_type_lookup (state->keymap, "TWO_LEVEL");
                } else {
                    // TODO: I'm not sure this is the actual default in xkbcomp
                    // for this situation. Research that.
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
                    }
                }
            }

        } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "name") ||
            xkb_parser_match_tok (state, XKB_PARSER_TOKEN_IDENTIFIER, "modifier_map")) {
            // TODO: Don't skip modifier_map statements.
            xkb_parser_skip_until_operator (state, ";");

        } else if (xkb_parser_match_tok (state, XKB_PARSER_TOKEN_OPERATOR, "}")) {
            break;

        } else {
            scanner_set_error (&state->scnr, "Invalid statement in symbols section");
        }

    } while (!state->scnr.error);

    xkb_parser_consume_tok (state, XKB_PARSER_TOKEN_OPERATOR, ";");

    state->scnr.eof_is_error = false;
}

// This parses a subset of the xkb file syntax into our internal representation
// keyboard_layout_t. We only care about parsing resolved layouts as returned by
// xkbcomp. Notable differences from a full xkb compiler are the lack of include
// statements and a more strict ordering of things.
bool xkb_file_parse (char *xkb_str, struct keyboard_layout_t *keymap)
{
    struct xkb_parser_state_t state;
    xkb_parser_state_init (&state, xkb_str, keymap);

    char *predefined_modifiers[] = {"Shift", "Control", "Lock", "Mod1", "Mod2", "Mod3", "Mod5", "Mod5"};
    // These modifiers will not be added into the keymap yet, they will be added
    // lazily when they are referenced somewhere in the keymap.
    // :lazy_add_predefined_modifiers
    state.predefined_modifiers = predefined_modifiers;
    state.predefined_modifiers_len = ARRAY_SIZE(predefined_modifiers);

    xkb_parser_consume_tok (&state, XKB_PARSER_TOKEN_IDENTIFIER, "xkb_keymap");
    xkb_parser_consume_tok (&state, XKB_PARSER_TOKEN_OPERATOR, "{");

    xkb_parser_parse_keycodes (&state);
    xkb_parser_parse_types (&state);
    xkb_parser_skip_block (&state, "xkb_compatibility");
    xkb_parser_parse_symbols (&state);

    // Should we accept keymaps with geometry section? looks like xkbcomp does
    // not return one.
    //xkb_parser_skip_block (&state, "xkb_geometry");

    xkb_parser_consume_tok (&state, XKB_PARSER_TOKEN_OPERATOR, "}");
    xkb_parser_consume_tok (&state, XKB_PARSER_TOKEN_OPERATOR, ";");

    // TODO: Make sure we reach EOF here?

    bool success = true;
    if (state.scnr.error) {
        printf ("Error: %s\n", state.scnr.error_message);
        success = false;
    }

    xkb_parser_state_destory (&state);

    // TODO: This parser does not care if some elements in the internal
    // representation are unused. We will maybe need to add functionality into
    // keyboard_layout.c that compacts layout elements. For example if the xkb
    // file defines 16 modifiers but only uses 10 fo those, we don't want to
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

    return success;
}

struct xkb_writer_state_t {
    string_t *xkb_str;

    // An array of modifier names indexed by the bit position of the mask. This
    // is effectiveley the reverse map of the one used in the internal
    // representation.
    // NOTE: This will point into the internal representation, which is fine
    // because we won't destroy the keymap representation inside xkb_file_write.
    // TODO: Maybe change this for a tree so we can represent the 'none'
    // modifier mask?
    // :none_modifier
    char *reverse_modifier_map[KEYBOARD_LAYOUT_MAX_MODIFIERS];

    // Flag used when printing modifier definitions to know when to print a ','
    // this is necessary because traversing of trees is done through a callback.
    bool is_first;
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
                str_cat_c (str, state->reverse_modifier_map[bit_pos]);

                if (mask>>1 != 0) {
                    str_cat_c (str, " + ");
                }
            }

            bit_pos++;
            mask >>= 1;
        }
    }
}

gboolean reverse_mapping_create_foreach (gpointer modifier_name, gpointer mask_ptr, gpointer data)
{
    struct xkb_writer_state_t *state = (struct xkb_writer_state_t*)data;

    key_modifier_mask_t mask = *(key_modifier_mask_t*)mask_ptr;
    if (mask != 0) {
        int pos = 0;
        while (!(mask&0x1) && pos < KEYBOARD_LAYOUT_MAX_MODIFIERS) {
            pos++;
            mask >>= 1;
        }

        if (state->reverse_modifier_map[pos]) {
            printf ("Modfier mapping is not 1:1, keymap seems to be corrupted.\n");

        } else if (pos == KEYBOARD_LAYOUT_MAX_MODIFIERS) {
            printf ("Invalid modifier mask, keymap seems to be corrupted.\n");

        } else {
            state->reverse_modifier_map[pos] = modifier_name;
        }
    }
    // else {
    // The 'none' modifier can't be represented with the current reverse mapping
    // data structure. We ignore it and later when writing masks we handle it as
    // a special case.
    // :none_modifier
    // }

    return FALSE;
}

gboolean print_modifiers_foreach (gpointer modifier_name, gpointer mask_ptr, gpointer data)
{
    struct xkb_writer_state_t *state = (struct xkb_writer_state_t*)data;

    if (state->is_first == true) {
        state->is_first = false;
    } else {
        str_cat_c (state->xkb_str, ",");
    }

    str_cat_c (state->xkb_str, modifier_name);

    return FALSE;
}

// Can we guarantee this will never fail? if we do then we can write the output
// directly into res.
void xkb_file_write (struct keyboard_layout_t *keymap, string_t *res)
{
    bool success = true;
    string_t xkb_str = {0};
    char buff[100];

    // TODO: When we have a compact function, we should call it before creating
    // the output string. :keyboard_layout_compact

    struct xkb_writer_state_t state = {0};
    state.xkb_str = &xkb_str;
    // Create a reverse mapping of the modifier mapping in the internal
    // representation.
    g_tree_foreach (keymap->modifiers, reverse_mapping_create_foreach, &state);

    // TODO: Print our extra informtion as comments.

    str_cat_c (&xkb_str, "keymap {\n");

    // As far as I've been able to understand, the keycode section is basically
    // useless. It's only purpose is to assign more semantically meaningful
    // names to keycodes. There is no list of specific key identifiers, instead
    // the keymap database is the one that attempts to provide meaning to them.
    // As far as I can tell any 0 to 4 sequence of alphanumeric characters can
    // be a key identifier. What we do with this section is to name keycodes by
    // their kernel keycode (keycode KEY_ESC will be <1>). At the moment the
    // kernel expects to have maximum 768 keycode macros, I don't think they
    // will go beyond the 4 character limit.
    //
    // In the future I think the xkb file format could even drop the <> but we would
    // have to check that this isn't semantically ambiguous. To improve human
    // readability we could use the kernel's macros for keycodes but then we are
    // relying on these macros never changing in the future (which I think is a
    // safe assumption to make about the kernel development team).
    //

    str_cat_c (&xkb_str, "xkb_keycodes \"keys_k\" {\n");
    str_cat_c (&xkb_str, "    minimum = 8\n");
    str_cat_c (&xkb_str, "    maximum = 255\n");

    // TODO: This could be faster if keys were in a linked list. But more cache
    // unfriendly?. If it ever becomes an issue, see which on is better.
    int i=0;
    for (i=0; i<KEY_CNT; i++) {
        if (keymap->keys[i] != NULL) {
            str_cat_c (&xkb_str, "    ");
            snprintf (buff, ARRAY_SIZE(buff), "<%d>", i);
            str_cat_c (&xkb_str, buff);
            str_cat_c (&xkb_str, " = ");
            snprintf (buff, ARRAY_SIZE(buff), "%d", i+8);
            str_cat_c (&xkb_str, buff);

            if (keycode_names[i] != NULL) {
                str_cat_c (&xkb_str, "; // ");
                str_cat_c (&xkb_str, keycode_names[i]);
                str_cat_c (&xkb_str, "\n");

            } else {
                str_cat_c (&xkb_str, ";\n");
            }
        }
    }
    str_cat_c (&xkb_str, "};\n\n"); // end of keycodes section

    str_cat_c (&xkb_str, "xkb_types \"keys_t\" {\n");

    str_cat_c (&xkb_str, "    virtual_modifiers ");
    // NOTE: We print modifier definitions from the internal representation's
    // tree and not from the reverse mapping because we want them in alphabetic
    // ordes so their ordering does not depend on the value of the mask assigned
    // to it.
    state.is_first = true;
    g_tree_foreach (keymap->modifiers, print_modifiers_foreach, &state);
    str_cat_c (&xkb_str, ";\n\n");

    struct key_type_t *curr_type = keymap->types;
    while (curr_type != NULL) {
        str_cat_c (&xkb_str, "    type \"");
        str_cat_c (&xkb_str, curr_type->name);
        str_cat_c (&xkb_str, "\" {\n");
        str_cat_c (&xkb_str, "        modifiers = ");
        xkb_file_write_modifier_mask (&state, &xkb_str, curr_type->modifier_mask);
        str_cat_c (&xkb_str, ";\n");

        struct level_modifier_mapping_t *curr_modifier_mapping = curr_type->modifier_mappings;
        while (curr_modifier_mapping != NULL) {
            str_cat_c (&xkb_str, "        map[");
            xkb_file_write_modifier_mask (&state, &xkb_str, curr_modifier_mapping->modifiers);
            str_cat_c (&xkb_str, "] = Level");
            snprintf (buff, ARRAY_SIZE(buff), "%d", curr_modifier_mapping->level);
            str_cat_c (&xkb_str, buff);
            str_cat_c (&xkb_str, ";\n");

            curr_modifier_mapping = curr_modifier_mapping->next;
        }

        // According to some documentation level names are required. When
        // testing it looks like xkbcomp only checks there is at least one name,
        // it doesn't check all mapped levels have a name. In any case, we
        // create generic names for all of them. Maybe in the future let the
        // user name them?.
        int num_levels = keyboard_layout_type_get_num_levels (curr_type);
        for (int i=0; i<num_levels; i++) {
            snprintf (buff, ARRAY_SIZE(buff), "%d", i+1);

            str_cat_c (&xkb_str, "        level_name[Level");
            str_cat_c (&xkb_str, buff);
            str_cat_c (&xkb_str, "] = \"Level ");
            str_cat_c (&xkb_str, buff);
            str_cat_c (&xkb_str, "\";\n");
        }
        str_cat_c (&xkb_str, "    };\n");

        curr_type = curr_type->next;
    }
    str_cat_c (&xkb_str, "};\n\n"); // end of types section

    str_cat_c (&xkb_str, "xkb_compatibility \"keys_c\" {\n");
    str_cat_c (&xkb_str, "};\n\n"); // end of compatibility section

    str_cat_c (&xkb_str, "xkb_symbols \"keys_s\" {\n");
    str_cat_c (&xkb_str, "};\n\n"); // end of symbols section

    str_cat_c (&xkb_str, "};\n\n"); // end of keymap

    if (success) {
        str_cpy (res, &xkb_str);
    }
    str_free (&xkb_str);
}

