#!/bin/sh
setxkbmap -print > _xkb_resolve_curr_keymap_bak.xkb

setxkbmap "$@" 2> _xkbcomp_error.log
if [ -s _xkbcomp_error.log ]
then
    cat _xkbcomp_error.log
    echo There was an issue compiling "$1".
    rm _xkbcomp_error.log

    # Restore backup keymap
    xkbcomp _xkb_resolve_curr_keymap_bak.xkb $DISPLAY 2> /dev/null
    rm _xkb_resolve_curr_keymap_bak.xkb
    return 1
fi

xkbcomp -xkb $DISPLAY _xkb_resolve_res.xkb
cat _xkb_resolve_res.xkb
rm _xkb_resolve_res.xkb

# Restore backup keymap
xkbcomp _xkb_resolve_curr_keymap_bak.xkb $DISPLAY 2> /dev/null
rm _xkb_resolve_curr_keymap_bak.xkb
