/*
 * Copiright (C) 2018 Santiago León O.
 */

// This component of the keyboard view manages string representations of
// keyboard views. We need this becaúse a keyboard representation can be defined
// programatically, be stored in the application's gresources, or be stored in
// the user's configuration folder (either as an autosave or a custom
// representation). The code in this file loads representations from all these
// sources and exposes them as a linked list of string representations.
//
// This is also where we create all programatic representations so there are
// several examples of how the keyboard_view_builder.c module is used.

struct kv_repr_state_t {
    char *repr;

    struct kv_repr_state_t *next;
};

struct kv_repr_t {
    bool is_internal;
    char *name;
    struct kv_repr_state_t *states;
    struct kv_repr_state_t *last_state;

    struct kv_repr_t *next;
};

struct kv_repr_store_t {
    mem_pool_t pool;
    struct kv_repr_t *reprs;
    struct kv_repr_t *last_repr;
    struct kv_repr_t *curr_repr;
};

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

BUILD_GEOMETRY_FUNC(multirow_test)
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

BUILD_GEOMETRY_FUNC(edge_resize_test_1)
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

BUILD_GEOMETRY_FUNC(edge_resize_test_2)
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

BUILD_GEOMETRY_FUNC(edge_resize_test_3)
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

BUILD_GEOMETRY_FUNC(adjust_left_edge_test)
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

BUILD_GEOMETRY_FUNC(vertical_extend_test_1)
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

BUILD_GEOMETRY_FUNC(vertical_extend_test_2)
{
    struct geometry_edit_ctx_t ctx;
    kv_geometry_ctx_init_append (kv, &ctx);

    kv_new_row_h (&ctx, 1);
    struct sgmt_t *m = kv_add_key_full (&ctx, KEY_1, 1, 0);

    kv_new_row_h (&ctx, 1);
    kv_add_multirow_sized_sgmt (&ctx, m, 0.5, MULTIROW_ALIGN_LEFT);
    kv_add_key_full (&ctx, KEY_2, 1, 2);

    kv_end_geometry (&ctx);
}

BUILD_GEOMETRY_FUNC(vertical_extend_test_3)
{
    struct geometry_edit_ctx_t ctx;
    kv_geometry_ctx_init_append (kv, &ctx);

    kv_new_row_h (&ctx, 1);
    kv_add_key_full (&ctx, KEY_1, 1, 0);
    kv_add_key_full (&ctx, KEY_2, 1, 1.5);

    kv_new_row_h (&ctx, 1);
    kv_add_key (&ctx, KEY_3);
    kv_add_key (&ctx, KEY_4);

    kv_end_geometry (&ctx);
}

void kv_repr_store_destroy (struct kv_repr_store_t *store)
{
    mem_pool_destroy (&store->pool);
}

// NOTE: Only call this is _str_ is already allocated into _store_->pool
void kv_repr_push_state_no_dup (struct kv_repr_store_t *store, struct kv_repr_t *repr, char *str)
{
    struct kv_repr_state_t *state =
        mem_pool_push_size (&store->pool, sizeof(struct kv_repr_state_t));
    *state = ZERO_INIT(struct kv_repr_state_t);
    state->repr = str;

    if (repr->last_state != NULL) {
        repr->last_state->next = state;
    } else {
        repr->states = state;
    }
    repr->last_state = state;
}

void kv_repr_push_state (struct kv_repr_store_t *store, struct kv_repr_t *repr, const char *str)
{
    char *dup_str = pom_strdup (&store->pool, str);
    kv_repr_push_state_no_dup (store, repr, dup_str);
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
    kv_repr_push_state_no_dup (store, new_repr, str);
    keyboard_view_destroy (kv);

    if (store->last_repr != NULL) {
        store->last_repr->next = new_repr;
    } else {
        store->reprs = new_repr;
    }
    store->last_repr = new_repr;
}

void kv_push_representation_str (struct kv_repr_store_t *store,
                                 const char *name, const char *str, bool is_internal)
{
    struct kv_repr_t *new_repr = mem_pool_push_size (&store->pool, sizeof(struct kv_repr_t));
    *new_repr = ZERO_INIT (struct kv_repr_t);
    new_repr->is_internal = is_internal;

    // TODO: Check that parsing of _repr_ will succeed.
    kv_repr_push_state (store, new_repr, str);
    new_repr->name = pom_strdup (&store->pool, name);

    if (store->last_repr != NULL) {
        store->last_repr->next = new_repr;
    } else {
        store->reprs = new_repr;
    }
    store->last_repr = new_repr;
}

