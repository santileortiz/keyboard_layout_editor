/*
 * Copiright (C) 2018 Santiago León O.
 */

char* kv_to_string (mem_pool_t *pool, struct keyboard_view_t *kv)
{
    // Set posix locale so the decimal separator is always .
    char *old_locale;
    old_locale = strdup (setlocale (LC_ALL, NULL));
    assert (old_locale != NULL);
    setlocale (LC_ALL, "POSIX");

    string_t str = {0};

    // Compute the number of digits in the real part of the biggest float so we
    // can create a buffer big enough and never read invalid memory.
    int buff_size = 0;
    {
        struct row_t *row = kv->first_row;
        while (row != NULL) {
            buff_size = MAX (buff_size, snprintf (NULL, 0, "%g", row->height));

            struct sgmt_t *sgmt = row->first_key;
            while (sgmt != NULL) {
                buff_size = MAX (buff_size, snprintf (NULL, 0, "%g", sgmt->width));
                buff_size = MAX (buff_size, snprintf (NULL, 0, "%g", sgmt->user_glue));
                buff_size = MAX (buff_size, snprintf (NULL, 0, "%g", sgmt->internal_glue));

                sgmt = sgmt->next_sgmt;
            }
            row = row->next_row;
        }
    }

    // Keycodes require at 3 bytes.
    buff_size = MAX(buff_size, 3);

    char buff[buff_size + 1];

    struct row_t *row = kv->first_row;
    while (row != NULL) {
        if (row->height != 1) {
            sprintf (buff, "%g", row->height);
            str_cat_c (&str, buff);
            str_cat_c (&str, " ");
        }

        struct sgmt_t *sgmt = row->first_key;
        while (sgmt != NULL) {
            if (!is_multirow_key (sgmt)) {
                str_cat_c (&str, "K(");
                snprintf (buff, ARRAY_SIZE(buff), "%i", sgmt->kc);
                str_cat_c (&str, buff);

                if (sgmt->width != 1) {
                    str_cat_c (&str, ", W: ");
                    snprintf (buff, ARRAY_SIZE(buff), "%g", sgmt->width);
                    str_cat_c (&str, buff);
                }

                if (sgmt->user_glue != 0) {
                    str_cat_c (&str, ", UG: ");
                    snprintf (buff, ARRAY_SIZE(buff), "%g", sgmt->user_glue);
                    str_cat_c (&str, buff);
                }

                str_cat_c (&str, ")");

            } else if (is_multirow_parent (sgmt)) {
                str_cat_c (&str, "P(");
                snprintf (buff, ARRAY_SIZE(buff), "%i", sgmt->kc);
                str_cat_c (&str, buff);

                if (sgmt->width != 1) {
                    str_cat_c (&str, ", W: ");
                    snprintf (buff, ARRAY_SIZE(buff), "%g", sgmt->width);
                    str_cat_c (&str, buff);
                }

                if (sgmt->user_glue != 0) {
                    str_cat_c (&str, ", UG: ");
                    snprintf (buff, ARRAY_SIZE(buff), "%g", sgmt->user_glue);
                    str_cat_c (&str, buff);
                }

                if (sgmt->internal_glue != 0) {
                    str_cat_c (&str, ", IG: ");
                    snprintf (buff, ARRAY_SIZE(buff), "%g", sgmt->internal_glue);
                    str_cat_c (&str, buff);
                }

                str_cat_c (&str, ")");

            } else {
                if (is_multirow_parent (sgmt->next_multirow)) {
                    str_cat_c (&str, "E(");
                } else {
                    str_cat_c (&str, "S(");
                }

                if (sgmt->type == KEY_MULTIROW_SEGMENT_SIZED) {
                    str_cat_c (&str, "W: ");
                    snprintf (buff, ARRAY_SIZE(buff), "%g", sgmt->width);
                    str_cat_c (&str, buff);

                    if (sgmt->align == MULTIROW_ALIGN_LEFT) {
                        str_cat_c (&str, ", L");
                    } else {
                        str_cat_c (&str, ", R");
                    }
                }

                if (sgmt->internal_glue != 0) {
                    if (sgmt->type == KEY_MULTIROW_SEGMENT_SIZED) {
                        str_cat_c (&str, ", ");
                    }

                    str_cat_c (&str, "IG: ");
                    snprintf (buff, ARRAY_SIZE(buff), "%g", sgmt->internal_glue);
                    str_cat_c (&str, buff);
                }

                str_cat_c (&str, ")");

            }

            sgmt = sgmt->next_sgmt;

            if (sgmt == NULL) {
                str_cat_c (&str, ";\n");
            } else {
                str_cat_c (&str, " ");
            }
        }
        row = row->next_row;
    }


    // Restore the original locale
    setlocale (LC_ALL, old_locale);
    free (old_locale);

    return pom_strdup (pool, str_data(&str));
}

