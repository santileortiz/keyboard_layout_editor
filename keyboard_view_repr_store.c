/*
 * Copiright (C) 2018 Santiago León O.
 */

#define BUILD_GEOMETRY_FUNC(name) \
    void name(struct keyboard_view_t *kv)
typedef BUILD_GEOMETRY_FUNC(set_geometry_func_t);

// Simple default keyboard geometry.
// NOTE: Keycodes are used as defined in the linux kernel. To translate them
// into X11 keycodes offset them by 8 (x11_kc = kc+8).
BUILD_GEOMETRY_FUNC(kv_build_default_geometry)
{
    struct geometry_edit_ctx_t ctx;
    kv_geometry_ctx_init_append (kv, &ctx);

    kv_new_row (&ctx);
    kv_add_key (&ctx, KEY_ESC);
    kv_add_key (&ctx, KEY_F1);
    kv_add_key (&ctx, KEY_F2);
    kv_add_key (&ctx, KEY_F3);
    kv_add_key (&ctx, KEY_F4);
    kv_add_key (&ctx, KEY_F5);
    kv_add_key (&ctx, KEY_F6);
    kv_add_key (&ctx, KEY_F7);
    kv_add_key (&ctx, KEY_F8);
    kv_add_key (&ctx, KEY_F9);
    kv_add_key (&ctx, KEY_F10);
    kv_add_key (&ctx, KEY_F11);
    kv_add_key (&ctx, KEY_F12);
    kv_add_key (&ctx, KEY_NUMLOCK);
    kv_add_key (&ctx, KEY_SCROLLLOCK);
    kv_add_key (&ctx, KEY_INSERT);

    kv_new_row (&ctx);
    kv_add_key (&ctx, KEY_GRAVE);
    kv_add_key (&ctx, KEY_1);
    kv_add_key (&ctx, KEY_2);
    kv_add_key (&ctx, KEY_3);
    kv_add_key (&ctx, KEY_4);
    kv_add_key (&ctx, KEY_5);
    kv_add_key (&ctx, KEY_6);
    kv_add_key (&ctx, KEY_7);
    kv_add_key (&ctx, KEY_8);
    kv_add_key (&ctx, KEY_9);
    kv_add_key (&ctx, KEY_0);
    kv_add_key (&ctx, KEY_MINUS);
    kv_add_key (&ctx, KEY_EQUAL);
    kv_add_key_w (&ctx, KEY_BACKSPACE, 2);
    kv_add_key (&ctx, KEY_HOME);

    kv_new_row (&ctx);
    kv_add_key_w (&ctx, KEY_TAB, 1.5);
    kv_add_key (&ctx, KEY_Q);
    kv_add_key (&ctx, KEY_W);
    kv_add_key (&ctx, KEY_E);
    kv_add_key (&ctx, KEY_R);
    kv_add_key (&ctx, KEY_T);
    kv_add_key (&ctx, KEY_Y);
    kv_add_key (&ctx, KEY_U);
    kv_add_key (&ctx, KEY_I);
    kv_add_key (&ctx, KEY_O);
    kv_add_key (&ctx, KEY_P);
    kv_add_key (&ctx, KEY_LEFTBRACE);
    kv_add_key (&ctx, KEY_RIGHTBRACE);
    kv_add_key_w (&ctx, KEY_BACKSLASH, 1.5);
    kv_add_key (&ctx, KEY_PAGEUP);

    kv_new_row (&ctx);
    kv_add_key_w (&ctx, KEY_CAPSLOCK, 1.75);
    kv_add_key (&ctx, KEY_A);
    kv_add_key (&ctx, KEY_S);
    kv_add_key (&ctx, KEY_D);
    kv_add_key (&ctx, KEY_F);
    kv_add_key (&ctx, KEY_G);
    kv_add_key (&ctx, KEY_H);
    kv_add_key (&ctx, KEY_J);
    kv_add_key (&ctx, KEY_K);
    kv_add_key (&ctx, KEY_L);
    kv_add_key (&ctx, KEY_SEMICOLON);
    kv_add_key (&ctx, KEY_APOSTROPHE);
    kv_add_key_w (&ctx, KEY_ENTER, 2.25);
    kv_add_key (&ctx, KEY_PAGEDOWN);

    kv_new_row (&ctx);
    kv_add_key_w (&ctx, KEY_LEFTSHIFT, 2.25);
    kv_add_key (&ctx, KEY_Z);
    kv_add_key (&ctx, KEY_X);
    kv_add_key (&ctx, KEY_C);
    kv_add_key (&ctx, KEY_V);
    kv_add_key (&ctx, KEY_B);
    kv_add_key (&ctx, KEY_N);
    kv_add_key (&ctx, KEY_M);
    kv_add_key (&ctx, KEY_COMMA);
    kv_add_key (&ctx, KEY_DOT);
    kv_add_key (&ctx, KEY_SLASH);
    kv_add_key_w (&ctx, KEY_RIGHTSHIFT, 1.75);
    kv_add_key (&ctx, KEY_UP);
    kv_add_key (&ctx, KEY_END);

    kv_new_row (&ctx);
    kv_add_key_w (&ctx, KEY_LEFTCTRL, 1.5);
    kv_add_key_w (&ctx, KEY_LEFTMETA, 1.5);
    kv_add_key_w (&ctx, KEY_LEFTALT, 1.5);
    kv_add_key_w (&ctx, KEY_SPACE, 5.5);
    kv_add_key_w (&ctx, KEY_RIGHTALT, 1.5);
    kv_add_key_w (&ctx, KEY_RIGHTCTRL, 1.5);
    kv_add_key (&ctx, KEY_LEFT);
    kv_add_key (&ctx, KEY_DOWN);
    kv_add_key (&ctx, KEY_RIGHT);

    kv_end_geometry (&ctx);
}

