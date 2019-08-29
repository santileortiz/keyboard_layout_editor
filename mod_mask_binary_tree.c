/*
 * Copiright (C) 2019 Santiago León O.
 */

// TODO: Think about moving this into common.h

// This will start as a tree API to serve the needs of the keyboard layout
// editor. Over time I expect it to become a more robust tree API.
//
// This means for now keys will only be strings, which I think is the common
// case anyway.

// For now it will be a tree that looks like the one in GLib but works well with
// memory pools and allows us to have a clean valgrind output. Also, Glib forces
// us to allocate stuff as pointers, here we will store values inside the nodes,
// or have no value at all.
struct mod_mask_binary_tree_t {
    mem_pool_t pool;

    uint32_t num_nodes;

    struct mod_mask_binary_tree_node_t *root;
};

// Leftmost node will be the smallest.
struct mod_mask_binary_tree_node_t {
    char *key;

    key_modifier_mask_t value;

    struct mod_mask_binary_tree_node_t *right;
    struct mod_mask_binary_tree_node_t *left;
};

void mod_mask_binary_tree_destroy (struct mod_mask_binary_tree_t *tree)
{
    mem_pool_destroy (&tree->pool);
}

struct mod_mask_binary_tree_node_t* mod_mask_binary_tree_allocate_node (struct mod_mask_binary_tree_t *tree)
{
    // TODO: When we add removal of nodes, this should allocate them from a free
    // list of nodes.
    struct mod_mask_binary_tree_node_t *new_node =
        mem_pool_push_struct (&tree->pool, struct mod_mask_binary_tree_node_t);
    *new_node = ZERO_INIT(struct mod_mask_binary_tree_node_t);
    return new_node;
}

void mod_mask_binary_tree_insert (struct mod_mask_binary_tree_t *tree, char *key, key_modifier_mask_t value)
{
    if (tree->root == NULL) {
        struct mod_mask_binary_tree_node_t *new_node = mod_mask_binary_tree_allocate_node (tree);
        new_node->key = key;
        new_node->value = value;

        tree->root = new_node;
        tree->num_nodes++;

    } else {
        bool key_found = false;
        struct mod_mask_binary_tree_node_t **curr_node = &tree->root;
        while (*curr_node != NULL) {
            int c = strcasecmp (key, (*curr_node)->key);
            if (c < 0) {
                curr_node = &(*curr_node)->left;

            } else if (c > 0) {
                curr_node = &(*curr_node)->right;

            } else {
                // Key already exists. Options of what we could do here:
                //
                //  - Assert that this will never happen.
                //  - Overwrite the existing value with the new one. The problem
                //    is if values are pointers in the future, then we could be
                //    leaking stuff without knowing?
                //  - Do nothing, but somehow let the caller know the key was
                //    already there so we didn't insert the value they wanted.
                //
                // I lean more towards the last option.
                key_found = true;
                break;
            }
        }

        if (!key_found) {
            *curr_node = mod_mask_binary_tree_allocate_node (tree);
            (*curr_node)->key = key;
            (*curr_node)->value = value;

            tree->num_nodes++;
        }

        // TODO: Rebalance the tree.
    }
}

bool mod_mask_binary_tree_lookup (struct mod_mask_binary_tree_t *tree, char *key, struct mod_mask_binary_tree_node_t **result)
{
    bool key_found = false;
    struct mod_mask_binary_tree_node_t **curr_node = &tree->root;
    while (*curr_node != NULL) {
        int c = strcasecmp (key, (*curr_node)->key);
        if (c < 0) {
            curr_node = &(*curr_node)->left;

        } else if (c > 0) {
            curr_node = &(*curr_node)->right;

        } else {
            key_found = true;
            break;
        }
    }

    if (result != NULL) {
        if (key_found) {
            *result = *curr_node;
        } else {
            *result = NULL;
        }
    }

    return key_found;
}

#define MOD_MASK_BINARY_TREE_FOREACH_CB(name) void name(struct mod_mask_binary_tree_node_t *node, void *clsr)
typedef MOD_MASK_BINARY_TREE_FOREACH_CB(mod_mask_binary_tree_foreach_cb_t);

void mod_mask_binary_tree_foreach (struct mod_mask_binary_tree_t *tree, mod_mask_binary_tree_foreach_cb_t *cb, void *clsr)
{
    if (tree->num_nodes < 1) return;

    mem_pool_t pool = {0};

    // TODO: Maybe get a real stack here? seems wasteful to create an array that
    // could hold all nodes. When we implement balancing, we can make this array
    // of size log(num_nodes), just need to be sure of an upper bound.
    struct mod_mask_binary_tree_node_t **stack =
        mem_pool_push_array (&pool, tree->num_nodes, struct mod_mask_binary_tree_node_t);

    int stack_idx = 0;
    struct mod_mask_binary_tree_node_t *curr_node = tree->root;
    while (true) {
        if (curr_node != NULL) {
            stack[stack_idx++] = curr_node;
            curr_node = curr_node->left;

        } else {
            if (stack_idx == 0) break;

            curr_node = stack[--stack_idx];
            cb (curr_node, clsr);

            curr_node = curr_node->right;
        }
    }

    mem_pool_destroy (&pool);
}

