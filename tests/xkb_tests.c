/*
 * Copiright (C) 2019 Santiago León O.
 */
#include "common.h"
#include "bit_operations.c"
#include "status.c"
#include "scanner.c"

#include <xkbcommon/xkbcommon.h>
#include <linux/input-event-codes.h>
#include "keycode_names.h"
#include "keysym_names.h"

// TODO: Clean up things so that tests only depend on GLib.
#include <gtk/gtk.h>
#include "gtk_utils.c"

#include "keyboard_layout.c"
#include "xkb_file_backend.c"

struct cli_opt_t {
    int id;
    const char *opt;
    bool expect_arg;
    const char *arg;
    void *data;

    struct cli_opt_t *next;
};

struct cli_parser_t {
    mem_pool_t pool;
    int num_opts;
    struct cli_opt_t *opts;
    struct cli_opt_t *last_opt;

    int argv_idx;

    int error;
    char *error_msg;
};

// NOTE: setting up an option without opt will make the parser never fail, all
// failing options will match this NULL option. Also, only the first NULL option
// will be taken into account, all the rest will be ignored. Thing of it as a
// default option.
int cli_parser_add_opt (struct cli_parser_t *parser, const char *opt, bool expect_arg, void *data)
{
    struct cli_opt_t *new_opt = mem_pool_push_size (&parser->pool, sizeof(struct cli_opt_t));
    *new_opt = ZERO_INIT(struct cli_opt_t);

    new_opt->opt = opt;
    new_opt->expect_arg = expect_arg;
    new_opt->data = data;
    if (parser->opts == NULL) {
        parser->opts = new_opt;
    } else {
        parser->last_opt->next = new_opt;
    }
    parser->last_opt = new_opt;

    return parser->num_opts++;
}

bool cli_parser_opt_lookup (struct cli_parser_t *parser, const char *opt_name, struct cli_opt_t *opt)
{
    struct cli_opt_t *curr_opt = parser->opts;
    while (curr_opt != NULL) {
        if (curr_opt->opt == NULL || strcmp (opt_name, curr_opt->opt) == 0) {
            if (opt != NULL) {
                *opt = *curr_opt;
            }
            return true;
        }

        curr_opt = curr_opt->next;
    }
    return false;
}

enum cli_parser_err_t {
    CP_ERR_NO_ERROR,
    CP_ERR_MISSING_ARGUMENT,
    CP_ERR_UNRECOGNIZED_OPT
};

bool cli_parser_get_next (struct cli_parser_t *parser, int argc, char *argv[], struct cli_opt_t *opt)
{
    bool retval = false;
    assert (opt != NULL);

    if (parser->argv_idx + 1 < argc) {
        int idx = parser->argv_idx + 1;
        if (cli_parser_opt_lookup (parser, argv[idx], opt)) {
            if (opt->expect_arg) {
                if (idx + 1 < argc && !cli_parser_opt_lookup (parser, argv[idx + 1], NULL)) {
                    opt->arg = argv[++idx];
                    retval = true;

                } else {
                    // Missing argument
                    parser->error = CP_ERR_MISSING_ARGUMENT;
                    parser->error_msg = pprintf (&parser->pool, "Missing argument for option '%s'", opt->opt);
                }

            } else {
                // Successfully found a non argument option.
                retval = true;

                // If it's a no option argument then store its value in arg.
                // :default_cli_argument
                if (opt->opt == NULL) {
                    opt->arg = argv[idx];
                }
            }

        } else {
            // Option not found
            parser->error = CP_ERR_UNRECOGNIZED_OPT;
            parser->error_msg = pprintf (&parser->pool, "Unrecognized option '%s'", argv[idx]);
        }

        if (retval == true) {
            parser->argv_idx += idx;
        }
    }

    return retval;
}

void cli_parser_destroy (struct cli_parser_t *parser)
{
    mem_pool_destroy (&parser->pool);
}