BUILD_GEOMETRY_FUNC(multirow_test_geometry)
{
    struct geometry_edit_ctx_t ctx;
    kv_geometry_ctx_init_append (kv, &ctx);

    kv_new_row_h (&ctx, 1.5);
    struct sgmt_t *multi1 = kv_add_key (&ctx, KEY_A);
    kv_add_key (&ctx, KEY_1);
    //kv_add_key (&ctx, KEY_2);
    struct sgmt_t *multi4 = kv_add_key_w (&ctx, KEY_D, 2);

    kv_new_row_h (&ctx, 1.25);
    struct sgmt_t *multi2 = kv_add_key (&ctx, KEY_B);
    kv_add_multirow_sgmt (&ctx, multi1);
    kv_add_key_full (&ctx, KEY_3, 1, 1);
    kv_add_multirow_sized_sgmt (&ctx, multi4, 1, MULTIROW_ALIGN_LEFT);

    kv_new_row_h (&ctx, 1);
    kv_add_key (&ctx, KEY_4);
    kv_add_multirow_sgmt (&ctx, multi2);
    struct sgmt_t *multi3 = kv_add_key (&ctx, KEY_C);
    kv_add_multirow_sgmt (&ctx, multi4);

    kv_new_row_h (&ctx, 0.75);
    kv_add_key (&ctx, KEY_5);
    kv_add_key (&ctx, KEY_6);
    kv_add_multirow_sgmt (&ctx, multi3);
    kv_add_multirow_sized_sgmt (&ctx, multi4, 3, MULTIROW_ALIGN_RIGHT);

    kv_end_geometry (&ctx);
}

BUILD_GEOMETRY_FUNC(edge_resize_leave_original_pos_1)
{
    struct geometry_edit_ctx_t ctx;
    kv_geometry_ctx_init_append (kv, &ctx);

    kv_new_row_h (&ctx, 1);
    struct sgmt_t *m = kv_add_key_w (&ctx, KEY_A, 3);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sized_sgmt (&ctx, m, 2, MULTIROW_ALIGN_LEFT);
    kv_add_key (&ctx, KEY_1);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sized_sgmt (&ctx, m, 3, MULTIROW_ALIGN_RIGHT);
    kv_add_key_full (&ctx, KEY_2, 1, 1);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sized_sgmt (&ctx, m, 4, MULTIROW_ALIGN_RIGHT);
    kv_add_key_full (&ctx, KEY_3, 1, 2);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sized_sgmt (&ctx, m, 3, MULTIROW_ALIGN_LEFT);

    kv_end_geometry (&ctx);
}

