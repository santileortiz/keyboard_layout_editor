/*
 * Copiright (C) 2018 Santiago León O.
 */

#include <libxml/parser.h>
#include <libxml/xmlsave.h>
#include <libxml/xmlwriter.h>

/////////////////
// DOCUMENTATION
//
// This file implements functions to install and uninstall .xkb files on a
// system. There is a lot of discussion to be had about what the implementation
// details of this should be so this file will change a lot over time, but
// ideally the API will be as simple as possible, so it maybe can be integrated
// into other projects like elementary's Keyboard Plug.
//
// NOTE: Words keymap and layout are used indistinctly to reffer to the same
// thing: configuration of how keys on a keyboard are interpreted.

// Installs the .xkb file specified by keymap_path. It must be a file containing
// all keymap components except maybe geometry which will be ignored. Metadata
// for the keymap (name, description, etc.) is gathered from the initial
// comments in the file. Check custom_keyboard.xkb to see an example. If info is
// passed each non null component will override the information in the file's
// initial comment.
//
// Discussion:
//
//   I'm not sure which characters are allowed in keymap names so that it works
//   fine everywhere (gsettings, setxkbmap, libxkbcommon, switchboard,
//   indicators). For now, we read the name of the keymap as the rest of the
//   metadata. It would be simpler to use the .xkb filename as name, but I don't
//   want to impose restrictions on the name of files that can be loaded, nor
//   say the name is the filename but changing - for _ or something like that. 
bool xkb_keymap_install (char *keymap_path, struct keyboard_layout_info_t *info);

// Uninstalls the keymap named layout_name from the system, if it was installed
// with xkb_keymap_install().
bool xkb_keymap_uninstall (char *layout_name);

// Reverts all changes done to the system while installing keymaps. Which
// includes uninstalling all custom keymaps installed with xkb_keymap_install().
//
// Discussion:
//
//   The only reason this exists is that, at the moment we are adding extra
//   information to system files that may cause update conflicts with the
//   package that provides keymap data. This function reverts all changes done
//   to those directories, leaving them in the original state. In a world where
//   keymaps can be installed locally this function may not exist or it's
//   implementation could just remove a directory under home.
bool xkb_keymap_uninstall_everything ();

// Returns the information of custom layouts installed in the system. Sets res
// to a point to a list of layout information structs and res_len to the length
// of such list. The array and the strings are allocated inside pool. The pool
// argument is mandatory.
void xkb_keymap_list (mem_pool_t *pool, struct keyboard_layout_info_t **res, int *res_len);

// Same as xkb_keymap_list() but the resulting list contains information of
// layouts installed in the system by default from the XKeyboardConfig database.
void xkb_keymap_list_default (mem_pool_t *pool, struct keyboard_layout_info_t **res, int *res_len);

//////////////////
// IMPLEMENTATION
//

// TODO: Maybe move back to common.c? It looks like there is a place for these
// one use functions that don't require keeping any state for parsing anywhere.
// Maybe we can keep the more generig consume_to_any_char() and implement
// consume_line() as a call to it.
static inline
char* consume_line (char *c)
{
    while (*c && *c != '\n') {
           c++;
    }

    if (*c) {
        c++;
    }

    return c;
}

#define C_STR(str) str,((str)!=NULL?strlen(str):0)

static inline
bool strneq (char *str1, uint32_t str1_size, char* str2, uint32_t str2_size)
{
    if (str1_size != str2_size) {
        return false;
    } else {
        return (strncmp (str1, str2, str1_size) == 0);
    }
}

#define XKB_CMPNT_TABLE \
    XKB_PARSER_COMPAT_ROW(XKB_CMPNT_KEYCODES, "keycodes", "_k") \
    XKB_PARSER_COMPAT_ROW(XKB_CMPNT_TYPES,    "types",    "_t") \
    XKB_PARSER_COMPAT_ROW(XKB_CMPNT_COMPAT,   "compat",   "_c") \
    XKB_PARSER_COMPAT_ROW(XKB_CMPNT_SYMBOLS,  "symbols",  "")

enum xkb_cmpnt_symbols_t {
#define XKB_PARSER_COMPAT_ROW(symbol, name, suffix) symbol,
    XKB_CMPNT_TABLE
#undef XKB_PARSER_COMPAT_ROW
    XKB_CMPNT_NUM_SYMBOLS
};

char *xkb_cmpnt_names[] = {
#define XKB_PARSER_COMPAT_ROW(symbol, name, suffix) name,
    XKB_CMPNT_TABLE
#undef XKB_PARSER_COMPAT_ROW
};

char *xkb_cmpnt_suffixes[] = {
#define XKB_PARSER_COMPAT_ROW(symbol, name, suffix) suffix,
    XKB_CMPNT_TABLE
#undef XKB_PARSER_COMPAT_ROW
};

#undef XKB_CMPNT_TABLE

void str_put_xkb_component_fname (string_t *str, size_t pos, char *layout_name, enum xkb_cmpnt_symbols_t cmpnt)
{
    str_put_printf (str, pos, "%s%s", layout_name, xkb_cmpnt_suffixes[cmpnt]);
}

void str_put_xkb_component_path (string_t *str, size_t pos, char *layout_name, enum xkb_cmpnt_symbols_t cmpnt)
{
    str_put_printf (str, pos, "%s/", xkb_cmpnt_names[cmpnt]);
    str_put_xkb_component_fname (str, str_len(str), layout_name, cmpnt);
}

