/*
 * Copiright (C) 2018 Santiago León O.
 */

#include "common.h"
#include <libxml/parser.h>
#include <libxml/xmlsave.h>
#include <libxml/xmlwriter.h>

static inline
bool is_blank (char *c) {
    return *c == ' ' ||  (*c >= '\t' && *c <= '\r');
}

static inline
char *consume_blanks (char *c)
{
    while (is_blank(c)) {
        c++;
    }
    return c;
}

// Parses a block of the form
// <block_id> ["<block_name>"] {<block_content>};
//
// NULL can be passed to arguments if we are not interested in a specific part
// of the block.
//
// Returns a pointer to the first character of the line after the block, or NULL
// if an error occured.
//
// NOTE: This functon does not allocate anything. Resulting pointers point into
// the given string s.
char* parse_xkb_block (char *s,
                      char **block_id, size_t *block_id_size,
                      char **block_name, size_t *block_name_size,
                      char **block_content, size_t *block_content_size)
{
    bool success = true;
    s = consume_blanks (s);
    char *id = s;
    int id_size = 0;
    while (*s && !is_blank (s)) {
        s++;
        id_size++;
    }

    s = consume_blanks (s);
    char *name = NULL;
    int name_size = 0;
    if (*s == '\"') {
        s++;
        name = s;
        while (*s && *s != '\"') {
            s++;
            name_size++;
        }
        s++;
    }

    s = consume_blanks (s);
    char *content = NULL;
    int content_size = 0;
    if (*s == '{') {
        int brace_cnt = 1;
        s++;
        content = s;
        while (*s) {
            if (*s == '{') {
                brace_cnt++;
            } else if (*s == '}') {
                brace_cnt--;
            }
            

            if (brace_cnt == 0) {
                s++; // consume '}'
                break;
            } else {
                s++;
                content_size++;
            }
        }
    } else {
        success = false;
        printf ("Block with invalid content.\n");
    }

    s = consume_blanks (s);
    if (*s != ';') {
        success = false;
        printf ("Missing ; at the end of block.\n");
    }

    if (*s == '\0') {
        success = false;
        printf ("Unexpected end of file.\n");
    }

    s++; // consume ';'

    if (block_id != NULL) {
        *block_id = id;
    }
    if (block_id_size != NULL) {
        *block_id_size = id_size;
    }

    if (block_name != NULL) {
        *block_name = name;
    }
    if (block_name_size != NULL) {
        *block_name_size = name_size;
    }

    if (block_content != NULL) {
        *block_content = content;
    }
    if (block_content_size != NULL) {
        *block_content_size = content_size;
    }

    if (!success) {
        return NULL;
    } else {
        return consume_line(s);
    }
}

#define C_STR(str) str,((str)!=NULL?strlen(str):0)

static inline
bool strneq (const char *str1, uint32_t str1_size, const char* str2, uint32_t str2_size)
{
    if (str1_size != str2_size) {
        return false;
    } else {
        return (strncmp (str1, str2, str1_size) == 0);
    }
}