void print_mod_state (struct xkb_state *xkb_state, struct xkb_keymap *xkb_keymap,
                      xkb_mod_index_t xkb_num_mods, enum xkb_state_component type)
{
    bool is_first = true;
    // TODO: This assumes modifier indices in libxkbcommon are consecutive from
    // 0 to xkb_num_mods-1. For now it looks like it, but the documentation does
    // not say this explicitly.
    for (int i=0; i<xkb_num_mods; i++) {
        if (xkb_state_mod_index_is_active (xkb_state, i, type)) {
            if (!is_first) {
                printf (", ");
            }
            is_first = false;

            printf ("%s", xkb_keymap_mod_get_name (xkb_keymap, i));
        }
    }
}

struct compare_key_foreach_clsr_t {
    string_t *msg;

    struct xkb_keymap *k1;
    struct xkb_keymap *k2;

    bool equal_keymaps;
};

void compare_key_foreach (struct xkb_keymap *keymap, xkb_keycode_t kc, void *data)
{
    struct compare_key_foreach_clsr_t *clsr = (struct compare_key_foreach_clsr_t*) data;
    struct xkb_keymap *k1 = clsr->k1;
    struct xkb_keymap *k2 = clsr->k2;
    string_t *msg = clsr->msg;

    // Check the number of layouts in the key is valid
    int k1_num_layouts = xkb_keymap_num_layouts_for_key (k1, kc);
    int k2_num_layouts = xkb_keymap_num_layouts_for_key (k2, kc);
    if (k1_num_layouts != k2_num_layouts) {
        str_set_printf (msg, "Key %d has %d layouts in k1 but %d in k2.", kc, k1_num_layouts, k2_num_layouts);
        clsr->equal_keymaps = false;

    } else if (k1_num_layouts > 1) {
        // NOTE: What does it mean to have a number of layouts of 0? (yes, it
        // happens).
        str_set (msg, "Compared keymaps have more than 1 layout, this is not supported yet.");
        clsr->equal_keymaps = false;
    }

    // Check the number of levels in the key is valid.
    int k1_num_levels = xkb_keymap_num_levels_for_key (k1, kc, 0);
    int k2_num_levels = xkb_keymap_num_levels_for_key (k2, kc, 0);
    if (clsr->equal_keymaps && k1_num_levels != k2_num_levels) {
        str_set_printf (msg, "Key %d has %d levels in k1 but %d in k2.", kc, k1_num_levels, k2_num_levels);
        clsr->equal_keymaps = false;
    }

    // Compare all keysyms in each level.
    if (clsr->equal_keymaps) {
        for (int lvl=0; clsr->equal_keymaps && lvl<k1_num_levels; lvl++) {
            const xkb_keysym_t *k1_keysyms;
            const xkb_keysym_t *k2_keysyms;
            int k1_num_syms = xkb_keymap_key_get_syms_by_level (k1, kc, 0, lvl, &k1_keysyms);
            int k2_num_syms = xkb_keymap_key_get_syms_by_level (k2, kc, 0, lvl, &k2_keysyms);
            if (k1_num_syms != k2_num_syms) {
                str_set_printf (msg, "Key %d has %d keysyms in k1 but %d in k2.", kc, k1_num_syms, k2_num_syms);
                clsr->equal_keymaps = false;

            } else {
                for (int idx=0; clsr->equal_keymaps && idx<k1_num_syms; idx++) {
                    if (k1_keysyms[idx] != k2_keysyms[idx]) {
                        str_set_printf (msg, "k1[kc:%d][lvl:%d] -> %d != k1[kc:%d][lvl:%d] -> %d",
                                        kc, lvl, k1_keysyms[idx], kc, lvl, k2_keysyms[idx]);
                        clsr->equal_keymaps = false;
                    }
                }
            }
        }
    }
}