bool xkb_keymap_xkb_install (struct keyboard_layout_t *keymap, char *dest_dir)
{
    bool success = true;
    // TODO: Do we want to check that this layout name is available? or we will
    // just overwrite it. Probably better to overwrite it at this level, at a
    // higher lever we can warn that an overwrite will happen.

    // FIXME: Using our internal writer here broke things, for some reason the
    // system doesn't like our layout. I did some testing by reverting the
    // components to the original copy and paste of the keymap blocks. The
    // compat section works fine. The problem is either the keycodes or the
    // sybols section.
    //   
    //      1. Test if the problem is in the keycodes section or in the symbols
    //      section. I currently can't test them separately because I renamed
    //      keycodes. Go back to the original keycode names, I wanted to do this
    //      anyway because they are easier to read.
    //
    //      2. The problem could be this keycode renaming, but maybe it isn't.
    //      In this case the problem is in the symbols section, here we will
    //      need to see what is causing the problem. It could be either one of
    //      these:
    //         - Action statements embedded per symbol.
    //         - Internally the system doesn't read our compat section and
    //         instead has a hardcoded "complete" compat, in which case stuff
    //         may be conflicting with out symbols section. This would be BAD.
    struct xkb_writer_state_t state = {0};
    state.real_modifiers = xkb_get_real_modifiers_mask (keymap);
    // Create a reverse mapping of the modifier mapping in the internal
    // representation.
    create_reverse_modifier_name_map (keymap, state.reverse_modifier_definition);

    string_t dest_file = str_new (dest_dir);
    if (str_last (&dest_file) != '/') {
        str_cat_c (&dest_file, "/");
    }
    int dest_dir_end = str_len (&dest_file);

    string_t section_buffer = {0};
    if (success) {
        str_set (&section_buffer, "");
        str_put_xkb_component_path (&dest_file, dest_dir_end, keymap->info.name, XKB_CMPNT_KEYCODES);
        xkb_file_write_keycodes (&state, keymap, &section_buffer);
        ensure_path_exists (str_data (&dest_file));
        full_file_write (str_data(&section_buffer), str_len(&section_buffer), str_data(&dest_file));

        str_set (&section_buffer, "");
        str_put_xkb_component_path (&dest_file, dest_dir_end, keymap->info.name, XKB_CMPNT_TYPES);
        xkb_file_write_types (&state, keymap, &section_buffer);
        ensure_path_exists (str_data (&dest_file));
        full_file_write (str_data(&section_buffer), str_len(&section_buffer), str_data(&dest_file));

        str_set (&section_buffer, "");
        str_put_xkb_component_path (&dest_file, dest_dir_end, keymap->info.name, XKB_CMPNT_COMPAT);
        xkb_file_write_compat (&state, keymap, &section_buffer);
        ensure_path_exists (str_data (&dest_file));
        full_file_write (str_data(&section_buffer), str_len(&section_buffer), str_data(&dest_file));

        // TODO: CRITICAL!!!! My fear about systems not loading every component
        // or at least having some hardcoded components seems to be true. In
        // elementary OS having actions in the symbols section breaks things.
        // Removing action statements from the symbols section fixes things
        // which wouldn't be that bad if it weren't for the fact that modifiers
        // STILL WORK, even with our almost empty compatibility section. If they
        // were just using the compatibility section we provide, there is no way
        // the system could know where modifiers are mapped, and it was able to
        // detect I have caps and ctrl flipped (note that I don't use the option
        // for this, I just reassign the key definition of the keycode).
        //
        // This could mean several things and we really need to
        // investigate what's going on here because it will affect how we
        // actually redefine modifier keys (which is the only purpose of the
        // compat section).
        //
        // 1) The system is hardcoding a 'complete' compatibility section. There
        // are two alternatives here:
        //
        //    a) They overwrite whatever compatibility we set inthe evdev rules
        //    and use theirs.
        //    b) They somehow merge their compatibility section with ours.
        //
        // 2) Modifier mapping is handled by a completely different layer of
        // code, maybe in GNOME?.
        //
        // Alternatives related to 1 are cumbersome because now we need to know
        // when things will conflict with the existing compatibility section,
        // there is a chance we won't be able to do some stuff. If the reality
        // is the situation in 2, then I doubt we will be able to provide any
        // modifier remapping at all, besides changing keycodes already
        // associated to a modifier key.
        //                                         - Santiago Sept 27, 2019
        //
        //
        // I did some more research and found out that the compatibility section
        // we install seems pretty much useless in elementary OS. I don't now
        // how or where the OS is messing up but I tried flipping keys just by
        // using a custom compatibility section and it didin't work. This limits
        // a lot how we can let the user work with modifiers. It means we can't
        // really expose the full flexibility of modifier remappings to the
        // user.
        //
        // Effectively we must assume there is a fixed mapping between modifiers
        // and certain keysyms like Caps_Lock and Control_L. Where is this
        // mapping being set? and what is the actual mapping? are things I will
        // need to track down so we only let users change things in a way that
        // will work in the system.
        //
        // This may also mean that indicator (led) configuration is also fixed
        // and bound to specific keysyms.
        //
        // The next step is to look at the system code. The best thing for us
        // would be this to be a bug in Gala, but it can also be g-s-d, Mutter
        // or Clutter...
        //                                         - Santiago Sept 29, 2019
        //
        // :actions_in_symbols_cause_problems
        str_set (&section_buffer, "");
        str_put_xkb_component_path (&dest_file, dest_dir_end, keymap->info.name, XKB_CMPNT_SYMBOLS);
        xkb_file_write_symbols (&state, keymap, &section_buffer, false);
        ensure_path_exists (str_data (&dest_file));
        full_file_write (str_data(&section_buffer), str_len(&section_buffer), str_data(&dest_file));
    }

    str_free (&section_buffer);
    str_free (&dest_file);
    return success;
}

xmlNodePtr xml_get_child (xmlNodePtr node, char *child_name)
{
    xmlNodePtr curr_node = node->children;
    while (curr_node != NULL && xmlStrcmp(curr_node->name, (const xmlChar *)child_name) != 0) {
        curr_node = curr_node->next;
    }
    return curr_node;
}

xmlNodePtr xml_get_sibling (xmlNodePtr node, char *sibling_name)
{
    while (node != NULL && xmlStrcmp(node->name, (const xmlChar *)sibling_name) != 0) {
        node = node->next;
    }
    return node;
}

void xml_print_subtree (xmlNodePtr node)
{
    xmlBufferPtr buff = xmlBufferCreate ();
    xmlSaveCtxtPtr save_ctx = xmlSaveToBuffer (buff, "UTF-8", 0);
    xmlSaveTree (save_ctx, node);
    xmlSaveClose (save_ctx);

    printf ("%.*s\n", buff->use, buff->content);
    xmlBufferFree (buff);
}

