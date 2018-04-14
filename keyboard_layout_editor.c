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

bool xkb_keymap_install (char *keymap_path)
{
    bool success = true;
    mem_pool_t pool = {0};
    char *s = full_file_read (&pool, keymap_path);

    char *keymap_id;
    size_t keymap_id_size;

    parse_xkb_block (s, &keymap_id, &keymap_id_size, NULL, NULL, &s, NULL);

    char *start, *end;
    if (s) {
        end = s = parse_xkb_block (s, &start, NULL, NULL, NULL, &s, NULL);
        full_file_write (start, end-start, "custom_k");
    } else { success = false; }

    if (s) {
        end = s = parse_xkb_block (s, &start, NULL, NULL, NULL, &s, NULL);
        full_file_write (start, end-start, "custom_t");
    } else { success = false; }

    if (s) {
        end = s = parse_xkb_block (s, &start, NULL, NULL, NULL, &s, NULL);
        full_file_write (start, end-start, "custom_c");
    } else { success = false; }

    if (s) {
        end = s = parse_xkb_block (s, &start, NULL, NULL, NULL, &s, NULL);
        full_file_write (start, end-start, "custom");
    } else { success = false; }

    mem_pool_destroy (&pool);
    return success;
}

int main (int argc, char *argv[])
{
    xkb_keymap_install (argv[1]);
}
