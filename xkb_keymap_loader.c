/*
 * Copiright (C) 2018 Santiago León O.
 */

// Reverts the process of xkb_keymap_install() creating a string_t with the
// content of an .xkb file from which this layout can be installed.
//
// NOTE: This function assumes layout_name is a custom layout installed by this
// program. Be sure to get the name from xkb_keymap_list().
// NOTE: This function is NOT optimized, it was written to be called only when
// choosing which layout to work with.
string_t reconstruct_installed_custom_layout_str (const char *layout_name)
{
    xmlDocPtr metadata_doc = xmlParseFile("/usr/share/X11/xkb/rules/evdev.xml");
    xmlNodePtr node = xmlDocGetRootElement (metadata_doc);
    node = xml_get_child (node, "layoutList");
    node = xml_get_child (node, "layout");
    bool found = false;
    xmlNodePtr data_node = NULL;
    while (node != NULL && !found) {
        data_node = xml_get_child (node, "configItem");
        if (data_node != NULL) {
            xmlNodePtr name_node = xml_get_child (data_node, "name");
            xmlChar *name = xmlNodeGetContent(name_node);
            if (xmlStrcmp (name, (const xmlChar *)layout_name) == 0) {
                found = true;
            }
            xmlFree (name);
        }
        node = xml_get_sibling (node->next, "layout");
    }

    string_t res = {0};
    {
        str_cat_c (&res, "// Name: ");
        xmlNodePtr content_node = xml_get_child (data_node, "name");
        xmlChar *content = xmlNodeGetContent(content_node);
        str_cat_c (&res, (char *)content);
        str_cat_c (&res, "\n");
        xmlFree (content);
    }

    // TODO: The following options should be optional
    {
        str_cat_c (&res, "// Description: ");
        xmlNodePtr content_node = xml_get_child (data_node, "description");
        xmlChar *content = xmlNodeGetContent(content_node);
        str_cat_c (&res, (char *)content);
        str_cat_c (&res, "\n");
        xmlFree (content);
    }

    {
        str_cat_c (&res, "// Short description: ");
        xmlNodePtr content_node = xml_get_child (data_node, "shortDescription");
        xmlChar *content = xmlNodeGetContent(content_node);
        str_cat_c (&res, (char *)content);
        str_cat_c (&res, "\n");
        xmlFree (content);
    }

    {
        str_cat_c (&res, "// Languages: ");
        xmlNodePtr languages_list_node = xml_get_child (data_node, "languageList");
        xmlNodePtr lang = xml_get_child (languages_list_node, "iso639Id");

        do {
            xmlChar *content = xmlNodeGetContent(lang);
            str_cat_c (&res, (char *)content);
            xmlFree (content);

            lang = xml_get_sibling (lang->next, "iso639Id");
            if (lang == NULL) break;
            str_cat_c (&res, ", ");
        } while (1);

        str_cat_c (&res, "\n");
    }

    str_cat_c (&res, "\nxkb_keymap {\n");
    {
        mem_pool_t local_pool = {0};
        string_t filename = str_new ("/usr/share/X11/xkb/");
        int root_dir_len = str_len (&filename);
        char *section;

        str_put_c (&filename, root_dir_len, "keycodes/");
        str_cat_c (&filename, layout_name);
        str_cat_c (&filename, "_k");
        section = full_file_read (&local_pool, str_data (&filename));
        str_cat_c (&res, section);
        str_cat_c (&res, "\n");

        str_put_c (&filename, root_dir_len, "types/");
        str_cat_c (&filename, layout_name);
        str_cat_c (&filename, "_t");
        section = full_file_read (&local_pool, str_data (&filename));
        str_cat_c (&res, section);
        str_cat_c (&res, "\n");

        str_put_c (&filename, root_dir_len, "compat/");
        str_cat_c (&filename, layout_name);
        str_cat_c (&filename, "_c");
        section = full_file_read (&local_pool, str_data (&filename));
        str_cat_c (&res, section);
        str_cat_c (&res, "\n");

        str_put_c (&filename, root_dir_len, "symbols/");
        str_cat_c (&filename, layout_name);
        section = full_file_read (&local_pool, str_data (&filename));
        str_cat_c (&res, section);
        str_cat_c (&res, "\n");

        // We don't add a geometry section because it has been deprecated.

        mem_pool_destroy (&local_pool);
        str_free (&filename);
    }
    str_cat_c (&res, "\n};\n");

    xmlFreeDoc (metadata_doc);
    return res;
}

// Same as reconstruct_installed_custom_layout_str() but allocates the result
// inside the pool, (or malloc's it if pool==NULL).
char* reconstruct_installed_custom_layout (mem_pool_t *pool, const char *layout_name)
{
    string_t res = reconstruct_installed_custom_layout_str (layout_name);
    char *retval = pom_strndup (pool, str_data (&res), str_len (&res));
    str_free (&res);
    return retval;
}