// Searches str for the first occurence of substr, then creates a copy of str
// where data is inserted before the line where substr was found.
//
// NOTE: If substr is not found returns NULL.
char* insert_string_before_line (mem_pool_t *pool, char *str, char *substr,
                                 char *data, size_t *len)
{
    char *s = strstr (str, substr);

    char *retval = NULL;
    if (s) {
        while (*s != '\n') {
            s--;
        }
        s++;
        string_t res = strn_new (str, s - str);
        str_cat_c (&res, data);
        str_cat_c (&res, s);
        retval = pom_strndup (pool, str_data(&res), str_len (&res));
        if (len != NULL) {
            *len = str_len (&res);
        }
        str_free (&res);
    }
    return retval;
}

char* insert_string_after_line (mem_pool_t *pool, char *str, char *substr,
                                char *data, size_t *len)
{
    char *s = strstr (str, substr);

    char *retval = NULL;
    if (s) {
        s = consume_line (s);
        string_t res = strn_new (str, s - str);
        str_cat_c (&res, data);
        str_cat_c (&res, s);
        retval = pom_strndup (pool, str_data(&res), str_len (&res));
        if (len != NULL) {
            *len = str_len (&res);
        }
        str_free (&res);
    }
    return retval;
}

bool xkb_keymap_info_install (struct keyboard_layout_info_t *keymap, bool *new_layout)
{
    // Currently, as far as I know, systems don't look for keymap metadata
    // anywhere else other than /usr/share/X11/xkb/rules/evdev.xml. This
    // function installs the metadata in keymap into this system file.
    //
    // We avoid name conflicts with existing layouts by prefixing our installed
    // files. If there is a custom keymap with the same name, then we update the
    // metadata.
    //
    // Custom keyboard metadata is added as children to the 'layoutList' xml
    // node. To separate custom from default layouts, custom keymap info is
    // wrapped around with comments as follows
    //
    //      <layoutList>
    //        <!--CUSTOM LAYOUTS START-->
    //        ... layout nodes for custom keymaps ...
    //        <!--CUSTOM LAYOUTS END-->
    //        ... default layout nodes ...
    //      </layoutList>
    //
    //  Sadly, this CAN break applications that parse evdev.xml, because in xml
    //  comments are nodes. An application that does not ignore them will crash,
    //  if it tries to read the children of a comment, thinking it was a
    //  'layout' node. Let's hope these applications either ignore comments, or
    //  use something like XPath. So far everything is working fine.
    //

    bool success = true;
    const char *db_path = "/usr/share/X11/xkb/rules/evdev.xml";
    mem_pool_t pool = {0};


    string_t installed_name = {0};
    str_put_xkb_component_fname (&installed_name, 0, keymap->name, XKB_CMPNT_SYMBOLS);

    // Build a new <layout> node from keymap
    string_t new_layout_str = {0};
    {
        xmlBufferPtr buff = xmlBufferCreate ();
        xmlTextWriterPtr wrtr = xmlNewTextWriterMemory (buff, 0);
        xmlTextWriterSetIndent (wrtr, 1);
        xmlTextWriterSetIndentString (wrtr, (const xmlChar *)"  ");
        xmlTextWriterStartElement (wrtr, (const xmlChar *)"layout");
        xmlTextWriterStartElement (wrtr, (const xmlChar *)"configItem");
        xmlTextWriterWriteElement (wrtr, (const xmlChar *)"name",
                                   (const xmlChar *)(str_data(&installed_name)));
        xmlTextWriterWriteElement (wrtr, (const xmlChar *)"shortDescription",
                                   (const xmlChar *)keymap->short_description);
        xmlTextWriterWriteElement (wrtr, (const xmlChar *)"description",
                                   (const xmlChar *)keymap->description);
        xmlTextWriterStartElement (wrtr, (const xmlChar *)"languageList");
        int i;
        for (i=0; i<keymap->num_languages; i++) {
            xmlTextWriterWriteElement (wrtr, (const xmlChar *)"iso639Id",
                                       (const xmlChar *)keymap->languages[i]);
        }
        xmlTextWriterEndElement (wrtr);
        xmlTextWriterEndElement (wrtr);
        xmlTextWriterEndElement (wrtr);
        xmlFreeTextWriter (wrtr);

        // Manually indent the resulting XML. I was unable to find a way to do this
        // with libxml.
        str_set(&new_layout_str, "    ");
        char *s = pom_strndup (&pool, (const char*)buff->content, buff->use);
        char *end;
        while (*s) {
            end = strstr (s, "\n");
            if (end) {
                end++;
                if (*end != '\0') {
                    strn_cat_c (&new_layout_str, s, end - s);
                    str_cat_c (&new_layout_str, "    ");
                } else {
                    str_cat_c (&new_layout_str, s);
                }
            }
            s = end;
        }
        xmlBufferFree (buff);
    }

    char *res;
    size_t res_len;
    bool updated = false;
    char *db = full_file_read (&pool, db_path);
    if (db) {
        char *s = strstr (db, "<!--CUSTOM LAYOUTS START-->");
        if (s) {
            s = strstr (s, "<!--CUSTOM LAYOUTS END-->");
        }
        if (s) {
            char *default_start = consume_line(s);
            char *default_end = strstr(default_start, "</layoutList>");

            string_t default_layouts = str_new ("<layoutList>");
            strn_cat_c (&default_layouts, default_start, default_end - default_start);
            str_cat_c (&default_layouts, "</layoutList>");

            xmlDocPtr default_xml = xmlParseMemory(str_data(&default_layouts),
                                                   str_len(&default_layouts));
            xmlNodePtr curr_layout = xmlDocGetRootElement (default_xml); // layoutList node
            curr_layout = xml_get_sibling (curr_layout->children, "layout");
            while (curr_layout != NULL && success) {
                xmlNodePtr configItem = xml_get_child (curr_layout, "configItem");
                if (configItem != NULL) {
                    xmlNodePtr name_node = xml_get_child (configItem, "name");
                    xmlChar *name = xmlNodeGetContent(name_node);
                    if (xmlStrcmp (name, (const xmlChar *)(str_data(&installed_name))) == 0) {
                        success = false;
                    }
                    xmlFree (name);
                }
                curr_layout = xml_get_sibling (curr_layout->next, "layout");
            }
            xmlFreeDoc (default_xml);

            // If keymap is already a custom layout update it.
            //
            // Look for a <layout> node with <name> == str_data(&installed_name)
            // in the full XML database, if we find it, update it. It's
            // guearanteed to be a  custom layout because these were the only
            // ones ignored.
            xmlDocPtr out_xml = xmlParseFile (db_path);
            {
                xmlDocPtr new_node_doc = xmlParseMemory (str_data(&new_layout_str),
                                                         str_len(&new_layout_str));
                xmlNodePtr new_node = xmlDocGetRootElement (new_node_doc);
                curr_layout = xmlDocGetRootElement (out_xml); // xkbConfigRegistry
                curr_layout = xml_get_sibling (curr_layout->children, "layoutList");
                curr_layout = xml_get_sibling (curr_layout->children, "layout");
                while (curr_layout != NULL && success && !updated) {
                    xmlNodePtr configItem = xml_get_child (curr_layout, "configItem");
                    if (configItem != NULL) {
                        xmlNodePtr name_node = xml_get_child (configItem, "name");
                        xmlChar *name = xmlNodeGetContent(name_node);
                        if (xmlStrcmp (name, (const xmlChar *)(str_data(&installed_name))) == 0) {
                            xmlAddPrevSibling (curr_layout, new_node);
                            xmlUnlinkNode (curr_layout);
                            xmlFreeNode (curr_layout);
                            updated = true;
                        }
                        xmlFree (name);
                    }
                    curr_layout = xml_get_sibling (curr_layout->next, "layout");
                }
                xmlFreeDoc (new_node_doc);
            }

            // Generate the new XML database and write it out.
            //
            // If we updated an existing custom layout, then get the file from the
            // modified xmlDoc. If nothing was updated then just insert the generated
            // node as the last custom layout.
            if (updated) {
                xmlBufferPtr buff = xmlBufferCreate ();
                xmlSaveCtxtPtr save_ctx = xmlSaveToBuffer (buff, "UTF-8", 0);
                xmlSaveDoc (save_ctx, out_xml);
                xmlSaveClose (save_ctx);

                res = pom_strndup (&pool, (const char*)buff->content, buff->use);
                res_len = buff->use;
                xmlBufferFree (buff);
            } else {
                res = insert_string_before_line (&pool, db,
                                                 "<!--CUSTOM LAYOUTS END-->",
                                                 str_data(&new_layout_str), &res_len);
            }
            xmlFreeDoc (out_xml);
            str_free (&default_layouts);
        } else {
            // There are no custom layouts. Write comments for the first time
            // with the new layout node.
            string_t res_str = str_new ("    <!--CUSTOM LAYOUTS START-->\n");
            str_cat_c (&res_str, "    <!--\n"
                       "    These layouts were installed by keyboard_layout_editor, these comments\n"
                       "    are used to keep track of them. Keep them at the beginning of <layoutList>.\n"
                       "    -->\n");
            str_cat (&res_str, &new_layout_str);
            str_cat_c (&res_str, "    <!--CUSTOM LAYOUTS END-->\n");
            res = insert_string_after_line (&pool, db, "<layoutList>",
                                            str_data(&res_str),
                                            &res_len);
        }

        if (full_file_write (res, res_len, db_path)) {
            success = false;
        }

    } else {
        success = false;
        printf ("Failed to load keymap info database: %s.\n", db_path);
    }

    if (new_layout != NULL) {
        if (updated) {
            *new_layout = false;
        } else {
            *new_layout = true;
        }
    }

    str_free (&new_layout_str);
    mem_pool_destroy (&pool);
    return success;
}

