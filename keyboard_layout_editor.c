/*
 * Copiright (C) 2018 Santiago León O.
 */

#include "common.h"
#include "xkb_keymap_installer.c"

#include <unistd.h>
#include <sys/types.h>

int main (int argc, char *argv[])
{
    if (argc > 1) {
        if (geteuid() == 0) {
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
        } else {
            string_t command = str_new ("pkexec ");

            char *sh_path = sh_expand (argv[0], NULL); // maybe substitute ~
            char *bin_path = realpath (sh_path, NULL); // get an absolute path
            str_cat_c (&command, bin_path);
            free (bin_path);
            free (sh_path);

            str_cat_c (&command, " ");
            str_cat_c (&command, argv[1]);
            str_cat_c (&command, " ");
            if (strcmp (argv[1], "--install") == 0) {
                char *file_path = sh_expand (argv[2], NULL);
                str_cat_c (&command, file_path);
                free (file_path);
            } else if (strcmp (argv[1], "--uninstall") == 0) {
                str_cat_c (&command, argv[2]);
            } else {
                // argv[1] == "--uninstall-everything", there are no more
                // arguments.
            }

            printf ("%s\n", str_data(&command));
            int retval = system (str_data (&command));
            if (!WIFEXITED (retval)) {
                printf ("Could not call pkexec.\n");
            }
            str_free (&command);
        }
    }
    xmlCleanupParser();
}