void kv_print (struct keyboard_view_t *kv)
{
    mem_pool_t pool = ZERO_INIT (mem_pool_t);
    printf ("%s\n", kv_to_string (&pool, kv));
    mem_pool_destroy (&pool);
}

struct scanner_t {
    char *pos;
    bool is_eof;

    bool error;
    char *error_message;
};

// TODO: I still have to think about parsing optional stuff, sometimes we want
// to test something but not consume it. Maybe split testing and consuming one
// value creating something like scanner_consume_matched() that consumes
// everything matched so far, and something like scanner_reset() that goes back
// to the last position where we consumed something?. Another option is to
// "backup" the position before consuming these things, and if we want to go
// back, restore it (like memory pool markers). I need more experience with the
// API to know which is better for the user, or if there are other alternatives.

bool scanner_float (struct scanner_t *scnr, float *value)
{
    // TODO: Maybe allow value==NULL for the case when we want to consume
    // something but discard it's value.
    assert (value != NULL);
    if (scnr->error)
        return false;

    char *end;
    float res = strtof (scnr->pos, &end);
    if (res != 0 || scnr->pos != end) {
        *value = res;
        scnr->pos = end;

        if (*scnr->pos == '\0') {
            scnr->is_eof = true;
        }
        return true;
    }

    return false;
}

bool scanner_int (struct scanner_t *scnr, int *value)
{
    // TODO: Maybe allow value==NULL for the case when we want to consume
    // something but discard it's value.
    assert (value != NULL);
    if (scnr->error)
        return false;

    char *end;
    int res = strtol (scnr->pos, &end, 10);
    if (res != 0 || scnr->pos != end) {
        *value = res;
        scnr->pos = end;

        if (*scnr->pos == '\0') {
            scnr->is_eof = true;
        }
        return true;
    }

    return false;
}

void scanner_consume_spaces (struct scanner_t *scnr)
{
    while (isspace(*scnr->pos)) {
           scnr->pos++;
    }

    if (*scnr->pos == '\0') {
        scnr->is_eof = true;
    }
}

bool scanner_char (struct scanner_t *scnr, char c)
{
    if (scnr->error)
        return false;

    scanner_consume_spaces (scnr);

    if (*scnr->pos == c) {
        scnr->pos++;

        if (*scnr->pos == '\0') {
            scnr->is_eof = true;
        }

        return true;
    }

    return false;
}

bool scanner_str (struct scanner_t *scnr, char *str)
{
    assert (str != NULL);
    if (scnr->error)
        return false;

    scanner_consume_spaces (scnr);

    size_t len = strlen(str);
    if (strncmp(scnr->pos, str, len) == 0) {
        scnr->pos += len;

        if (*scnr->pos == '\0') {
            scnr->is_eof = true;
        }

        return true;
    }

    return false;
}

// NOTE: The error message is not duplicated or stored by the scanner, it just
// stores a pointer to it.
void scanner_set_error (struct scanner_t *scnr, char *error_message)
{
    scnr->error = true;
    scnr->error_message = error_message;
}

void parse_full_key_arguments (struct scanner_t *scnr, int *kc, float *width, float *user_glue)
{
    assert (kc != NULL && width != NULL && user_glue != NULL);

    *kc=0;
    *width = 1;
    *user_glue = 0;

    if (!scanner_int (scnr, kc)) {
        scanner_set_error (scnr, "Expected keycode.\n");
    }

    if (scanner_str (scnr, ", W:")) {
        if (!scanner_float (scnr, width)) {
            scanner_set_error (scnr, "Expected width.\n");
        }
    }

    if (scanner_str (scnr, ", UG:")) {
        if (!scanner_float (scnr, user_glue)) {
            scanner_set_error (scnr, "Expected user glue.\n");
        }
    }

    // Internal glue is parsed but ignored as the API to create keyboard layouts
    // does not allow setting it. Instead it expects the user to call
    // kv_compute_glue(). I currently think this is the right thing because I
    // don't want to bother the user of the API with something that can be
    // computed. Maybe it should be removed from the string format, although it
    // can be useful for debug purposes.
    // @parser_ignores_internal_glue
    if (scanner_str (scnr, ", IG:")) {
        float internal_glue;
        if (!scanner_float (scnr, &internal_glue)) {
            scanner_set_error (scnr, "Expected user glue.\n");
        }
    }

    if (!scanner_char (scnr, ')')) {
        scanner_set_error (scnr, "Missing ')'\n");
    }
}