// This tet check that two keymaps have the same keysym table. This means that
// each keycode had the same keysyms in each level.
// NOTE: This doesn't guarantee that the two keymaps will behave in the same way
// as they may have different modifier or key type configurations.
bool keymap_equality_test (struct xkb_keymap *k1, struct xkb_keymap *k2, string_t *msg)
{
    assert (msg != NULL);

    struct compare_key_foreach_clsr_t clsr;
    clsr.k1 = k1;
    clsr.k2 = k2;
    clsr.msg = msg;
    clsr.equal_keymaps = true;

    if (xkb_keymap_num_layouts (k1) != xkb_keymap_num_layouts (k2)) {
        str_set (msg, "Keymaps have different number of layouts.");
        clsr.equal_keymaps = false;
    }

    xkb_keymap_key_for_each (k1, compare_key_foreach, &clsr);
    xkb_keymap_key_for_each (k2, compare_key_foreach, &clsr);

    return clsr.equal_keymaps;
}

struct modifier_key_t {
    int kc;
    key_modifier_mask_t modifiers;
    enum action_type_t type;

    struct modifier_key_t *next;
};
templ_sort(modifier_key_sort, struct modifier_key_t, a->kc < b->kc);

struct get_modifier_keys_list_clsr_t {
    mem_pool_t *pool;

    struct modifier_key_t *modifier_list_end;
    struct modifier_key_t *modifier_list;
};

void append_modifier_key (struct get_modifier_keys_list_clsr_t *clsr, int kc, enum action_type_t type)
{
    struct modifier_key_t *new_key = mem_pool_push_struct (clsr->pool, struct modifier_key_t);
    new_key->next = NULL;
    new_key->kc = kc;
    new_key->type = type;

    // TODO: Set a mask here corresponding to the modifiers that changed. Where
    // can we get this from?. Not straightforward because we need to make sure
    // these masks are comparable across different keymaps. Maybe assume they
    // will always be real modifiers and have a global predefined mask
    // definition for them. As fas as I can tell libxkbcommon always uses real
    // modifiers here, even when virtual modifiers are defined in the keymap.
    // :modifier_key_modifier_mask_test
    new_key->modifiers = 0x0;

    if (clsr->modifier_list == NULL) {
        clsr->modifier_list = new_key;
    } else {
        clsr->modifier_list_end->next = new_key;
    }
    clsr->modifier_list_end = new_key;
}

void get_modifier_keys_list_foreach (struct xkb_keymap *keymap, xkb_keycode_t kc, void *data)
{
    struct xkb_state *xkb_state = xkb_state_new(keymap);
    if (!xkb_state) {
        printf ("could not create xkb state.\n");
        return;
    }

    struct get_modifier_keys_list_clsr_t *clsr =
        (struct get_modifier_keys_list_clsr_t*)data;

    enum xkb_state_component changed_components = xkb_state_update_key (xkb_state, kc+8, XKB_KEY_DOWN);
    if (changed_components) {
        if (changed_components & XKB_STATE_MODS_DEPRESSED) {
            append_modifier_key (clsr, kc, KEY_ACTION_TYPE_MOD_SET);
        }

        if (changed_components & XKB_STATE_MODS_LATCHED) {
            append_modifier_key (clsr, kc, KEY_ACTION_TYPE_MOD_LATCH);
        }

        if (changed_components & XKB_STATE_MODS_LOCKED) {
            append_modifier_key (clsr, kc, KEY_ACTION_TYPE_MOD_LOCK);
        }
    }

    xkb_state_unref(xkb_state);
}

struct modifier_key_t* get_modifier_keys_list (mem_pool_t *pool, struct xkb_keymap *keymap)
{
    struct get_modifier_keys_list_clsr_t clsr;
    clsr.pool = pool;
    xkb_keymap_key_for_each (keymap, get_modifier_keys_list_foreach, &clsr);

    return clsr.modifier_list;
}

