/*
 * Copiright (C) 2018 Santiago León O.
 */

#include "common.h"
#include "xkb_keymap_installer.c"

#include <unistd.h>
#include <sys/types.h>

char* argv_0;

static inline
void str_cat_full_path (string_t *str, char *path)
{
    char *expanded_path = sh_expand (path, NULL); // maybe substitute ~
    char *abs_path = realpath (expanded_path, NULL); // get an absolute path
    str_cat_c (str, abs_path);
    free (expanded_path);
    free (abs_path);
}

bool unprivileged_xkb_keymap_install (char *keymap_path)
{
    bool success = true;
    if (!xkb_keymap_install (keymap_path)) {
        if (errno == EACCES) {
            string_t command = str_new ("pkexec ");
            str_cat_full_path (&command, argv_0);
            str_cat_c (&command, " --install ");
            str_cat_full_path (&command, keymap_path);

            int retval = system (str_data (&command));
            if (!WIFEXITED (retval)) {
                printf ("Could not call pkexec\n");
            }
            str_free (&command);
        } else {
            success = false;
        }
    }
    return success;
}

bool unprivileged_xkb_keymap_uninstall (char *layout_name)
{
    bool success = true;
    if (!xkb_keymap_uninstall (layout_name)) {
        if (errno == EACCES) {
            string_t command = str_new ("pkexec ");
            str_cat_full_path (&command, argv_0);
            str_cat_c (&command, " --uninstall ");
            str_cat_c (&command, layout_name);

            int retval = system (str_data (&command));
            if (!WIFEXITED (retval)) {
                printf ("Could not call pkexec.\n");
            }
            str_free (&command);
        } else {
            success = false;
        }
    }
    return success;
}

bool unprivileged_xkb_keymap_uninstall_everything ()
{
    bool success = true;
    if (!xkb_keymap_uninstall_everything ()) {
        if (errno == EACCES) {
            string_t command = str_new ("pkexec ");
            str_cat_full_path (&command, argv_0);
            str_cat_c (&command, " --uninstall-everything");

            int retval = system (str_data (&command));
            if (!WIFEXITED (retval)) {
                printf ("Could not call pkexec.\n");
            }
            str_free (&command);
        } else {
            success = false;
        }
    }
    return success;
}

int main (int argc, char *argv[])
{
    bool success = true;
    argv_0 = argv[0];
    if (argc > 1) {
        if (strcmp (argv[1], "--install") == 0) {
            if (argc == 2) {
                printf ("Expected a keymap file to install.\n");
            } else {
                success = unprivileged_xkb_keymap_install (argv[2]);
            }
        } else if (strcmp (argv[1], "--uninstall") == 0) {
            if (argc == 2) {
                printf ("Expected a keymap name to uninstall.\n");
            } else {
                success = unprivileged_xkb_keymap_uninstall (argv[2]);
            }
        } else if (strcmp (argv[1], "--uninstall-everything") == 0) {
            success = unprivileged_xkb_keymap_uninstall_everything ();
        }
    }
    xmlCleanupParser();

    return !success;
}