BUILD_GEOMETRY_FUNC(edge_resize_leave_original_pos_2)
{
    struct geometry_edit_ctx_t ctx;
    kv_geometry_ctx_init_append (kv, &ctx);

    kv_new_row_h (&ctx, 1);
    struct sgmt_t *m1 = kv_add_key (&ctx, KEY_1);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, m1);
    kv_add_key_full (&ctx, KEY_A, 1, 1);
    kv_add_key_full (&ctx, KEY_B, 1, 1);
    kv_add_key_full (&ctx, KEY_C, 1, 1);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, m1);

    kv_end_geometry (&ctx);
}

BUILD_GEOMETRY_FUNC(edge_resize_test_geometry_1)
{
    struct geometry_edit_ctx_t ctx;
    kv_geometry_ctx_init_append (kv, &ctx);

    kv_new_row_h (&ctx, 1);
    struct sgmt_t *l = kv_add_key_full (&ctx, KEY_L, 1, 0);
    struct sgmt_t *m1 = kv_add_key_full (&ctx, KEY_1, 1, 1);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, l);
    struct sgmt_t *m2 = kv_add_key_full (&ctx, KEY_2, 1, 2.5);
    kv_add_multirow_sgmt (&ctx, m1);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, l);
    kv_add_multirow_sgmt (&ctx, m2);
    kv_add_multirow_sgmt (&ctx, m1);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, l);
    kv_add_multirow_sized_sgmt (&ctx, m1, 4, MULTIROW_ALIGN_RIGHT);

    kv_end_geometry (&ctx);
}

BUILD_GEOMETRY_FUNC(edge_resize_test_geometry_2)
{
    struct geometry_edit_ctx_t ctx;
    kv_geometry_ctx_init_append (kv, &ctx);

    kv_new_row_h (&ctx, 1);
    struct sgmt_t *m1 = kv_add_key_full (&ctx, KEY_1, 1, 1);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, m1);
    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, m1);

    kv_new_row_h (&ctx, 1);
    struct sgmt_t *m2 = kv_add_key_full (&ctx, KEY_2, 1, 0);
    kv_add_multirow_sgmt (&ctx, m1);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, m2);
    kv_add_multirow_sgmt (&ctx, m1);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, m2);
    kv_add_multirow_sgmt (&ctx, m1);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, m1);
    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, m1);
    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, m1);

    kv_end_geometry (&ctx);
}

BUILD_GEOMETRY_FUNC(edge_resize_test_geometry_3)
{
    struct geometry_edit_ctx_t ctx;
    kv_geometry_ctx_init_append (kv, &ctx);

    kv_new_row_h (&ctx, 1);
    struct sgmt_t *e = kv_add_key (&ctx, KEY_1);
    struct sgmt_t *k1 = kv_add_key_full (&ctx, KEY_A, 3, 2);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sized_sgmt (&ctx, e, 3, MULTIROW_ALIGN_LEFT);
    kv_add_multirow_sized_sgmt (&ctx, k1, 1, MULTIROW_ALIGN_RIGHT);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, e);
    kv_add_multirow_sized_sgmt (&ctx, k1, 2, MULTIROW_ALIGN_RIGHT);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, e);
    kv_add_multirow_sgmt (&ctx, k1);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, e);
    struct sgmt_t *k2 = kv_add_key_full (&ctx, KEY_B, 1, 2);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, e);
    kv_add_multirow_sized_sgmt (&ctx, k2, 2, MULTIROW_ALIGN_RIGHT);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, e);
    kv_add_multirow_sgmt (&ctx, k2);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sized_sgmt (&ctx, e, 1, MULTIROW_ALIGN_LEFT);
    kv_add_multirow_sized_sgmt (&ctx, k2, 3, MULTIROW_ALIGN_RIGHT);

    kv_end_geometry (&ctx);
}