struct compare_key_states_foreach_clsr_t {
    int *bit_lookup;
    struct modifier_key_t *mod_keys;
};

void compare_key_states_foreach (struct xkb_keymap *keymap, xkb_keycode_t kc, void *data)
{

}

// This test is a more functional equality test of the keymaps. The idea is to
// press all modifier combinations and check that the resulting keysyms in each
// key are the same. Some caveats of how the test works, (we could fix them but
// it probably will be overkill?):
//
//  - We only get modifiers from the first level, actions that set modifiers in
//    other key levels are ignored and not checked.
//  - We currently ignore latched modifiers.
//  - We only compare the keysyms of keys that don't set a modifier in their
//    first level. It's possible to have modifier keys that in an other level
//    produces a keysym, differences here won't be caught.
//  - We ignore keysyms of keys that set/lock modifiers (modifier keys).
//
// NOTE: This assumes that the keymaps passed the keymap_equality_test.
// NOTE: This has exponential complexity on the number of keys that trigger
// modifiers. We could do a faster test based on key type information. The
// problem is I don't see how we can get type information from libxkbcommon, so
// we would need to use our internal representation of keymaps, and that's what
// we want to check.
// TODO: Do something like this that checks the LED states.
bool modifier_equality_test (struct xkb_keymap *k1, struct xkb_keymap *k2, string_t *msg)
{
    mem_pool_t pool;
    bool are_equal = true;
    int num_mod_keys = 0;
    struct modifier_key_t *mod_list_k1 = get_modifier_keys_list (&pool, k1);
    struct modifier_key_t *mod_list_k2 = get_modifier_keys_list (&pool, k2);

    // Check that both keymaps have the same modifiers.
    {
        struct modifier_key_t *curr_mod_k1 = mod_list_k1, *curr_mod_k2 = mod_list_k2;
        while (are_equal == true && curr_mod_k1 != NULL && curr_mod_k2 != NULL) {
            if (curr_mod_k1->kc != curr_mod_k2->kc) {
                str_set (msg, "Keymaps don't map modifiers to the same keys.");
                are_equal = false;

            } else if (curr_mod_k1->modifiers != curr_mod_k2->modifiers) {
                // TODO: This is currently not implemented, we need to think
                // about how to get global modifier masks.
                // :modifier_key_modifier_mask_test
                str_set_printf (msg, "Keymaps set or lock different real modifiers with key %d.", curr_mod_k1->kc);
                are_equal = false;
            }

            num_mod_keys++;

            curr_mod_k1 = curr_mod_k1->next;
            curr_mod_k2 = curr_mod_k2->next;
        }

        if ((curr_mod_k1 != NULL && curr_mod_k2 == NULL) ||
            (curr_mod_k1 == NULL && curr_mod_k2 != NULL)) {
            str_set (msg, "Keymaps don't have the same number of modifier keys.");
            are_equal = false;
        }
    }

    int max_mod_keys = 20;
    if (are_equal) {
        struct compare_key_states_foreach_clsr_t clsr;
        clsr.bit_lookup = create_bit_pos_lookup (&pool);

        // Put modifier key nodes into an array sorted by keycode. This is required
        // so we can check in a fast way if a keycode is a modifier key.
        struct modifier_key_t *curr_mod_k1 = mod_list_k1;
        clsr.mod_keys = mem_pool_push_array (&pool, num_mod_keys, struct modifier_key_t);
        for (int i=0; i<num_mod_keys; i++) {
            clsr.mod_keys[i] = *curr_mod_k1;
            clsr.mod_keys[i].next = NULL;

            curr_mod_k1 = curr_mod_k1->next;
        }

        modifier_key_sort (clsr.mod_keys, num_mod_keys);

        // Iterate all num_mod_keys^2 combinations and check that the resulting
        // keysyms are the same.
        // NOTE: This grows exponentially with the number of modifier keys in a
        // layout!
        if (are_equal && num_mod_keys <= max_mod_keys) {
            for (uint32_t pressed_keys = 1; pressed_keys<num_mod_keys*num_mod_keys; pressed_keys++) {
                struct xkb_state *s1 = xkb_state_new(k1);
                assert (s1);

                struct xkb_state *s2 = xkb_state_new(k2);
                assert (s2);

                // Iterate bits of pressed_keys and press the correspoiding keys in
                // the keymap states.
                while (pressed_keys) {
                    key_modifier_mask_t next_bit_mask = pressed_keys & -pressed_keys;

                    uint32_t idx = bit_mask_perfect_hash (next_bit_mask);
                    struct modifier_key_t mod_key = clsr.mod_keys[clsr.bit_lookup[idx]];
                    if (mod_key.type != KEY_ACTION_TYPE_MOD_LATCH) {
                        // We don't test latch modifiers. They need a special
                        // treatment because they are unset everytime a key is
                        // pressed. Currently we press modifier keys, then press
                        // each non modifier key compare the keysyms produced.
                        //
                        // They are considered modifier keys, though. Because we
                        // don't want to press them (as if they were non
                        // modifier keys) when comparing keysyms.
                        xkb_state_update_key (s1, mod_key.kc+8, XKB_KEY_DOWN);
                        xkb_state_update_key (s2, mod_key.kc+8, XKB_KEY_DOWN);
                    }

                    pressed_keys = pressed_keys & (pressed_keys-1);
                }

                xkb_keymap_key_for_each (k1, compare_key_states_foreach, NULL);

                xkb_state_unref(s1);
                xkb_state_unref(s2);
            }
        }
    }

    if (are_equal && num_mod_keys > max_mod_keys) {
        str_set_printf (msg, "We don't do modifier tests on keymaps with more than %d modifier keys.",
                        max_mod_keys);
    }

    mem_pool_destroy (&pool);
    return are_equal;
}

