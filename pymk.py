#!/usr/bin/python3
from mkpy.utility import *
import re, textwrap
import scripts

assert sys.version_info >= (3,2)

modes = {
        'debug': '-O0 -g -Wall',
        'profile_debug': '-O2 -g -pg -Wall',
        'release': '-O2 -g -DNDEBUG -Wall'
        }
cli_mode = get_cli_arg_opt('-M,--mode', modes.keys())
FLAGS = modes[pers('mode', 'debug', cli_mode)]
GTK3_FLAGS = ex ('pkg-config --cflags --libs gtk+-3.0', ret_stdout=True, echo=False)
GTK2_FLAGS = ex ('pkg-config --cflags --libs gtk+-2.0', ret_stdout=True, echo=False)
ensure_dir ("bin")

def default():
    target = pers ('last_target', 'keyboard_layout_editor')
    call_user_function(target)

def keyboard_layout_editor ():
    ex ('glib-compile-resources data/gresource.xml --internal --generate-source --target=gresource.c')
    ex ('gcc {FLAGS} -o bin/keyboard-layout-editor keyboard_layout_editor.c -I/usr/include/libxml2 -lxml2 {GTK3_FLAGS} -lm -lxkbcommon')

def xkbcommon_view ():
    ex ('gcc {FLAGS} -o bin/xkbcommon-view libxkbcommon_view.c -I/usr/include/libxml2 -lxml2 {GTK3_FLAGS} -lm -lxkbcommon')

def im_gtk3 ():
    print ("BUILDING IM FOR GTK3")
    ex ('gcc -shared -fPIC {FLAGS} -o bin/im-kle-xkb.so im/kle_im_context.c im/kle_im_module.c {GTK3_FLAGS} -lm -lxkbcommon')
    ex ('chmod 644 bin/im-kle-xkb.so', echo=False)
    ex ('sudo cp bin/im-kle-xkb.so /usr/lib/x86_64-linux-gnu/gtk-3.0/3.0.0/immodules')
    ex ('sudo /usr/lib/x86_64-linux-gnu/libgtk-3-0/gtk-query-immodules-3.0 --update-cache')

def im_gtk2 ():
    print ("BUILDING IM FOR GTK2")
    ex ('gcc -shared -fPIC {FLAGS} -o bin/im-kle-xkb.so im/kle_im_context.c im/kle_im_module.c {GTK2_FLAGS} -lm -lxkbcommon')
    ex ('chmod 644 bin/im-kle-xkb.so', echo=False)
    ex ('sudo cp bin/im-kle-xkb.so /usr/lib/x86_64-linux-gnu/gtk-2.0/2.10.0/immodules')
    ex ('sudo /usr/lib/x86_64-linux-gnu/libgtk2.0-0/gtk-query-immodules-2.0 --update-cache')

def xkb_tests ():
    ex ('gcc {FLAGS} -o bin/xkb_tests tests/xkb_tests.c -I. -lm -lrt -lxkbcommon')

def generate_base_layout_tests ():
    """
    This target flattens out all available layouts from the installed
    XKeyboardConfig database and puts the output in ./tests/XKeyboardConfig/,
    the reason for doing this is that calling the ./tests/get_xkb_str.sh script
    every time we want them is slow. In fact, running this target will freeze
    the X11 server for about 4 minutes (the screen will freeze).
    """

    blacklist = [
            'dz',  # <SPCE> is of type EIGHT_LEVEL but the keymap never assigns ISO_Level5_Shift so levels 5,6,7,8 are broken.
            'mv',  # Maps 2 real modifiers to <MDSW>.
            'nec_vndr/jp' # Maps 2 real modifiers to <RALT>, also has 2 groups which we currently don't support.
            ]

    layout_names = ex ("./bin/keyboard-layout-editor --list-default", ret_stdout=True).split('\n')
    for name in layout_names:
        if name in blacklist:
            continue

        target_fname = './tests/XKeyboardConfig/' + name + '.xkb'

        # Getting this information makes everything O(n^2) because the
        # implementation of --show-info looks up the information of all layouts
        # and from that list it linearly searches for the passed layout name.
        # We could implement this whole test layout generation in C the 'right
        # way' if we ever implement support for include statements in our
        # parser. Then we wouldn't need to use the get_xkb_str.sh script. For
        # now I don't care that much because this stuff won't be called at
        # runtime.
        info_lines = ex ("./bin/keyboard-layout-editor --show-info " + name, ret_stdout=True).split('\n')
        for idx, line in enumerate(info_lines):
            if idx == 0:
                ex ("echo '// " + line + "' > " + target_fname)
            else:
                ex ("echo '// " + line + "' >> " + target_fname)

        ex ("echo" + " >> " + target_fname)
        ex ('./tests/get_xkb_str.sh ' + name + ' >> ' + target_fname)

def generate_keycode_names ():
    global g_dry_run
    if g_dry_run:
        return

    aliases = {}
    kc_names = {}
    names_kc = {}
    evdev_include = open (scripts.find_system_lib ("linux/input-event-codes.h"))
    for line in evdev_include:
        line = line.strip()
        m = re.search (r'^#define\s+(KEY_\S+)\s+(\S+)', line)
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

    keycode_names = open ("kernel_keycode_names.h", "w")
    keycode_names.write ("// File automatically generated using './pymk generate_keycode_names'\n")
    keycode_names.write ("char *kernel_keycode_names[KEY_CNT] = {0};\n\n")
    keycode_names.write ("void init_kernel_keycode_names () {\n")
    for kc, name in sorted(kc_names.items()):
        keycode_names.write ('    kernel_keycode_names[{}] = "{}";\n'.format(kc, name))
    keycode_names.write ("}\n")

def generate_keysym_names ():
    global g_dry_run
    if g_dry_run:
        return

    xkbcommon_keysyms_header = open (scripts.find_system_lib ("xkbcommon/xkbcommon-keysyms.h"))

    scripts.header_define_to_struct_array_header (xkbcommon_keysyms_header,
            'named_keysym_t', 'xkb_keysym_t',
            'keysym_names', 'keysym_names.h',
            "// File automatically generated using './pymk generate_keysym_names'", 'XKB_KEY_')

def generate_gdk_keysym_names ():
    global g_dry_run
    if g_dry_run:
        return

    xkbcommon_keysyms_header = open (scripts.find_system_lib ("gdk/gdkkeysyms.h", 'gdk-3.0'))

    scripts.header_define_to_struct_array_header (xkbcommon_keysyms_header,
            'named_gdk_keysym_t', 'guint',
            'gdk_keysym_names', 'gdk_keysym_names.h',
            "// File automatically generated using './pymk generate_gdk_keysym_names'")

if __name__ == "__main__":
    # Everything above this line will be executed for each TAB press.
    # If --get_completions is set, handle_tab_complete() calls exit().
    handle_tab_complete ()

    pymk_default()

