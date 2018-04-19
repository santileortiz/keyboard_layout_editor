#!/usr/bin/python3
from mkpy.utility import *

assert sys.version_info >= (3,2)

modes = {
        'debug': '-Og -g -Wall',
        'profile_debug': '-O2 -g -pg -Wall',
        'release': '-O2 -g -DNDEBUG -Wall'
        }
cli_mode = get_cli_option('-M,--mode', modes.keys())
FLAGS = modes[pers('mode', 'debug', cli_mode)]
ensure_dir ("bin")

def default():
    target = pers ('last_target', 'keyboard_layout_editor')
    call_user_function(target)

def keyboard_layout_editor ():
    ex ('gcc {FLAGS} -o bin/keyboard-layout-editor keyboard_layout_editor.c -I/usr/include/libxml2 -lxml2 -lm')

if __name__ == "__main__":
    # Everything above this line will be executed for each TAB press.
    # If --get_completions is set, handle_tab_complete() calls exit().
    handle_tab_complete ()

    pymk_default()