struct print_modifier_info_foreach_clsr_t {
    xkb_mod_index_t xkb_num_mods;
};

void print_modifier_info_foreach (struct xkb_keymap *keymap, xkb_keycode_t kc, void *data)
{
    struct xkb_state *xkb_state = xkb_state_new(keymap);
    if (!xkb_state) {
        printf ("could not create xkb state.\n");
        return;
    }

    struct print_modifier_info_foreach_clsr_t *clsr =
        (struct print_modifier_info_foreach_clsr_t*)data;
    xkb_mod_index_t xkb_num_mods = clsr->xkb_num_mods;

    enum xkb_state_component changed_components = xkb_state_update_key (xkb_state, kc+8, XKB_KEY_DOWN);
    if (changed_components) {
        printf (" %s (%d): ", keycode_names[kc], kc);
        if (changed_components & XKB_STATE_MODS_DEPRESSED) {
            printf ("Sets(");
            print_mod_state (xkb_state, keymap, xkb_num_mods, XKB_STATE_MODS_DEPRESSED);
            printf (") ");
        }

        if (changed_components & XKB_STATE_MODS_LATCHED) {
            printf ("Latches(");
            print_mod_state (xkb_state, keymap, xkb_num_mods, XKB_STATE_MODS_LATCHED);
            printf (") ");
        }

        if (changed_components & XKB_STATE_MODS_LOCKED) {
            printf ("Locks(");
            print_mod_state (xkb_state, keymap, xkb_num_mods, XKB_STATE_MODS_LOCKED);
            printf (") ");
        }

        if (changed_components & XKB_STATE_MODS_EFFECTIVE) {
            // Is this just an OR of the other modifier masks? or is
            // this related to "consumed mods".
            printf ("Effective(");
            print_mod_state (xkb_state, keymap, xkb_num_mods, XKB_STATE_MODS_EFFECTIVE);
            printf (") ");
        }

        if (changed_components & XKB_STATE_LAYOUT_DEPRESSED ||
            changed_components & XKB_STATE_LAYOUT_LATCHED ||
            changed_components & XKB_STATE_LAYOUT_LOCKED ||
            changed_components & XKB_STATE_LAYOUT_EFFECTIVE ) {
            printf ("LayoutChange ");
        }

        if (changed_components & XKB_STATE_LEDS) {
            printf ("LedsChange ");
        }

        printf ("\n");
    }

    xkb_state_unref(xkb_state);
}

