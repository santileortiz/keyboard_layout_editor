#!/usr/bin/python3
from mkpy.utility import *
import textwrap, re

assert sys.version_info >= (3,2)

modes = {
        'debug': '-O0 -g -Wall',
        'profile_debug': '-O2 -g -pg -Wall',
        'release': '-O2 -g -DNDEBUG -Wall'
        }
cli_mode = get_cli_option('-M,--mode', modes.keys())
FLAGS = modes[pers('mode', 'debug', cli_mode)]
GTK_FLAGS = ex ('pkg-config --cflags --libs gtk+-3.0', ret_stdout=True, echo=False)
ensure_dir ("bin")

def default():
    target = pers ('last_target', 'keyboard_layout_editor')
    call_user_function(target)

def keyboard_layout_editor ():
    ex ('gcc {FLAGS} -o bin/keyboard-layout-editor keyboard_layout_editor.c -I/usr/include/libxml2 -lxml2 {GTK_FLAGS} -lm -lxkbcommon')

def generate_keycode_names ():
    def find_system_lib (library):
        """
        Returns the full path that will be included by gcc for the library.
        The argument passed to this fuction is expected to be what would be
        inside <> in a system #include directive.
        """
        dummy_fname = ex ('mktemp', ret_stdout=True, echo=False)
        dummy_c = open (dummy_fname, "w")
        include_line = "#include <{}>".format (library)
        dummy_c.write (textwrap.dedent(include_line + """
            int main () {
                return 0;
            }
            """))
        dummy_c.close ()

        rules_fname = ex ('mktemp', ret_stdout=True, echo=False)
        ex ("gcc -M -MF {} -xc {}".format (rules_fname, dummy_fname), echo=False)

        awk_prg = r"""
        BEGIN {
            RS="\\";
            OFS="\n"
        }
        {
            for(i=1;i<=NF;i++)
                if ($i ~ /^\//) {print $i}
        }
        """
        awk_prg = ex_escape(awk_prg)
        res = ex ("awk '{}' {}".format(awk_prg, rules_fname), echo=False, ret_stdout=True)
        return res.split('\n')[2]

    aliases = {}
    kc_names = {}
    names_kc = {}
    evdev_include = open (find_system_lib ("linux/input-event-codes.h"))
    for line in evdev_include:
        line = line.strip()
        m = re.search (r'#define\s+(KEY_\S+)\s+(\S+)', line)
        if m != None:
            try:
                value = int (m.group (2), 0)
                kc_names[value] = m.group(1)
                names_kc[m.group(1)] = value
            except:
                aliases[m.group(1)] = m.group (2)

    for alias, original in aliases.items():
        # KEY_CNT is defined with '#define KEY_CNT (KEY_MAX+1)'. Which means it
        # will be in the aliases dictionary, but it won't have a keycode in
        # names_kc. Looking up this alias we will throw an exception in which
        # case we do nothing.
        try:
            kc_names[names_kc[original]] += "/" + alias
        except:
            pass

    keycode_names = open ("keycode_names.h", "w")
    keycode_names.write ("// File automatically generated using './pymk generate_keycode_names'\n")
    keycode_names.write ("char *keycode_names[KEY_CNT] = {0};\n\n")
    keycode_names.write ("void init_keycode_names () {\n")
    for kc, name in sorted(kc_names.items()):
        keycode_names.write ('    keycode_names[{}] = "{}";\n'.format(kc, name))
    keycode_names.write ("}\n")

if __name__ == "__main__":
    # Everything above this line will be executed for each TAB press.
    # If --get_completions is set, handle_tab_complete() calls exit().
    handle_tab_complete ()

    pymk_default()

