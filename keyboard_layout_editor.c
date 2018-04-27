/*
 * Copiright (C) 2018 Santiago León O.
 */

#include "common.h"
#include "xkb_keymap_installer.c"

int main (int argc, char *argv[])
{
    if (argc > 1) {
        if (strcmp (argv[1], "--install") == 0) {
            if (argc == 2) {
                printf ("Expected a keymap file to install.\n");
            } else {
                xkb_keymap_install (argv[2]);
            }
        } else if (strcmp (argv[1], "--uninstall") == 0) {
            if (argc == 2) {
                printf ("Expected a keymap name to uninstall.\n");
            } else {
                xkb_keymap_uninstall (argv[2]);
            }
        } else if (strcmp (argv[1], "--uninstall-everything") == 0) {
            xkb_keymap_uninstall_everything ();
        }
    }
    xmlCleanupParser();
}