void xkbcommon_print_modifier_info (struct xkb_keymap *keymap)
{
    printf ("Modifiers: ");
    xkb_mod_index_t xkb_num_mods = xkb_keymap_num_mods (keymap);
    for (int i=0; i<xkb_num_mods; i++) {
        printf ("%s", xkb_keymap_mod_get_name (keymap, i));
        if (i < xkb_num_mods-1) {
            printf (", ");
        }
    }

    printf ("\n\n");
    printf ("Modifier mapping:\n");
    // Iterate all keycodes and detect those that change the state of a
    // modifier.
    struct print_modifier_info_foreach_clsr_t clsr;
    clsr.xkb_num_mods = xkb_num_mods;
    xkb_keymap_key_for_each (keymap, print_modifier_info_foreach, &clsr);
}

enum input_type_t {
    INPUT_RMLVO_NAMES,
    INPUT_XKB_FILE
};

int main (int argc, char **argv)
{
    init_keycode_names ();

    struct xkb_context *xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!xkb_ctx) {
        printf ("could not create xkb context.\n");
    }

    enum input_type_t input_type;
    // Data if type is INPUT_RMLVO_NAMES
    // TODO: I think a value of NULL will set things to libxkbcommon's default
    // value, is there a way we can determin whet it's using? (programatically,
    // not just reading the code). Should we have our own default values so we
    // are always sure qwhat is being used?.
    char *rules = NULL;
    char *model = NULL;
    char *layout = NULL;
    char *variant = NULL;
    char *options = NULL;
    bool success = true;

    // Data if type is INPUT_XKB_FILE
    char *input_file = NULL;

    bool file_output_enabled = false;

    // Compute the keymap names of the layout that will be tested.
    {
        if (argc == 1) {
            // TODO: This case should execute tests for all available layouts.
            // For now we expect the user to say which layout to test.
            printf ("At least a layout name should be provided.\n");
            success = false;

        } else {
            struct cli_parser_t cli_parser = {0};
            cli_parser_add_opt (&cli_parser, "-r", true, &rules);
            cli_parser_add_opt (&cli_parser, "-m", true, &model);
            cli_parser_add_opt (&cli_parser, "-l", true, &layout);
            cli_parser_add_opt (&cli_parser, "-v", true, &variant);
            cli_parser_add_opt (&cli_parser, "-o", true, &options);

            cli_parser_add_opt (&cli_parser, "--write-output", false, &file_output_enabled);

            // :default_cli_argument
            char *default_argument = NULL;
            cli_parser_add_opt (&cli_parser, NULL, false, &default_argument);

            struct cli_opt_t opt;
            while (cli_parser_get_next (&cli_parser, argc, argv, &opt) &&
                   cli_parser.error == CP_ERR_NO_ERROR)
            {
                if (opt.opt == NULL && default_argument == NULL) {
                    // There is a non argument option.
                    // :default_cli_argument
                    *(const char **)opt.data = opt.arg;

                } else if (opt.expect_arg) {
                    *(const char **)opt.data = opt.arg;

                } else if (!opt.expect_arg) {
                    *(bool*)opt.data = true;

                } else {
                    cli_parser.error = CP_ERR_UNRECOGNIZED_OPT;
                    cli_parser.error_msg = pprintf (&cli_parser.pool, "Unexpected option '%s'", opt.opt);
                }
            }

            if (cli_parser.error) {
                // TODO: This will rarely ever happen because we are using a
                // default option that will match every unrecognized option.
                // :default_cli_argument
                printf ("Error: %s\n", cli_parser.error_msg);
                success = false;
            }

            if (default_argument) {
                char *extension = get_extension (default_argument);
                if (extension == NULL) {
                    // TODO: Check that this is an existing layout name.
                    layout = default_argument;
                    input_type = INPUT_RMLVO_NAMES;

                } else if (strncmp (extension, "xkb", 3) == 0) {
                    input_file = default_argument;
                    input_type = INPUT_XKB_FILE;

                } else {
                    printf ("Invalid arguments.\n");
                    success = false;
                }

            } else {
                input_type = INPUT_RMLVO_NAMES;
            }

            if (input_type == INPUT_XKB_FILE &&
                (rules != NULL || model != NULL || options != NULL || layout != NULL || variant != NULL)) {
                printf ("Xkb file provided as input, ignoring all passed RMLVO options.\n");
            }

            cli_parser_destroy (&cli_parser);
        }
    }

    if (!success) {
        // TODO: Show a message here. Usage documentation?
        return 1;

    } else {
        printf ("Used xkb_rule_names:\n");
        printf ("  rules: %s\n", rules);
        printf ("  model: %s\n", model);
        printf ("  layout: %s\n", layout);
        printf ("  variant: %s\n", variant);
        printf ("  options: %s\n", options);
        printf ("\n");
    }

    // Get an xkb string from the CLI input
    string_t input_str = {0};
    if (input_type == INPUT_RMLVO_NAMES) {
        // NOTE: This is a slow process, and there is a high chance of messing the
        // user's current layout. In the actual application we should have a
        // predefined library of base layouts in resolved xkb form. We can use this
        // code to generate that library.
        // TODO: Is there a way to get this from libxkbcommon? if so, that should be
        // faster, we should do that instead of calling the bash script.
        string_t cmd = str_new ("./tests/get_xkb_str.sh");
        if (rules != NULL) {
            str_cat_printf (&cmd, " -rules %s", rules);
        }

        if (model != NULL) {
            str_cat_printf (&cmd, " -model %s", model);
        }

        if (layout != NULL) {
            str_cat_printf (&cmd, " -layout %s", layout);
        }

        if (variant != NULL) {
            str_cat_printf (&cmd, " -variant %s", variant);
        }

        // TODO: setxkbmap only receives one option per -option argument, I
        // think we probably want to do the same? but our CLI parser only
        // hanldles a single -o. For now I don't care because I won't be testing
        // layouts that have options set, probably I won't even support them.
        if (options != NULL) {
            str_cat_printf (&cmd, " -option %s", options);
        }

        FILE* cmd_stdout = popen (str_data(&cmd), "r");
        if (cmd_stdout != NULL) {

            char buff[1024];
            size_t total_bytes = 0;
            size_t bytes_read = 0;
            do {
                bytes_read = fread (buff, 1, ARRAY_SIZE(buff), cmd_stdout);
                strn_cat_c (&input_str, buff, bytes_read);
                total_bytes += bytes_read;

            } while (bytes_read == ARRAY_SIZE(buff));
            str_cat_c (&input_str, "\0");

            if (ferror (cmd_stdout)) {
                printf ("An error occurred while reading output of script.");
            }

            assert (feof (cmd_stdout));

            int exit_status = pclose (cmd_stdout);
            if (exit_status != 0) {
                printf ("Command exited with %d.\n", exit_status%255);
            }
        }

        str_free (&cmd);

    } else {
        // TODO: Make a file read that writes directly to a string_t?, maybe
        // don't use a string_t for input_str?
        mem_pool_t tmp = {0};
        char *data = full_file_read (&tmp, input_file);
        str_set (&input_str, data);
        mem_pool_destroy (&tmp);
    }

    // Print modifier information from a libxkbcommon keymap created from the
    // received CLI input
    struct xkb_keymap *input_keymap;
    {
        if (input_type == INPUT_RMLVO_NAMES) {
            struct xkb_rule_names names = {
                .rules = rules,
                .model = model,
                .layout = layout,
                .variant = variant,
                .options = options
            };

            input_keymap = xkb_keymap_new_from_names(xkb_ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);

        } else {
            input_keymap =
                xkb_keymap_new_from_string(xkb_ctx, str_data(&input_str),
                                           XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
        }

        if (!input_keymap) {
            printf ("could not create xkbcommon keymap.\n");

        } else {
            // Get an overview of the layout loaded from libxkbcommon.
            printf ("------ libxkbcommon -----\n");
            xkbcommon_print_modifier_info (input_keymap);
        }
    }

    struct xkb_keymap *internal_keymap = NULL;
    printf ("\n------ xkb backend output -----\n");
    string_t parser_out = {0};
    struct keyboard_layout_t keymap = {0};
    if (!xkb_file_parse_verbose (str_data (&input_str), &keymap, &parser_out)) {
        if (input_type == INPUT_RMLVO_NAMES) {
            // TODO: Print all used input options
            printf ("Errors parsing input layout '%s'.\n", layout);
        } else {
            printf ("Errors parsing input layout '%s'.\n", input_file);
        }

    } else {
        // Our parser was successful, write the keymap back to an xkb file, load
        // it into libxkbcommon and print the modifier information of it.
        struct status_t status = {0};
        string_t xkb_out_str = {0};
        xkb_file_write (&keymap, &xkb_out_str, &status);

        if (status_is_error (&status)) {
            printf ("Internal xkb writer failed.\n");
            status_print (&status);
            if (file_output_enabled) {
                printf ("\n");
                printf ("Wrote xkb input file to: parser_input.xkb\n");
                full_file_write (str_data(&input_str), str_len(&input_str), "parser_input.xkb");
                printf ("Wrote incomplete xkb file to: writer_output.xkb\n");
                full_file_write (str_data(&xkb_out_str), str_len(&xkb_out_str), "writer_output.xkb");
            }

        } else {
            internal_keymap =
                xkb_keymap_new_from_string(xkb_ctx, str_data(&xkb_out_str),
                                           XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
            if (!internal_keymap) {
                printf ("Could not create keymap from internal writer's output.\n");
                if (file_output_enabled) {
                    printf ("\n");
                    printf ("Wrote xkb input file to: parser_input.xkb\n");
                    full_file_write (str_data(&input_str), str_len(&input_str), "parser_input.xkb");
                    printf ("Wrote writer's output xkb file to: writer_output.xkb\n");
                    full_file_write (str_data(&xkb_out_str), str_len(&xkb_out_str), "writer_output.xkb");
                }

            } else {
                // Get an overview of the layout loaded from libxkbcommon.
                xkbcommon_print_modifier_info (internal_keymap);

                printf ("\n%s", str_data(&parser_out));

                if (file_output_enabled) {
                    printf ("\n");
                    printf ("Wrote parser input to: parser_input.xkb\n");
                    full_file_write (str_data(&input_str), str_len(&input_str), "parser_input.xkb");
                    printf ("Wrote writer output to: writer_output.xkb\n");
                    full_file_write (str_data(&xkb_out_str), str_len(&xkb_out_str), "writer_output.xkb");
                }
            }
        }
    }

    printf ("\n------ Symbol Equality Test -----\n");
    if (internal_keymap != NULL && input_keymap != NULL) {
        string_t msg = {0};
        if (!keymap_equality_test (internal_keymap, input_keymap, &msg)) {
            printf ("Keymaps not equal: %s\n", str_data(&msg));
        } else {
            printf ("Keymaps are the same!\n");
        }
    }

    str_free (&input_str);

    xkb_keymap_unref(input_keymap);
    xkb_context_unref(xkb_ctx);

    return 0;
}