bool xkb_keymap_xkb_install (const char *keymap_path, const char *dest_dir, const char *layout_name)
{
    bool success = true;
    mem_pool_t pool = {0};
    char *s = full_file_read (&pool, keymap_path);
    if (s != NULL) {

        string_t dest_file = str_new (dest_dir);
        if (str_last (&dest_file) != '/') {
            str_cat_c (&dest_file, "/");
        }
        int dest_dir_end = str_len (&dest_file);

        char *keymap_id;
        size_t keymap_id_size;
        parse_xkb_block (s, &keymap_id, &keymap_id_size, NULL, NULL, &s, NULL);
        if (!strneq (keymap_id, keymap_id_size, C_STR("xkb_keymap"))) {
            success = false;
        }

        while (*s && success) {
            char *block_name;
            size_t block_name_size;
            s = parse_xkb_block (s, &block_name, &block_name_size, NULL, NULL, NULL, NULL);
            if (s == NULL) {
                success = false;
                break;
            }

            if (strneq (block_name, block_name_size, C_STR("xkb_keycodes"))) {
                str_put_c (&dest_file, dest_dir_end, "keycodes/");
                str_cat_c (&dest_file, layout_name);
                str_cat_c (&dest_file, "_k");

            } else if (strneq (block_name, block_name_size, C_STR("xkb_types"))) {
                str_put_c (&dest_file, dest_dir_end, "types/");
                str_cat_c (&dest_file, layout_name);
                str_cat_c (&dest_file, "_t");

            } else if (strneq (block_name, block_name_size, C_STR("xkb_compatibility"))) {
                str_put_c (&dest_file, dest_dir_end, "compat/");
                str_cat_c (&dest_file, layout_name);
                str_cat_c (&dest_file, "_c");

            } else if (strneq (block_name, block_name_size, C_STR("xkb_symbols"))) {
                str_put_c (&dest_file, dest_dir_end, "symbols/");
                str_cat_c (&dest_file, layout_name);
            } else {
                success = false;
                break;
            }

            if (ensure_path_exists (str_data (&dest_file))) {
                // s -> pointer to end of parsed block.
                // block_name -> pointer to the beginning of the parsed block.
                if (full_file_write (block_name, s - block_name, str_data (&dest_file))) {
                    success = false;
                }
            } else {
                success = false;
            }
        }
        str_free (&dest_file);
    }

    mem_pool_destroy (&pool);
    return success;
}

struct keymap_t {
    const char *name;
    char *short_description;
    char *description;
    char **languages;
    int num_languages;
};

xmlNodePtr xml_get_child (xmlNodePtr node, const char *child_name)
{
    xmlNodePtr curr_node = node->children;
    while (curr_node != NULL && xmlStrcmp(curr_node->name, (const xmlChar *)child_name) != 0) {
        curr_node = curr_node->next;
    }
    return curr_node;
}

xmlNodePtr xml_get_sibling (xmlNodePtr node, const char *sibling_name)
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
char* insert_string_before_line (mem_pool_t *pool, const char *str, const char *substr,
                                 const char *data, size_t *len)
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

char* insert_string_after_line (mem_pool_t *pool, const char *str, const char *substr,
                                const char *data, size_t *len)
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

