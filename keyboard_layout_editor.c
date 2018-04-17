/*
 * Copiright (C) 2018 Santiago León O.
 */

#include "common.h"

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

int main (int argc, char *argv[])
{
    xkb_keymap_install (argv[1], "/usr/share/X11/xkb", "my_layout");
}
