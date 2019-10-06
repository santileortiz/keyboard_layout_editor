/*
 * Copiright (C) 2018 Santiago León O.
 */

char* kv_to_string_full (mem_pool_t *pool, struct keyboard_view_t *kv, bool full)
{
    // Set posix locale so the decimal separator is always .
    char *old_locale = begin_posix_locale ();

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

                if (full) {
                    switch (sgmt->type) {
                        case KEY_DEFAULT:
                            break;

                        case KEY_PRESSED:
                            str_cat_c (&str, ", P");
                            break;

                        case KEY_MULTIROW_SEGMENT:
                            str_cat_c (&str, ", MSEG");
                            break;

                        case KEY_MULTIROW_SEGMENT_SIZED:
                            str_cat_c (&str, ", MSIZ");
                            break;

                        default:
                            str_cat_c (&str, ", ?");
                            break;
                    }
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

                if (full) {
                    if (sgmt->internal_glue != 0) {
                        str_cat_c (&str, ", IG: ");
                        snprintf (buff, ARRAY_SIZE(buff), "%g", sgmt->internal_glue);
                        str_cat_c (&str, buff);
                    }

                    switch (sgmt->type) {
                        case KEY_DEFAULT:
                            break;

                        case KEY_PRESSED:
                            str_cat_c (&str, ", P");
                            break;

                        case KEY_MULTIROW_SEGMENT:
                            str_cat_c (&str, ", MSEG");
                            break;

                        case KEY_MULTIROW_SEGMENT_SIZED:
                            str_cat_c (&str, ", MSIZ");
                            break;

                        default:
                            str_cat_c (&str, ", ?");
                            break;
                    }
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

                if (full && sgmt->internal_glue != 0) {
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
    end_posix_locale (old_locale);

    return pom_strdup (pool, str_data(&str));
}

void kv_print (struct keyboard_view_t *kv)
{
    mem_pool_t pool = ZERO_INIT (mem_pool_t);
    printf ("%s\n", kv_to_string_debug (&pool, kv));
    mem_pool_destroy (&pool);
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

    scanner_consume_spaces (scnr);
    if (scanner_str (scnr, ", W:")) {
        scanner_consume_spaces (scnr);
        if (!scanner_float (scnr, width)) {
            scanner_set_error (scnr, "Expected width.\n");
        }
    }

    scanner_consume_spaces (scnr);
    if (scanner_str (scnr, ", UG:")) {
        scanner_consume_spaces (scnr);
        if (!scanner_float (scnr, user_glue)) {
            scanner_set_error (scnr, "Expected user glue.\n");
        }
    }

    scanner_consume_spaces (scnr);
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

    scanner_consume_spaces (scnr);
    if (scanner_str (scnr, "W:")) {
        scanner_consume_spaces (scnr);
        if (!scanner_float (scnr, width)) {
            scanner_set_error (scnr, "Expected width.\n");
        }

        scanner_consume_spaces (scnr);
        if (scanner_char (scnr, ',')) {
            scanner_consume_spaces (scnr);
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

    }

    scanner_consume_spaces (scnr);
    if (!scanner_char (scnr, ')')) {
        scanner_set_error (scnr, "Missing ')'\n");
    }
}

// NOTE: This only parses strings created by calling kv_to_string().
// @keyboard_string_formats
void kv_set_from_string (struct keyboard_view_t *kv, char *str)
{
    kv_clear (kv);
    struct geometry_edit_ctx_t ctx;
    kv_geometry_ctx_init_append (kv, &ctx);

    // Set posix locale so the decimal separator is always .
    char *old_locale = begin_posix_locale ();

    struct scanner_t scnr = ZERO_INIT(struct scanner_t);
    scnr.pos = str;

    GList *multirow_list=NULL, *curr_multirrow = multirow_list;

    while (!scnr.is_eof && !scnr.error) {
        float row_height = 1;
        scanner_consume_spaces (&scnr);
        scanner_float (&scnr, &row_height);

        kv_new_row_h (&ctx, row_height);

        while (!scnr.is_eof) {
            scanner_consume_spaces (&scnr);
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
    end_posix_locale (old_locale);
}

bool kv_test_parser (struct keyboard_view_t *kv)
{
    mem_pool_t pool = ZERO_INIT(mem_pool_t);
    char *str1 = kv_to_string (&pool, kv);
    kv_set_from_string (kv, str1);
    char *str2 = kv_to_string (&pool, kv);

    if (strcmp (str1, str2) != 0) {
        printf ("Strings differ\n");
        printf ("original:\n%s\nparsed:\n%s\n", str1, str2);
        return false;
    } else {
        printf ("Strings are the same!\n");
        return true;
    }
}