static inline
void str_right_pad (string_t *str, int n)
{
    while (str_len(str) < n) {
        str_cat_c (str, " ");
    }
}

bool xkb_keymap_rules_install (char *keymap_name)
{
    // Build the rule that will be installed
    string_t new_rule = {0};
    {
        int col_size = MAX (2 + strlen (keymap_name), strlen("! layout")) + 1;

        // TODO: It would be nice to have padding in a printf like syntax
        string_t decl = str_new ("! layout");
        str_right_pad (&decl, col_size);
        str_cat_c (&decl, "= ");
        size_t decl_len = str_len (&decl);

        string_t value = {0};
        str_set_printf (&value, "  %s = %s", keymap_name, keymap_name);
        size_t value_len = str_len (&value);

        for (enum xkb_cmpnt_symbols_t cmpnt = 0; cmpnt < XKB_CMPNT_NUM_SYMBOLS; cmpnt++) {
            str_put_printf (&decl, decl_len, "%s\n", xkb_cmpnt_names[cmpnt]);
            str_put_printf (&value, value_len, "%s\n", xkb_cmpnt_suffixes[cmpnt]);

            str_cat (&new_rule, &decl);
            str_cat (&new_rule, &value);
        }
        str_cat_c (&new_rule, "\n");

        str_free (&decl);
        str_free (&value);
    }

    bool success = true;
    mem_pool_t pool = {0};
    char *res;
    size_t res_len;
    char *path = "/usr/share/X11/xkb/rules/evdev";
    char *db = full_file_read (&pool, path);
    if (db) {
        char *s = strstr (db, "CUSTOM LAYOUTS START");
        if (s) {
            s = strstr (s, "CUSTOM LAYOUTS END");
        }
        if (s) {
            res = insert_string_before_line (&pool, db, "CUSTOM LAYOUTS END",
                                             str_data(&new_rule),
                                             &res_len);
        } else {
            string_t new_install = str_new ("// CUSTOM LAYOUTS START\n");
            str_cat_c (&new_install, "// These rules were added by keyboard_layout_editor.\n\n");
            str_cat (&new_install, &new_rule);
            str_cat_c (&new_install, "// CUSTOM LAYOUTS END\n\n");
            res = insert_string_before_line (&pool, db, "// PC models",
                                             str_data(&new_install),
                                             &res_len);
            str_free (&new_install);
        }

        if (full_file_write (res, res_len, path)) {
            success = false;
        }
    }
    str_free (&new_rule);

    mem_pool_destroy (&pool);
    return success;
}

