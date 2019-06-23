/*
 * Copiright (C) 2019 Santiago León O.
 */
#include "common.h"
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

int cli_parser_add_opt (struct cli_parser_t *parser, const char *opt, bool expect_arg, void *data)
{
    assert (opt && "non option arguments not yet implemented.");

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
        if (strcmp (opt_name, curr_opt->opt) == 0) {
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

void assert_no_active_modifier (struct xkb_state *xkb_state, struct xkb_keymap *xkb_keymap,
                                xkb_mod_index_t xkb_num_mods, enum xkb_state_component type)
{
    for (int i=0; i<xkb_num_mods; i++) {
        assert (!xkb_state_mod_index_is_active (xkb_state, i, type));
    }
}

void xkbcommon_print_modifier_info (struct xkb_keymap *keymap)
{
    struct xkb_state *xkb_state = xkb_state_new(keymap);
    if (!xkb_state) {
        printf ("could not create xkb state.\n");
        return;
    }

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
    for (xkb_keycode_t kc=0; kc<ARRAY_SIZE(keycode_names); kc++) {
        if (keycode_names[kc] != NULL) {
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

                // Reset changed modifiers.
                {
                    xkb_state_update_key (xkb_state, kc+8, XKB_KEY_UP);

                    if (changed_components & XKB_STATE_MODS_LATCHED) {
                        // TODO: For now we assume that the Escape key will not
                        // set any modifiers, this may not be the case in
                        // general. The proper way to do this is to create a
                        // clean layout state from the keymap, press a key,
                        // check if it sets a modifier, if it doesn't we found a
                        // key and terminate. If it does set a modifier, delete
                        // the keymap state and create a new one for the next
                        // keycode and repeat. We can't reuse the layout state
                        // because there is no way for us to properly reset
                        // modifiers without having a non modifier key which is
                        // what we will be computeng here. I didn't find an API
                        // to reset the state in libxkbcommon.
                        //
                        // Probably this is useless? if we will be creating a
                        // layout state for each keycode to be able to reset
                        // modifiers, probably we can do the same when computing
                        // the modifier map?. Then we don't even need to have a
                        // non modifier key to reset the state...
                        xkb_state_update_key (xkb_state, KEY_ESC+8, XKB_KEY_DOWN);
                        xkb_state_update_key (xkb_state, KEY_ESC+8, XKB_KEY_UP);
                    }

                    if (changed_components & XKB_STATE_MODS_LOCKED) {
                        // Unlock the modifier.
                        xkb_state_update_key (xkb_state, kc+8, XKB_KEY_DOWN);
                        xkb_state_update_key (xkb_state, kc+8, XKB_KEY_UP);
                    }

                    // Check that no modifier is set
                    assert_no_active_modifier (xkb_state, keymap, xkb_num_mods, XKB_STATE_MODS_DEPRESSED);
                    assert_no_active_modifier (xkb_state, keymap, xkb_num_mods, XKB_STATE_MODS_LATCHED);
                    assert_no_active_modifier (xkb_state, keymap, xkb_num_mods, XKB_STATE_MODS_LOCKED);
                    assert_no_active_modifier (xkb_state, keymap, xkb_num_mods, XKB_STATE_MODS_EFFECTIVE);
                }

                printf ("\n");
            }
        }
    }

    xkb_state_unref(xkb_state);
}

int main (int argc, char **argv)
{
    init_keycode_names ();

    struct xkb_context *xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!xkb_ctx) {
        printf ("could not create xkb context.\n");
    }

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

    // Compute the keymap names of the layout that will be tested.
    {
        if (argc == 1) {
            // TODO: This case should execute tests for all available layouts.
            // For now we expect the user to say which layout to test.
            printf ("At least a layout name should be provided.\n");
            success = false;

        } if (argc == 2) {
            // TODO: Check that this is an existing layout name.
            layout = argv[1];

        } else {
            struct cli_parser_t cli_parser = {0};
            cli_parser_add_opt (&cli_parser, "-r", true, &rules);
            cli_parser_add_opt (&cli_parser, "-m", true, &model);
            cli_parser_add_opt (&cli_parser, "-l", true, &layout);
            cli_parser_add_opt (&cli_parser, "-v", true, &variant);
            cli_parser_add_opt (&cli_parser, "-o", true, &options);

            struct cli_opt_t opt;
            while (cli_parser_get_next (&cli_parser, argc, argv, &opt) &&
                   cli_parser.error == CP_ERR_NO_ERROR)
            {
                *(const char **)opt.data = opt.arg;
            }

            if (cli_parser.error) {
                printf ("Error: %s\n", cli_parser.error_msg);
                success = false;
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

    // Print modifier information from a libxkbcommon keymap created from the
    // received RMLVO configuration
    struct xkb_keymap *xkb_keymap;
    {
        struct xkb_rule_names names = {
            .rules = rules,
            .model = model,
            .layout = layout,
            .variant = variant,
            .options = options
        };

        xkb_keymap = xkb_keymap_new_from_names(xkb_ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (!xkb_keymap) {
            printf ("could not create xkbcommon keymap.\n");

        } else {
            // Get an overview of the layout loaded from libxkbcommon.
            printf ("------ libxkbcommon -----\n");
            xkbcommon_print_modifier_info (xkb_keymap);
        }
    }

    // Get an xkb string from the RMLVO configuration
    // NOTE: This is a slow process, and there is a high chance of messing the
    // user's current layout. In the actual application we should have a
    // predefined library of base layouts in resolved xkb form. We can use this
    // code to generate that library.
    // TODO: Is there a way to get this from libxkbcommon? if so, that should be
    // faster, we should do that instead of calling the bash script.
    string_t xkb_str = {0};
    {
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
                strn_cat_c (&xkb_str, buff, bytes_read);
                total_bytes += bytes_read;

            } while (bytes_read == ARRAY_SIZE(buff));
            str_cat_c (&xkb_str, "\0");

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
    }

    printf ("\n------ xkb backend output -----\n");
    struct keyboard_layout_t keymap = {0};
    if (!xkb_file_parse (str_data (&xkb_str), &keymap)) {
        printf ("Keymap:\n%s", str_data (&xkb_str));
        printf ("Errors parsing layout '%s'.\n", layout);

    } else {
        // Our parser was successful, write the keymap back to an xkb file, load
        // it into libxkbcommon and print the modifier information of it.
        struct status_t status = {0};
        string_t xkb_out_str = {0};
        xkb_file_write (&keymap, &xkb_out_str, &status);

        if (status_is_error (&status)) {
            printf ("Internal xkb writer failed.\n");

        } else {
            struct xkb_keymap *internal_keymap =
                xkb_keymap_new_from_string(xkb_ctx, str_data(&xkb_out_str),
                                           XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
            if (!xkb_keymap) {
                printf ("Could not create keymap from internal writer's output.\n");

            } else {
                // Get an overview of the layout loaded from libxkbcommon.
                xkbcommon_print_modifier_info (internal_keymap);
            }
        }
    }

    str_free (&xkb_str);

    xkb_keymap_unref(xkb_keymap);
    xkb_context_unref(xkb_ctx);

    return 0;
}
