/*
 * Copiright (C) 2018 Santiago León O.
 */

#include "common.h"

// Parses a block of the form
// <block_id> ["<block_name>"] {<block_content>};
//
// NULL can be passed to arguments if we are not interested in a specific part
// of the block.
//
// NOTE: This functon does not allocate anything. Resulting pointers point into
// the given string s.
bool parse_xkb_block (char *s,
                      char **block_id, size_t *block_id_size,
                      char **block_name, size_t *block_name_size,
                      char **block_content, size_t *block_content_size)
{
    bool success = true;
    s = consume_spaces (s);
    char *id = s;
    int id_size = 0;
    while (*s && !is_space (s)) {
        s++;
        id_size++;
    }

    s = consume_spaces (s);
    char *name = NULL;
    int name_size = 0;
    if (*s == '"') {
        s++;
        name = s;
        while (*s && *s != '"') {
            s++;
            name_size++;
        }
    }

    s = consume_spaces (s);
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
                s++; // consume }
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

    s = consume_spaces (s);
    if (*s != ';') {
        printf ("%c\n", *s);
        success = false;
        printf ("Missing ; at the end of block.\n");
    }

    if (*s == '\0') {
        success = false;
        printf ("Unexpected end of file.\n");
    }

    if (block_id != NULL) {
        assert (block_id_size != NULL);
        *block_id = id;
        *block_id_size = id_size;
    }

    if (block_name != NULL) {
        assert (block_name_size != NULL);
        *block_name = name;
        *block_name_size = name_size;
    }

    if (block_content != NULL) {
        assert (block_content_size != NULL);
        *block_content = content;
        *block_content_size = content_size;
    }

    return success;
}

int main (int argc, char *argv[])
{
    mem_pool_t pool = {0};
    char *keymap = full_file_read (&pool, argv[1]);

    char *keymap_id;
    size_t keymap_id_size;

    char *keymap_content;
    size_t keymap_content_size;
    parse_xkb_block (keymap, &keymap_id, &keymap_id_size, NULL, NULL, &keymap_content, &keymap_content_size);
    printf ("%.*s\n", (int)keymap_id_size, keymap_id);
    printf ("%.*s\n", (int)keymap_content_size, keymap_content);
}