// Ideally, the installation of a new keymap should be as simple as copying a
// file to some local configuration directory. A bit less idealy we could copy
// the keymap as a .xkb file, then add metadata somewhere else like evdev.xml.
// Sadly as far as I can tell none of these can be accomplished with the state
// of current systems. At the moment the process of making a full .xkb file to
// a system is as follows:
//
//   1. Split the .xkb file into it's components (symbols, types, compat and
//      keycodes) and install each of them in the corresponding folder under
//      /usr/share/X11/xkb/.
//   2. Install metadata into /usr/share/X11/xkb/evdev.xml from which systems
//      will know the existance of the keymap.
//   3. Install rules into /usr/share/X11/xkb/evdev to lin together the
//      components of the .xkb file that were installed.
//
// This process has several drawbacks:
//   - Requires administrator privileges.
//   - Changes files from a system package (xkeyboard-config), maybe blocking
//     upgrades.
//   - The code required is more complex than necessary.
//   - Changes are made for all users in a system.
//
// The path towards a simpler system will require making some changes upstream
// and talking with people from other projects. Here are the facts I've gathered
// so far:
//
//   - The current layout installation makes the command 'setxkbmap my_layout'
//     do the correct thing, and load all installed components. This was tested
//     by swapping keys using the keycodes component.
//
//   - From reading the API and it's source code, libxkbcommon can search for
//     keymap definitions from multiple base directories. Actually, ~/.xkb is a
//     default search directory. But, for some reason just installing a keymap
//     there and calling setxkbmap doesn't work. More research is needed here as
//     several things may be happening: The window manager is not using
//     libxkbcommon, the WM uses libxkbcommon but changes the default
//     directories, testing with setxkbmap does not relate to libxkbcommon.
//     Depending on what causes this we may need to create patches for each WM
//     (Gala, Gnome Shell), or a single patch to Mutter.
//
//   - Configuring a keymap in the shell in Gnome is done by using the gsettings
//     schema /org/gnome/desktop/input-sources/, 'sources' includes a list of
//     layout names and 'current' chooses the index for the active one. But we
//     have to take into account that Gnome has added another schema for this
//     functionality /org/gnome/libgnomekbd/keyboard/ in libgnomekbd. Things may
//     move here soon.
//
//   - Keymap metadata is not handled by libxkbcommon. Applications seem to read
//     some of it from /usr/share/X11/xkb/evdev.xml. Still, there is no
//     consensus on which metadata is shown to the user to choose the right
//     layout. Sometimes the description is used, others a list of languages,
//     elementary for example shows descriptions as if they were language names.
//     There is also no consensus on what the layout indicator shows, sometimes
//     it's the short description, others the first 2 letters of the layout
//     name. More research is required here too, at check the settings panel
//     and layout indicator for Gala and Gnome.
//
//   - I have not done any research on KDE based desktops, but it 'should' be
//     similar, changing gsettings for configuration files.
//
//                                                  Santiago (April 20, 2018)
//
bool xkb_keymap_install (char *keymap_path, struct keyboard_layout_info_t *info)
{

    bool success = true;

    mem_pool_t pool = {0};
    char *xkb_file_content = full_file_read (&pool, keymap_path);
    struct keyboard_layout_t keymap = {0};
    if (!xkb_file_parse (xkb_file_content, &keymap)) {
        success = false;
    }

    if (info) {
        if (info->name) {
            keymap.info.name = pom_strdup (&keymap.pool, info->name);
        }

        if (info->description) {
            keymap.info.description = pom_strdup (&keymap.pool, info->description);
        }

        if (info->short_description) {
            keymap.info.short_description = pom_strdup (&keymap.pool, info->short_description);
        }

        if (info->languages && info->num_languages > 0) {
            keymap.info.num_languages = info->num_languages;
            keymap.info.languages =
                mem_pool_push_array (&keymap.pool, info->num_languages, char*);

            for (int i=0; i<info->num_languages; i++) {
                keymap.info.languages[i] = pom_strdup (&keymap.pool, info->languages[i]);
            }
        }
    }

    // Check we are not overwriting a layout from the default database.
    if (success) {
        struct keyboard_layout_info_t *info_list = NULL;
        int num_layouts = 0;
        xkb_keymap_list_default (&pool, &info_list, &num_layouts);
        for (int i=0; i<num_layouts && success; i++) {
            if (strcmp (keymap.info.name, info_list[i].name) == 0) {
                printf ("The XKeyboardConfig database also contains a layout named '%s'. Use the --name option to install it with a different name.\n", keymap.info.name);
                success = false;
            }
        }
    }

    bool new_layout;
    if (success) {
        success = xkb_keymap_info_install (&keymap.info, &new_layout);
    }

    if (success && new_layout) {
        success = xkb_keymap_rules_install (keymap.info.name);
    }

    if (success) {
        success = xkb_keymap_xkb_install (&keymap, "/usr/share/X11/xkb");
    }

    keyboard_layout_destroy (&keymap);
    mem_pool_destroy (&pool);
    return success;
}