bool xkb_keymap_info_install (struct keymap_t *keymap, bool *new_layout)
{
    // Currently, as far as I know, systems don't look for keymap metadata
    // anywhere else other than /usr/share/X11/xkb/rules/evdev.xml. This
    // function installs the metadata in keymap into this system file.
    //
    // The function fails if the system by default has a keymap with the same
    // name as keymap->name. But, if there is a custom keymap with the same
    // name, then we update the metadata.
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
                                   (const xmlChar *)keymap->name);
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
        char *s = pom_strndup (&pool, buff->content, buff->use);
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
                    if (xmlStrcmp (name, (const xmlChar *)keymap->name) == 0) {
                        success = false;
                    }
                    xmlFree (name);
                }
                curr_layout = xml_get_sibling (curr_layout->next, "layout");
            }

            // If keymap is already a custom layout update it.
            //
            // Look for a <layout> node with <name> == keymap->name in the full XML
            // database, if we find it, update it. It's guearanteed to be a  custom
            // layout because these were the only ones ignored.
            xmlDocPtr new_node_doc = xmlParseMemory (str_data(&new_layout_str),
                                                     str_len(&new_layout_str));
            xmlNodePtr new_node = xmlDocGetRootElement (new_node_doc);
            xmlDocPtr out_xml = xmlParseFile (db_path);
            curr_layout = xmlDocGetRootElement (out_xml); // xkbConfigRegistry
            curr_layout = xml_get_sibling (curr_layout->children, "layoutList");
            curr_layout = xml_get_sibling (curr_layout->children, "layout");
            while (curr_layout != NULL && success && !updated) {
                xmlNodePtr configItem = xml_get_child (curr_layout, "configItem");
                if (configItem != NULL) {
                    xmlNodePtr name_node = xml_get_child (configItem, "name");
                    xmlChar *name = xmlNodeGetContent(name_node);
                    if (xmlStrcmp (name, (const xmlChar *)keymap->name) == 0) {
                        xmlAddPrevSibling (curr_layout, new_node);
                        xmlUnlinkNode (curr_layout);
                        xmlFreeNode (curr_layout);
                        updated = true;
                    }
                    xmlFree (name);
                }
                curr_layout = xml_get_sibling (curr_layout->next, "layout");
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

                res = pom_strndup (&pool, buff->content, buff->use);
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

bool xkb_keymap_rules_install (const char *keymap_name)
{
    // Build the rule that will be installed
    string_t new_rule = {0};
    {
        int col_size = MAX (2 + strlen (keymap_name), strlen("! layout")) + 1;

        string_t decl = str_new ("! layout");
        str_right_pad (&decl, col_size);
        str_cat_c (&decl, "= ");
        size_t decl_len = str_len (&decl);

        string_t value = str_new ("  ");
        str_cat_c (&value, keymap_name);
        str_right_pad (&value, col_size);
        str_cat_c (&value, "= ");
        str_cat_c (&value, keymap_name);
        size_t value_len = str_len (&value);

        str_put_c (&decl, decl_len, "types\n");
        str_put_c (&value, value_len, "_t\n");
        str_cat (&new_rule, &decl);
        str_cat (&new_rule, &value);

        str_put_c (&decl, decl_len, "keycodes\n");
        str_put_c (&value, value_len, "_k\n");
        str_cat (&new_rule, &decl);
        str_cat (&new_rule, &value);

        str_put_c (&decl, decl_len, "compat\n");
        str_put_c (&value, value_len, "_c\n");
        str_cat (&new_rule, &decl);
        str_cat (&new_rule, &value);

        str_put_c (&decl, decl_len, "symbols\n");
        str_put_c (&value, value_len, "\n\n");
        str_cat (&new_rule, &decl);
        str_cat (&new_rule, &value);
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
bool xkb_keymap_install (const char *keymap_path, const char *layout_name)
{
    bool success = true;
    struct keymap_t keymap = {0};
    keymap.name = layout_name;
    keymap.short_description = "su";
    keymap.description = "Test custom layout";
    keymap.languages = (char *[]){"es", "us"};
    keymap.num_languages = 2;

    bool new_layout;
    success = xkb_keymap_info_install (&keymap, &new_layout);
    if (success && new_layout) {
        success = xkb_keymap_rules_install (keymap.name);
    }

    if (success) {
        success = xkb_keymap_xkb_install (keymap_path, "/usr/share/X11/xkb", keymap.name);
    }

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
char* delete_lines (mem_pool_t *pool, const char *str,
                   const char *start, const char *end, size_t *res_len)
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
    e += strlen (end);
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

void get_custom_layout_names (mem_pool_t *pool, char ***res, int *res_len)
{
    if (res == NULL || res_len == NULL) {
        return;
    }

    mem_pool_t local_pool = {0};
    char *metadata_path = "/usr/share/X11/xkb/rules/evdev.xml";
    char *metadata = full_file_read (&local_pool, metadata_path);

    int num_layouts = 0;
    char **layouts = NULL;
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

        xmlDocPtr default_xml = xmlParseMemory(str_data(&default_layouts),
                                               str_len(&default_layouts));
        xmlNodePtr curr_layout = xmlDocGetRootElement (default_xml); // layoutList node
        num_layouts = xmlChildElementCount (curr_layout);
        layouts = pom_push_size (pool, sizeof (char *)*num_layouts);
        int layout_count = 0;
        curr_layout = xml_get_sibling (curr_layout->children, "layout");
        while (curr_layout != NULL) {
            xmlNodePtr configItem = xml_get_child (curr_layout, "configItem");
            if (configItem != NULL) {
                xmlNodePtr name_node = xml_get_child (configItem, "name");
                xmlChar *name = xmlNodeGetContent(name_node);
                layouts[layout_count] = pom_strndup (pool, name, strlen((char*)name));
                layout_count++;
                xmlFree (name);
            }
            curr_layout = xml_get_sibling (curr_layout->next, "layout");
        }

    } else {
        // There are no custom layouts
    }

    *res = layouts;
    *res_len = num_layouts;

    mem_pool_destroy (&local_pool);
}

bool xkb_keymap_components_remove (const char *layout_name)
{
    bool success = true;
    string_t xkb_file = str_new ("/usr/share/X11/xkb/");
    size_t xkb_root_end = str_len (&xkb_file);

    str_put_c (&xkb_file, xkb_root_end, "types/");
    str_cat_c (&xkb_file, layout_name);
    str_cat_c (&xkb_file, "_t");
    if (unlink (str_data(&xkb_file)) == -1) {
        success = false;
        printf ("Error deleting %s: %s\n", str_data (&xkb_file), strerror (errno));
    }

    str_put_c (&xkb_file, xkb_root_end, "keycodes/");
    str_cat_c (&xkb_file, layout_name);
    str_cat_c (&xkb_file, "_k");
    if (unlink (str_data(&xkb_file)) == -1) {
        success = false;
        printf ("Error deleting %s: %s\n", str_data (&xkb_file), strerror (errno));
    }

    str_put_c (&xkb_file, xkb_root_end, "compat/");
    str_cat_c (&xkb_file, layout_name);
    str_cat_c (&xkb_file, "_c");
    if (unlink (str_data(&xkb_file)) == -1) {
        success = false;
        printf ("Error deleting %s: %s\n", str_data (&xkb_file), strerror (errno));
    }

    str_put_c (&xkb_file, xkb_root_end, "symbols/");
    str_cat_c (&xkb_file, layout_name);
    if (unlink (str_data(&xkb_file)) == -1) {
        success = false;
        printf ("Error deleting %s: %s\n", str_data (&xkb_file), strerror (errno));
    }
    str_free (&xkb_file);
    return success;
}

bool xkb_keymap_uninstall (const char *layout_name)
{
    bool success = true;
    mem_pool_t pool = {0};
    char **custom_layouts;
    int num_custom_layouts;
    get_custom_layout_names (&pool, &custom_layouts, &num_custom_layouts);
    bool found = false;
    int i;
    for (i=0; i<num_custom_layouts; i++) {
        if (strcmp (layout_name, custom_layouts[i]) == 0) {
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
        str_cat_c (&mrkr, "= types\n  ");
        size_t line_len = str_len (&mrkr);
        str_cat_c (&mrkr, layout_name);
        str_right_pad (&mrkr, line_len + col_size - 2 /*spaces after \n*/);
        str_cat_c (&mrkr, "= ");
        str_cat_c (&mrkr, layout_name);
        str_cat_c (&mrkr, "_t");

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
                    xmlUnlinkNode (curr_layout);
                    xmlFreeNode (curr_layout);
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
    char **custom_layouts;
    int num_custom_layouts;
    get_custom_layout_names (&pool, &custom_layouts, &num_custom_layouts);
    int i;
    for (i=0; i<num_custom_layouts; i++) {
        xkb_keymap_components_remove (custom_layouts[i]);
    }

    // Remove installed xkb rules
    if (success) {
        char *rules_path = "/usr/share/X11/xkb/rules/evdev";
        char *rules = full_file_read (&pool, rules_path);
        size_t new_file_len;
        char *new_file = delete_lines (&pool, rules,
                                       "CUSTOM LAYOUTS START", "CUSTOM LAYOUTS END\n", &new_file_len);
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

int main (int argc, char *argv[])
{
#if 0
    if (xkb_keymap_install ("./custom_keyboard.xkb", "my_layout")) {
        return 0;
    } else {
        return 1;
    }
#else
//    if (xkb_keymap_uninstall ("my_layout")) {
//        return 0;
//    } else {
//        return 1;
//    }
    xkb_keymap_uninstall_everything ();
#endif
}
