/*
 * Copiright (C) 2019 Santiago León O.
 */
#include "common.h"
#include <xkbcommon/xkbcommon.h>

int main (int argc, char **argv)
{
    if (argc <= 1) {
        printf ("Usage: xkb_keymap_getter [LAYOUT_NAME]\n");
        return 0;
    }

    bool success = true;

    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!ctx) {
        printf ("Error creating xkb_context.\n");
        success = false;
    }

    struct xkb_keymap *keymap = NULL;
    if (success) {
        struct xkb_rule_names names = {0};
        names.layout = argv[1];
        keymap = xkb_keymap_new_from_names (ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (!keymap) {
            printf ("Error creating xkb_keymap.\n");
            success = false;
        }
    }

    if (success) {
        printf ("%s", xkb_keymap_get_as_string (keymap, XKB_KEYMAP_FORMAT_TEXT_V1));
    }

    return 0;
}
