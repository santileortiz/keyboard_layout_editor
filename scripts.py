from mkpy.utility import *
import re, textwrap

# Maybe move this into mkpy?
def find_system_lib (library, lib_name=''):
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

    lib_flags = ''
    if lib_name != '':
        lib_flags = ex ('pkg-config --cflags --libs {}'.format(lib_name), ret_stdout=True, echo=False)

    rules_fname = ex ('mktemp', ret_stdout=True, echo=False)

    ex ("gcc -M -MF {} -xc {} {}".format(
        rules_fname, dummy_fname, lib_flags), echo=False)

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

def header_define_to_struct_array_header (header_file,
        type_name, val_type, array_name, output_path, comment, prefix_to_strip=''):
    """
    This function takes a header file that uses #define to create named
    constants and creates a C header file at output_path that defines an array
    in C of type_name structs. This struct holds the macro name as the 'name'
    member and the vaule it was defined to as the 'val' member.

    The comment parameter will be the start of the file. This is used to notify
    the users this file was automatically generated.
    """

    definitions = []
    regex = re.compile (r'^#define\s+{}(\S+)\s+(\S+)'.format(prefix_to_strip))
    num_cols = regex.groups
    col_width = [0]*num_cols

    avg_len = 0
    for line in header_file:
        m = regex.match (line.strip())
        if m != None:
            groups = m.groups()
            for i, val in enumerate(groups):
                if i == 0:
                    avg_len += len(val)

                col_width[i] = max(col_width[i], len(val))

            definitions.append (groups)

    res = []
    for definition in definitions:
        col = []
        for i, val in enumerate(definition):
            if (i == 0):
                col.append ('{0:{width}}'.format ('"'+val+'",', width=col_width[i]+3))
            else:
                col.append ('{0:{width}}'.format (val, width=col_width[i]))

        res.append ('    {'+''.join (col)+'}')

    out_file = open (output_path, "w")
    out_file.write (comment + '\n')
    out_file.write (textwrap.dedent (
                    r'''
                    struct ''' + type_name + r''' {
                        const char *name;
                        ''' + val_type + r''' val;
                    };

                    '''))

    # Maybe these can be useful in the future? or maybe not...
    #out_file.write ('int keysym_names_avg_len = {width};\n'.format (width=int(avg_len/len(res))))
    #out_file.write ('int keysym_names_max_len = {width};\n'.format (width=col_width[0]))
    out_file.write ('static const struct ' + type_name + ' ' + array_name + '[] = {\n')
    out_file.write (',\n'.join(res))
    out_file.write ('\n};')

