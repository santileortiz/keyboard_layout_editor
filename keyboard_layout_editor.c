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

bool xkb_keymap_install (const char *keymap_path, const char *dest_dir, const char *layout_name)
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
    char *name;
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

bool xkb_keymap_info_install (struct keymap_t *keymap)
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
            bool updated = false;
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

        full_file_write (res, res_len, db_path);

    } else {
        success = false;
        printf ("Failed to load keymap info database: %s.\n", db_path);
    }

    str_free (&new_layout_str);
    mem_pool_destroy (&pool);
    return success;
}

int main (int argc, char *argv[])
{
    //xkb_keymap_install (argv[1], "/usr/share/X11/xkb", "my_layout");

    struct keymap_t keymap = {0};
    keymap.name = "my_layout";
    keymap.short_description = "su";
    keymap.description = "US layout with Spanish characters";
    keymap.languages = (char *[]){"es", "us"};
    keymap.num_languages = 2;
    xkb_keymap_info_install (&keymap);
}