BUILD_GEOMETRY_FUNC(adjust_left_edge_test_geometry)
{
    struct geometry_edit_ctx_t ctx;
    kv_geometry_ctx_init_append (kv, &ctx);

    kv_new_row_h (&ctx, 1);
    struct sgmt_t *m1 = kv_add_key_full (&ctx, KEY_1, 1, 0);

    kv_new_row_h (&ctx, 1);
    struct sgmt_t *m2 = kv_add_key_full (&ctx, KEY_2, 1, 1);
    kv_add_multirow_sgmt (&ctx, m1);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sgmt (&ctx, m2);
    kv_add_multirow_sgmt (&ctx, m1);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sized_sgmt (&ctx, m1, 4, MULTIROW_ALIGN_RIGHT);

    kv_end_geometry (&ctx);
}

BUILD_GEOMETRY_FUNC(vertical_extend_test_geometry)
{
    struct geometry_edit_ctx_t ctx;
    kv_geometry_ctx_init_append (kv, &ctx);

    kv_new_row_h (&ctx, 1);
    struct sgmt_t *m = kv_add_key_full (&ctx, KEY_1, 1, 0);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sized_sgmt (&ctx, m, 0.5, MULTIROW_ALIGN_LEFT);
    kv_add_key_full (&ctx, KEY_2, 1, 1);

    kv_end_geometry (&ctx);
}

void kv_repr_store_destroy (struct kv_repr_store_t *store)
{
    mem_pool_destroy (&store->pool);
}

void kv_repr_push_state (struct kv_repr_store_t *store, struct kv_repr_t *repr, char *str)
{
    struct kv_repr_state_t *state =
        mem_pool_push_size (&store->pool, sizeof(struct kv_repr_state_t*));
    *state = ZERO_INIT(struct kv_repr_state_t);
    state->repr = str;

    if (repr->last_state != NULL) {
        repr->last_state->next = state;
    } else {
        repr->states = state;
    }
    repr->last_state = state;
}

void kv_repr_store_push_func (struct kv_repr_store_t *store, char *name, set_geometry_func_t *func)
{
    struct kv_repr_t *new_repr = mem_pool_push_size (&store->pool, sizeof(struct kv_repr_t));
    *new_repr = ZERO_INIT (struct kv_repr_t);

    new_repr->is_internal = true;
    new_repr->name = pom_strdup (&store->pool, name);

    struct keyboard_view_t *kv = kv_new ();

    func (kv);
    char *str = kv_to_string (&store->pool, kv);
    kv_repr_push_state (store, new_repr, str);
    keyboard_view_destroy (kv);

    if (store->last_repr != NULL) {
        store->last_repr->next = new_repr;
    } else {
        store->reprs = new_repr;
    }
    store->last_repr = new_repr;
}

void kv_repr_store_push_file (struct kv_repr_store_t *store, char *path)
{
    bool invalid_file = false;

    mem_pool_temp_marker_t mrkr = mem_pool_begin_temporary_memory (&store->pool);

    struct kv_repr_t *new_repr = mem_pool_push_size (&store->pool, sizeof(struct kv_repr_t));
    *new_repr = ZERO_INIT (struct kv_repr_t);
    new_repr->is_internal = false;

    char *fname;
    path_split (NULL, path, NULL, &fname);
    if (g_str_has_suffix(fname, ".autosave.lrep") || !g_str_has_suffix(fname, ".lrep")) {
        // Invalid file extension don't push anything.
        invalid_file = true;
    } else {
        new_repr->name = remove_extension (&store->pool, fname);
    }

    free (fname);

    if (!invalid_file) {
        // TODO: Check that parsing of this file will actually succeed and if
        // not set invalid_file = true.
        char *str = full_file_read (&store->pool, path);
        kv_repr_push_state (store, new_repr, str);

        if (store->last_repr != NULL) {
            store->last_repr->next = new_repr;
        } else {
            store->reprs = new_repr;
        }
        store->last_repr = new_repr;

    } else {
        // The push failed, restore pool as it whas when we started.
        printf ("File representation load failed: %s\n", path);
        mem_pool_end_temporary_memory (mrkr);
    }
}