// Returns a newly allocated string inside pool that has everything between
// lines containing start and end (including both) removed. If res_len is not
// null it's set to the length of the resulting string.
//
// If either end or start are not found, NULL is returned.
//
// NOTE: Substring start is looked up first, then end is searched for after the
// first occurrence of start.
char* delete_lines (mem_pool_t *pool, char *str,
                   char *start, char *end, size_t *res_len)
{
    char *s = strstr (str, start);
    if (s == NULL) {
        printf ("Error deleting lines from string, start not found.\n");
        return NULL;
    }

    char *e = strstr (s, end);
    if (e == NULL) {
        printf ("Error deleting lines from string, end not found.\n");
        return NULL;
    }

    while (*s != '\n') {
        s--;
    }
    s++;

    // The -1 avoids consuming an extra line if end has '\n' as final character.
    e += strlen (end) - 1;
    e = consume_line (e);

    string_t res_str = strn_new (str, s - str);
    str_cat_c (&res_str, e);
    char *res = pom_strndup (pool, str_data(&res_str), str_len (&res_str));
    if (res_len != NULL) {
        *res_len = str_len (&res_str);
    }
    str_free (&res_str);
    return res;
}

void get_info_from_layoutList (string_t *layoutList_xml,
                               mem_pool_t *pool, struct keyboard_layout_info_t **res, int *res_len)
{
    assert (pool != NULL && res != NULL && res_len != NULL);

    int num_layouts = 0;
    struct keyboard_layout_info_t *layouts = NULL;

    xmlDocPtr default_xml = xmlParseMemory(str_data(layoutList_xml),
                                           str_len(layoutList_xml));
    xmlNodePtr curr_layout = xmlDocGetRootElement (default_xml); // layoutList node
    num_layouts = xmlChildElementCount (curr_layout);
    layouts = pom_push_size (pool, sizeof (struct keyboard_layout_info_t)*num_layouts);
    int layout_count = 0;
    curr_layout = xml_get_sibling (curr_layout->children, "layout");
    while (curr_layout != NULL) {
        xmlNodePtr configItem = xml_get_child (curr_layout, "configItem");
        if (configItem != NULL) {
            {
                xmlNodePtr content_node = xml_get_child (configItem, "name");
                xmlChar *content = xmlNodeGetContent(content_node);
                layouts[layout_count].name =
                    pom_strndup (pool, (const char*)content, strlen((char*)content));
                xmlFree (content);
            }

            {
                xmlNodePtr content_node = xml_get_child (configItem, "description");
                xmlChar *content = xmlNodeGetContent(content_node);
                layouts[layout_count].description =
                    pom_strndup (pool, (const char*)content, strlen((char*)content));
                xmlFree (content);
            }

            {
                xmlNodePtr content_node = xml_get_child (configItem, "shortDescription");
                xmlChar *content = xmlNodeGetContent(content_node);
                layouts[layout_count].short_description =
                    pom_strndup (pool, (const char*)content, strlen((char*)content));
                xmlFree (content);
            }

            {
                xmlNodePtr languages_list_node = xml_get_child (configItem, "languageList");

                if (languages_list_node != NULL) {
                    xmlNodePtr lang = xml_get_child (languages_list_node, "iso639Id");
                    struct ptrarr_t languages = {0};
                    layouts[layout_count].num_languages = 0;
                    do {
                        xmlChar *content = xmlNodeGetContent(lang);
                        char* lang_str = pom_strdup (pool, (const char*)content);
                        ptrarr_push (&languages, lang_str);
                        xmlFree (content);

                        layouts[layout_count].num_languages++;
                        lang = xml_get_sibling (lang->next, "iso639Id");
                        if (lang == NULL) break;
                    } while (1);

                    layouts[layout_count].languages =
                        pom_push_size (pool, sizeof(char*)*layouts[layout_count].num_languages);
                    int i;
                    for (i=0; i<layouts[layout_count].num_languages; i++) {
                        layouts[layout_count].languages[i] = languages.data[i];
                    }
                    ptrarr_free (&languages);
                }
            }

            layout_count++;
        }
        curr_layout = xml_get_sibling (curr_layout->next, "layout");
    }
    xmlFreeDoc (default_xml);

    *res = layouts;
    *res_len = num_layouts;
}

void xkb_keymap_list_default (mem_pool_t *pool, struct keyboard_layout_info_t **res, int *res_len)
{
    if (res == NULL || res_len == NULL) {
        return;
    }

    mem_pool_t local_pool = {0};
    char *metadata_path = "/usr/share/X11/xkb/rules/evdev.xml";
    char *metadata = full_file_read (&local_pool, metadata_path);

    string_t default_layouts;
    char *layoutList_start = strstr (metadata, "<layoutList>");

    char *s = strstr (layoutList_start, "CUSTOM LAYOUTS START");
    if (s) {
        while (*s != '\n') {
            s--;
        }

        char *e = strstr (s, "CUSTOM LAYOUTS END");
        assert (e != NULL && "invalid evdev.xml format. Didn't find CUSTOM LAYOUTS END");
        e = consume_line (e);

        char *layoutList_end = strstr (metadata, "</layoutList>");
        assert (layoutList_end != NULL && "invalid evdev.xml format. Didn't find </layoutList>");
        layoutList_end = consume_line (layoutList_end);

        default_layouts = strn_new (layoutList_start, s - layoutList_start);
        strn_cat_c (&default_layouts, e, layoutList_end - e);

    } else {
        char *layoutList_end = strstr (metadata, "</layoutList>");
        assert (layoutList_end != NULL && "invalid evdev.xml format. Didn't find </layoutList>");
        layoutList_end = consume_line (layoutList_end);

        default_layouts = strn_new (layoutList_start, layoutList_end - layoutList_start);
    }

    get_info_from_layoutList (&default_layouts, pool, res, res_len);

    str_free (&default_layouts);
    mem_pool_destroy (&local_pool);
}

void xkb_keymap_list (mem_pool_t *pool, struct keyboard_layout_info_t **res, int *res_len)
{
    if (res == NULL || res_len == NULL) {
        return;
    }

    mem_pool_t local_pool = {0};
    char *metadata_path = "/usr/share/X11/xkb/rules/evdev.xml";
    char *metadata = full_file_read (&local_pool, metadata_path);

    char *s = strstr (metadata, "CUSTOM LAYOUTS START");
    if (s) {
        s = consume_line (s);
        char *e = strstr (metadata, "CUSTOM LAYOUTS END");
        while (*e != '\n') {
            e--;
        }

        string_t default_layouts = str_new ("<layoutList>");
        strn_cat_c (&default_layouts, s, e - s);
        str_cat_c (&default_layouts, "</layoutList>");

        get_info_from_layoutList (&default_layouts, pool, res, res_len);

    } else {
        // There are no custom layouts
    }

    mem_pool_destroy (&local_pool);
}