// NOTE: path is expected to be absolute.
void kv_repr_store_push_file (struct kv_repr_store_t *store, char *path)
{
    mem_pool_t pool_l = {0};
    char *fname;
    path_split (&pool_l, path, NULL, &fname);

    if (!g_str_has_suffix(fname, ".autosave.lrep") && g_str_has_suffix(fname, ".lrep")) {
        char *name = remove_extension (&pool_l, fname);
        char *str = full_file_read (&pool_l, path, NULL);
        kv_push_representation_str (store, name, str, false);

    } else {
        // The push failed, restore pool as it whas when we started.
        printf ("File representation load failed: %s\n", path);
    }

    mem_pool_destroy (&pool_l);
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

#define kv_repr_store_push_func_simple(store,func_name) \
    kv_repr_store_push_func(store, #func_name, func_name);

struct kv_repr_store_t* kv_repr_store_new (char *repr_path)
{
    struct kv_repr_store_t *store;
    {
        mem_pool_t bootstrap = {0};
        store = mem_pool_push_size (&bootstrap, sizeof(struct kv_repr_store_t));
        *store = ZERO_INIT (struct kv_repr_store_t);
        store->pool = bootstrap;
    }

    kv_repr_store_push_func (store, "Simple", kv_build_default_geometry);

    // Load internal representations from GResource
    {
        GResource *gresource = gresource_get_resource ();
        string_t path = str_new ("/com/github/santileortiz/iconoscope/data/repr/");
        size_t path_len = str_len (&path);
        GError *error = NULL;
        char **fnames = g_resource_enumerate_children (gresource,
                                                       str_data(&path),
                                                       G_RESOURCE_LOOKUP_FLAGS_NONE,
                                                       &error);
        while (*fnames) {
            if (!g_str_has_suffix(*fnames, ".autosave.lrep") && g_str_has_suffix(*fnames, ".lrep")) {

                str_put_c (&path, path_len, *fnames);
                GBytes *bytes = g_resources_lookup_data (str_data(&path), G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
                gsize bytes_size;
                const char *str = g_bytes_get_data (bytes, &bytes_size);
                char *name = remove_extension (NULL, *fnames);

                kv_push_representation_str (store, name, str, true);
                free (name);
            }
            fnames++;
        }

        str_free (&path);
    }

#ifndef NDEBUG
    // Push debug geometries
    kv_repr_store_push_func_simple (store, multirow_test);
    kv_repr_store_push_func_simple (store, edge_resize_leave_original_pos_1);
    kv_repr_store_push_func_simple (store, edge_resize_leave_original_pos_2);
    kv_repr_store_push_func_simple (store, edge_resize_test_1);
    kv_repr_store_push_func_simple (store, edge_resize_test_2);
    kv_repr_store_push_func_simple (store, edge_resize_test_3);
    kv_repr_store_push_func_simple (store, adjust_left_edge_test);
    kv_repr_store_push_func_simple (store, vertical_extend_test_1);
    kv_repr_store_push_func_simple (store, vertical_extend_test_2);
    kv_repr_store_push_func_simple (store, vertical_extend_test_3);
#endif

    if (repr_path != NULL) {
        string_t repr_path_str = str_new (repr_path);
        size_t repr_path_len = str_len (&repr_path_str);

        // Load all saved representations (ignore autosave files)
        {
            DIR *d = opendir (str_data(&repr_path_str));
            if (d != NULL) {
                struct dirent *entry_info;
                while (read_dir (d, &entry_info)) {
                    if (entry_info->d_name[0] != '.' &&
                        !g_str_has_suffix (entry_info->d_name, ".autosave.lrep") &&
                        g_str_has_suffix (entry_info->d_name, ".lrep")) {

                        str_put_c (&repr_path_str, repr_path_len, entry_info->d_name);
                        kv_repr_store_push_file (store, str_data(&repr_path_str));
                    }
                }

                closedir (d);

            } else {
                printf ("Error opening %s: %s\n", str_data(&repr_path_str), strerror(errno));
            }
        }

        // Push autosaves as a state into the corresponding representation
        // FIXME: This is O(n^2) @performance
        {
            str_data(&repr_path_str)[repr_path_len] = '\0';

            DIR *d = opendir (str_data(&repr_path_str));
            if (d != NULL) {
                struct dirent *entry_info;
                while (read_dir (d, &entry_info)) {
                    if (entry_info->d_name[0] != '.' &&
                        g_str_has_suffix (entry_info->d_name, ".autosave.lrep")) {

                        char *name = remove_multiple_extensions (NULL, entry_info->d_name, 2);
                        struct kv_repr_t *repr = kv_repr_get_by_name (store, name);

                        if (repr != NULL) {
                            str_put_c (&repr_path_str, repr_path_len, entry_info->d_name);
                            char *str = full_file_read (&store->pool, str_data(&repr_path_str), NULL);
                            kv_repr_push_state_no_dup (store, repr, str);

                        } else {
                            // TODO: Should we remove this dangling autosave?
                            printf ("Autosave for non existent representation \"%s\".\n", name);
                        }

                        free (name);
                    }
                }

                closedir (d);

            } else {
                printf ("Error opening %s: %s\n", str_data(&repr_path_str), strerror(errno));
            }
        }

        str_free (&repr_path_str);
    }

    store->curr_repr = store->reprs;

    return store;
}