#define kv_repr_store_push_func_simple(store,func_name) \
    kv_repr_store_push_func(store, #func_name, func_name);

struct kv_repr_store_t* kv_repr_store_new ()
{
    struct kv_repr_store_t *store;
    {
        mem_pool_t bootstrap = {0};
        store = mem_pool_push_size (&bootstrap, sizeof(struct kv_repr_store_t));
        *store = ZERO_INIT (struct kv_repr_store_t);
        store->pool = bootstrap;
    }

    kv_repr_store_push_func (store, "Simple", kv_build_default_geometry);
    // TODO: Get Full.lrep file from gresource
    //kv_push_representation_str (kv, "Full", full_str);

#ifndef NDEBUG
    // Push debug geometries
    kv_repr_store_push_func_simple (store, multirow_test_geometry);
    kv_repr_store_push_func_simple (store, edge_resize_leave_original_pos_1);
    kv_repr_store_push_func_simple (store, edge_resize_leave_original_pos_2);
    kv_repr_store_push_func_simple (store, edge_resize_test_geometry_1);
    kv_repr_store_push_func_simple (store, edge_resize_test_geometry_2);
    kv_repr_store_push_func_simple (store, edge_resize_test_geometry_3);
    kv_repr_store_push_func_simple (store, adjust_left_edge_test_geometry);
    kv_repr_store_push_func_simple (store, vertical_extend_test_geometry);
#endif

    string_t repr_path = app_get_repr_path (&app);
    size_t repr_path_len = str_len (&repr_path);

    // Load all saved representations (ignore autosave files)
    {
        DIR *d = opendir (str_data(&repr_path));
        if (d != NULL) {
            struct dirent *entry_info;
            while (read_dir (d, &entry_info)) {
                if (entry_info->d_name[0] != '.' &&
                    !g_str_has_suffix (entry_info->d_name, ".autosave.lrep") &&
                    g_str_has_suffix (entry_info->d_name, ".lrep")) {

                    str_put_c (&repr_path, repr_path_len, entry_info->d_name);
                    kv_repr_store_push_file (store, str_data(&repr_path));
                }
            }

            closedir (d);

        } else {
            printf ("Error opening %s: %s\n", str_data(&repr_path), strerror(errno));
        }
    }

    // Push autosaves as a state into the corresponding representation
    // FIXME: This is O(n^2) @performance
    {
        str_data(&repr_path)[repr_path_len] = '\0';

        DIR *d = opendir (str_data(&repr_path));
        if (d != NULL) {
            struct dirent *entry_info;
            while (read_dir (d, &entry_info)) {
                if (entry_info->d_name[0] != '.' &&
                    g_str_has_suffix (entry_info->d_name, ".autosave.lrep")) {

                    char *name = remove_multiple_extensions (NULL, entry_info->d_name, 2);
                    struct kv_repr_t *repr = kv_repr_get_by_name (store, name);

                    if (repr != NULL) {
                        str_put_c (&repr_path, repr_path_len, entry_info->d_name);
                        char *str = full_file_read (&store->pool, str_data(&repr_path));
                        kv_repr_push_state (store, repr, str);

                    } else {
                        // TODO: Should we remove this dangling autosave?
                        printf ("Autosave for non existant representation \"%s\".\n", name);
                    }

                    free (name);
                }
            }

            closedir (d);

        } else {
            printf ("Error opening %s: %s\n", str_data(&repr_path), strerror(errno));
        }
    }

    str_free (&repr_path);

    store->curr_repr = store->reprs;

    return store;
}

struct kv_repr_t* kv_repr_get_by_name (struct kv_repr_store_t *store, const char *name)
{
    struct kv_repr_t *curr_repr = store->reprs;
    while (curr_repr != NULL) {
        if (strcmp (name, curr_repr->name) == 0) {
            break;
        }
        curr_repr = curr_repr->next;
    }
    return curr_repr;
}