bool xkb_keymap_components_remove (char *layout_name)
{
    bool success = true;
    string_t xkb_file = str_new ("/usr/share/X11/xkb/");
    size_t xkb_root_end = str_len (&xkb_file);

    for (enum xkb_cmpnt_symbols_t cmpnt = 0; cmpnt < XKB_CMPNT_NUM_SYMBOLS; cmpnt++) {
        str_put_xkb_component_path (&xkb_file, xkb_root_end, layout_name, cmpnt);
        if (unlink (str_data(&xkb_file)) == -1) {
            success = false;
            if (errno != EACCES) {
                printf ("Error deleting %s: %s\n", str_data (&xkb_file), strerror (errno));
            }
        }
    }

    str_free (&xkb_file);
    return success;
}

bool xkb_keymap_uninstall (char *layout_name)
{
    bool success = true;
    mem_pool_t pool = {0};
    struct keyboard_layout_info_t *custom_layouts;
    int num_custom_layouts;
    xkb_keymap_list (&pool, &custom_layouts, &num_custom_layouts);
    bool found = false;
    int i;
    for (i=0; i<num_custom_layouts; i++) {
        if (strcmp (layout_name, custom_layouts[i].name) == 0) {
            found = true;
            break;
        }
    }
    if (!found) {
        success = false;
        printf ("Could not find layout to be uninstalled.\n");
    }

    // Delete installed .xkb components.
    if (success) {
        success = xkb_keymap_components_remove (layout_name);
    }

    // Remove installed xkb rules
    if (success) {
        int col_size = MAX (2 + strlen(layout_name), strlen("! layout")) + 1;
        string_t mrkr = str_new ("! layout");
        str_right_pad (&mrkr, col_size);
        str_cat_printf (&mrkr, "= %s\n  ", xkb_cmpnt_names[0]);
        size_t line_len = str_len (&mrkr);
        str_cat_c (&mrkr, layout_name);
        str_right_pad (&mrkr, line_len + col_size - 2 /*spaces after \n*/);
        str_cat_c (&mrkr, "= ");
        str_cat_c (&mrkr, layout_name);
        str_cat_c (&mrkr, xkb_cmpnt_suffixes[0]);

        char *rules_path = "/usr/share/X11/xkb/rules/evdev";
        char *rules = full_file_read (&pool, rules_path);
        size_t new_file_len;
        char *new_file = delete_lines (&pool, rules, str_data(&mrkr), "\n\n", &new_file_len);
        if (new_file == NULL) {
            success = false;
        }

        if (success && full_file_write (new_file, new_file_len, rules_path)) {
            success = false;
        }
        str_free (&mrkr);
    }

    // Remove installed metadata
    if (success) {
        bool done = false;
        char *metadata_path = "/usr/share/X11/xkb/rules/evdev.xml";
        xmlDocPtr out_xml = xmlParseFile (metadata_path);
        xmlNodePtr curr_layout = xmlDocGetRootElement (out_xml); // xkbConfigRegistry
        curr_layout = xml_get_sibling (curr_layout->children, "layoutList");
        curr_layout = xml_get_sibling (curr_layout->children, "layout");
        while (curr_layout != NULL && success && !done) {
            xmlNodePtr configItem = xml_get_child (curr_layout, "configItem");
            if (configItem != NULL) {
                xmlNodePtr name_node = xml_get_child (configItem, "name");
                xmlChar *name = xmlNodeGetContent(name_node);
                if (xmlStrcmp (name, (const xmlChar *)layout_name) == 0) {
                    // Delete indentation node
                    xmlNodePtr tmp = curr_layout->next;
                    xmlUnlinkNode (curr_layout->next);
                    xmlFreeNode (tmp);

                    // Delete actual <layout> node
                    xmlNodePtr to_delete = curr_layout;
                    curr_layout = curr_layout->prev;
                    xmlUnlinkNode (to_delete);
                    xmlFreeNode (to_delete);
                    done = true;
                }
                xmlFree (name);
            }
            curr_layout = xml_get_sibling (curr_layout->next, "layout");
        }

        xmlSaveCtxtPtr save_ctx = xmlSaveToFilename (metadata_path, "UTF-8", 0);
        xmlSaveDoc (save_ctx, out_xml);
        xmlSaveClose (save_ctx);
        xmlFreeDoc (out_xml);
    }

    mem_pool_destroy (&pool);
    return success;
}

// Removes everything we changed in the system's xkb configuration folder.
bool xkb_keymap_uninstall_everything ()
{
    bool success = true;
    mem_pool_t pool = {0};
    struct keyboard_layout_info_t *custom_layouts;
    int num_custom_layouts;
    xkb_keymap_list (&pool, &custom_layouts, &num_custom_layouts);
    int i;
    for (i=0; i<num_custom_layouts; i++) {
        xkb_keymap_components_remove (custom_layouts[i].name);
    }

    // Remove installed xkb rules
    if (success) {
        char *rules_path = "/usr/share/X11/xkb/rules/evdev";
        char *rules = full_file_read (&pool, rules_path);
        size_t new_file_len;
        char *new_file = delete_lines (&pool, rules,
                                       "\n\n// CUSTOM LAYOUTS START", "CUSTOM LAYOUTS END", &new_file_len);
        if (new_file == NULL) {
            success = false;
        }

        if (success && full_file_write (new_file, new_file_len, rules_path)) {
            success = false;
        }
    }

    // Remove installed metadata
    if (success) {
        char *metadata_path = "/usr/share/X11/xkb/rules/evdev.xml";
        char *metadata = full_file_read (&pool, metadata_path);
        size_t new_file_len;
        char *new_file = delete_lines (&pool, metadata,
                                       "CUSTOM LAYOUTS START", "CUSTOM LAYOUTS END", &new_file_len);
        if (new_file == NULL) {
            success = false;
        }

        if (success && full_file_write (new_file, new_file_len, metadata_path)) {
            success = false;
        }
    }

    mem_pool_destroy (&pool);
    return success;
}

