/*
 * Copiright (C) 2018 Santiago León O.
 */

void kv_repr_store_destroy (struct kv_repr_store_t *store)
{
    mem_pool_destroy (&store->pool);
}

void kv_repr_store_push_func (struct kv_repr_store_t *store, char *name, set_geometry_func_t *func)
{
    struct kv_repr_t *new_repr = mem_pool_push_size (&store->pool, sizeof(struct kv_repr_t));
    *new_repr = ZERO_INIT (struct kv_repr_t);

    new_repr->is_internal = true;
    new_repr->saved = true;
    new_repr->name = pom_strdup (&store->pool, name);

    struct keyboard_view_t *kv = kv_new ();

    func (kv);
    new_repr->repr = kv_to_string (&store->pool, kv);
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

    struct kv_repr_t res = ZERO_INIT (struct kv_repr_t);
    res.is_internal = false;

    char *fname;
    path_split (NULL, path, NULL, &fname);
    if (g_str_has_suffix(fname, ".autosave.lrep")) {
        res.saved = false;
        res.name = remove_multiple_extensions (&store->pool, fname, 2);

    } else if (g_str_has_suffix(fname, ".lrep")) {
        res.saved = true;
        res.name = remove_extension (&store->pool, fname);

    } else {
        // Invalid file extension don't push anything.
        invalid_file = true;
    }
    free (fname);

    if (!invalid_file) {
        // TODO: Check that parsing of this file will actually succeed and if
        // not set invalid_file = true.
        res.repr = full_file_read (&store->pool, path);

        struct kv_repr_t *new_repr = mem_pool_push_size (&store->pool, sizeof(struct kv_repr_t));
        *new_repr = res;

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

    DIR *d = opendir (str_data(&repr_path));
    if (d == NULL) {
        printf ("Error opening %s: %s\n", str_data(&repr_path), strerror(errno));
    }

    struct dirent *entry_info;
    while (read_dir (d, &entry_info)) {
        if (entry_info->d_name[0] != '.') {
            str_put_c (&repr_path, repr_path_len, entry_info->d_name);
            kv_repr_store_push_file (store, str_data(&repr_path));
        }
    }

    store->curr_repr = store->reprs;

    closedir (d);
    str_free (&repr_path);

    return store;
}

struct kv_repr_t* kv_repr_get_by_name (struct kv_repr_store_t *store, const char *name, bool saved)
{
    struct kv_repr_t *curr_repr = store->reprs;
    while (curr_repr != NULL) {
        if (strcmp (name, curr_repr->name) == 0 && curr_repr->saved == saved) {
            break;
        }
        curr_repr = curr_repr->next;
    }
    return curr_repr;

}

