/*
 * Copiright (C) 2018 Santiago León O.
 */

#include "common.h"
#include "xkb_keymap_installer.c"

int main (int argc, char *argv[])
{
    xkb_keymap_uninstall ("my_layout");
    xkb_keymap_install ("./custom_keyboard.xkb");
    xmlCleanupParser();
}
