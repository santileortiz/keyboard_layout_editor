from mkpy.utility import *
import textwrap

# Maybe move this into mkpy?
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