struct gsettings_layout_t {
    char *type;
    char *name;
};

bool xkb_keymap_get_active (mem_pool_t *pool, struct gsettings_layout_t *res)
{
    assert (res != NULL);
    bool success = false;

    GSettings *gtk_keyboard_settings = g_settings_new ("org.gnome.desktop.input-sources");

    // Get index of active layout
    uint32_t layout_idx;
    {
        GVariant *current_v = g_settings_get_value (gtk_keyboard_settings, "current");
        layout_idx = g_variant_get_uint32 (current_v);
        g_variant_unref (current_v);
    }


    // Get the layout name and type at position layout_idx
    {
        GVariant *layout_list = g_settings_get_value (gtk_keyboard_settings, "sources");
        GVariantIter *layout_list_it = g_variant_iter_new (layout_list);
        GVariant *layout_tuple;

        int idx = 0;
        while (idx < layout_idx &&
               (layout_tuple = g_variant_iter_next_value (layout_list_it))) {
            idx++;
            g_variant_unref (layout_tuple);
        }

        layout_tuple = g_variant_iter_next_value (layout_list_it);
        if (layout_tuple) {
            GVariantIter *layout_tuple_it = g_variant_iter_new (layout_tuple);

            GVariant *layout_type_v = g_variant_iter_next_value (layout_tuple_it);
            GVariant *layout_name_v = g_variant_iter_next_value (layout_tuple_it);

            const char *layout_type = g_variant_get_string (layout_type_v, NULL);
            res->type = pom_strdup (pool, layout_type);

            const char *layout_name = g_variant_get_string (layout_name_v, NULL);
            res->name = pom_strdup (pool, layout_name);
            success = true;

            g_variant_unref (layout_type_v);
            g_variant_unref (layout_name_v);

            g_variant_iter_free (layout_tuple_it);
            g_variant_unref (layout_tuple);
        }

        g_variant_iter_free (layout_list_it);
        g_variant_unref (layout_list);
    }

    g_object_unref (gtk_keyboard_settings);

    return success;
}

#define xkb_keymap_set_active(name) xkb_keymap_set_active_full("xkb",name)
bool xkb_keymap_set_active_full (char *type, char *name)
{
    GSettings *gtk_keyboard_settings = g_settings_new ("org.gnome.desktop.input-sources");

    GVariant *layout_list = g_settings_get_value (gtk_keyboard_settings, "sources");

    // Lookup the index of the layout name
    bool found = false;
    int layout_idx = 0;
    {
        GVariantIter *layout_list_it = g_variant_iter_new (layout_list);
        GVariant *layout_tuple;

        while (!found &&
               (layout_tuple = g_variant_iter_next_value (layout_list_it))) {
            GVariantIter *layout_tuple_it = g_variant_iter_new (layout_tuple);

            GVariant *layout_type_v = g_variant_iter_next_value (layout_tuple_it);
            GVariant *layout_name_v = g_variant_iter_next_value (layout_tuple_it);

            const char *layout_type = g_variant_get_string (layout_type_v, NULL);
            const char *layout_name = g_variant_get_string (layout_name_v, NULL);
            if (strcmp (layout_type, type) == 0 && strcmp (layout_name, name) == 0) {
                found = true;
            }
            g_variant_unref (layout_type_v);
            g_variant_unref (layout_name_v);

            if (!found) {
                layout_idx++;
            }

            g_variant_iter_free (layout_tuple_it);
            g_variant_unref (layout_tuple);
        }

        g_variant_iter_free (layout_list_it);
    }

    // Set 'current' key to the value of the found index
    if (found) {
        g_settings_set_uint (gtk_keyboard_settings, "current", layout_idx);
    }

    g_variant_unref (layout_list);
    g_object_unref (gtk_keyboard_settings);

    return found;
}

bool xkb_keymap_add_to_gsettings (char *name)
{
    GSettings *gtk_keyboard_settings = g_settings_new ("org.gnome.desktop.input-sources");

    GVariant *layout_list = g_settings_get_value (gtk_keyboard_settings, "sources");

    GVariantBuilder builder;
    bool found = false;

    g_variant_builder_init (&builder, G_VARIANT_TYPE (g_variant_get_type_string (layout_list)));

    GVariantIter *layout_list_it = g_variant_iter_new (layout_list);
    GVariant *layout_tuple;

    while (!found &&
           (layout_tuple = g_variant_iter_next_value (layout_list_it))) {
        GVariantIter *layout_tuple_it = g_variant_iter_new (layout_tuple);

        GVariant *layout_type_v = g_variant_iter_next_value (layout_tuple_it);
        GVariant *layout_name_v = g_variant_iter_next_value (layout_tuple_it);

        const char *layout_type = g_variant_get_string (layout_type_v, NULL);
        const char *layout_name = g_variant_get_string (layout_name_v, NULL);
        if (strcmp (layout_type, "xkb") == 0 && strcmp (layout_name, name) == 0) {
            found = true;
        }

        // Add the current tuple to the new layout list
        char *tuple_str = g_variant_print (layout_tuple, FALSE);
        g_variant_builder_add_parsed (&builder, tuple_str);
        g_free (tuple_str);

        g_variant_unref (layout_type_v);
        g_variant_unref (layout_name_v);

        g_variant_iter_free (layout_tuple_it);
        g_variant_unref (layout_tuple);
    }

    g_variant_iter_free (layout_list_it);

    if (!found) {
        g_variant_builder_add_parsed (&builder, "('xkb', %s)", name);
        GVariant *new_list = g_variant_builder_end (&builder);
        g_settings_set_value (gtk_keyboard_settings, "sources", new_list);
    }

    g_variant_unref (layout_list);
    g_object_unref (gtk_keyboard_settings);

    return false;
}

bool xkb_keymap_remove_from_gsettings (char *name)
{
    // TODO: Implement this.
    return false;
}