void parse_key_sgmt_arguments (struct scanner_t *scnr, float *width, enum multirow_key_align_t *align)
{
    assert (width != NULL && align != NULL);
    // The default width of 0 will make the segment keep the width of the
    // previous multirow segment.
    *width = 0;
    // For segments that keep the width of the previous multirow segment, the
    // align property is ignored.
    *align = MULTIROW_ALIGN_LEFT;

    if (scanner_str (scnr, "W:")) {
        if (!scanner_float (scnr, width)) {
            scanner_set_error (scnr, "Expected width.\n");
        }

        if (scanner_char (scnr, ',')) {
            if (scanner_char (scnr, 'L')) {
                *align = MULTIROW_ALIGN_LEFT;
            } else if (scanner_char (scnr, 'R')) {
                *align = MULTIROW_ALIGN_RIGHT;
            } else {
                scanner_set_error (scnr, "Expected alignment value.\n");
            }

        } else {
            scanner_set_error (scnr, "Expected segment alignment.\n");
        }

        // @parser_ignores_internal_glue
        if (scanner_char (scnr, ',')) {
            float user_glue;
            if (!scanner_str (scnr, "IG:") || !scanner_float (scnr, &user_glue)) {
                scanner_set_error (scnr, "Expected internal glue value.\n");
            }
        }

    } else {
        // @parser_ignores_internal_glue
        if (scanner_str (scnr, "IG:")){
            float user_glue;
            if (!scanner_float (scnr, &user_glue)) {
                scanner_set_error (scnr, "Expected internal glue value.\n");
            }
        }
    }

    if (!scanner_char (scnr, ')')) {
        scanner_set_error (scnr, "Missing ')'\n");
    }
}

void kv_set_from_string (struct keyboard_view_t *kv, char *str)
{
    kv_clear (kv);
    struct geometry_edit_ctx_t ctx;
    kv_geometry_ctx_init_append (kv, &ctx);

    // Set posix locale so the decimal separator is always .
    char *old_locale;
    old_locale = strdup (setlocale (LC_ALL, NULL));
    assert (old_locale != NULL);
    setlocale (LC_ALL, "POSIX");

    struct scanner_t scnr = ZERO_INIT(struct scanner_t);
    scnr.pos = str;

    GList *multirow_list=NULL, *curr_multirrow = multirow_list;

    while (!scnr.is_eof && !scnr.error) {
        float row_height = 1;
        scanner_float (&scnr, &row_height);

        kv_new_row_h (&ctx, row_height);

        while (!scnr.is_eof) {
            if (scanner_str (&scnr, "K(")) {
                int kc;
                float width, user_glue;
                parse_full_key_arguments (&scnr, &kc, &width, &user_glue);

                if (!scnr.error) {
                    kv_add_key_full (&ctx, kc, width, user_glue);
                } else {
                    break;
                }

            } else if (scanner_str (&scnr, "P(")) {
                int kc;
                float width, user_glue;
                parse_full_key_arguments (&scnr, &kc, &width, &user_glue);

                if (!scnr.error) {
                    struct sgmt_t *new_parent = kv_add_key_full (&ctx, kc, width, user_glue);

                    multirow_list = g_list_insert_before (multirow_list, curr_multirrow, new_parent);

                } else {
                    break;
                }

            } else if (scanner_str (&scnr, "S(")) {
                float width;
                enum multirow_key_align_t align;
                parse_key_sgmt_arguments (&scnr, &width, &align);

                if (!scnr.error) {
                    kv_add_multirow_sized_sgmt (&ctx, curr_multirrow->data, width, align);
                    curr_multirrow = curr_multirrow->next;

                } else {
                    break;
                }

            } else if (scanner_str (&scnr, "E(")) {
                float width;
                enum multirow_key_align_t align;
                parse_key_sgmt_arguments (&scnr, &width, &align);

                if (!scnr.error) {
                    kv_add_multirow_sized_sgmt (&ctx, curr_multirrow->data, width, align);

                    assert (curr_multirrow != NULL);
                    GList *to_remove = curr_multirrow;
                    curr_multirrow = curr_multirrow->next;
                    multirow_list = g_list_remove_link (multirow_list, to_remove);
                    g_list_free (to_remove);

                } else {
                    break;
                }

            } else if (scanner_char (&scnr, ';')) {
                scanner_consume_spaces (&scnr);
                break;

            } else {
                scnr.error = true;
                scnr.error_message = "Error: expected key segment or ';'\n";
                break;
            }
        }

        assert (curr_multirrow == NULL);
        curr_multirrow = multirow_list;
    }

    assert (g_list_length (multirow_list) == 0);

    if (scnr.error) {
        printf ("%s", scnr.error_message);

    } else {
        kv_compute_glue (kv);
    }

    // Restore the original locale
    setlocale (LC_ALL, old_locale);
    free (old_locale);
}

bool kv_test_parser (struct keyboard_view_t *kv)
{
    mem_pool_t pool = ZERO_INIT(mem_pool_t);
    char *str1 = kv_to_string (&pool, app.keyboard_view);
    kv_set_from_string (app.keyboard_view, str1);
    char *str2 = kv_to_string (&pool, app.keyboard_view);

    if (strcmp (str1, str2) != 0) {
        printf ("Strings differ\n");
        printf ("original:\n%s\nparsed:\n%s\n", str1, str2);
        return false;
    } else {
        printf ("Strings are the same!\n");
        return true;
    }
}

