/*
 * Copiright (C) 2019 Santiago León O.
 */
#include "common.h"
#include "bit_operations.c"
#include "status.c"
#include "scanner.c"
#include "cli_parser.c"
#include "binary_tree.c"

#include <xkbcommon/xkbcommon.h>
#include <linux/input-event-codes.h>
#include "kernel_keycode_names.h"
#include "xkb_keycode_names.h"
#include "keysym_names.h"

#include "keyboard_layout.c"
#include "xkb_file_backend.c"

#include <sys/wait.h>
#include <sys/mman.h>

#define SUCCESS ECMA_GREEN("OK")"\n"
#define FAIL ECMA_RED("FAILED")"\n"
#define TEST_NAME_WIDTH 40
#define TEST_INDENT 4

#define MAX_MODIFIERS_TO_TEST 20

void str_cat_kc (string_t *str, xkb_keycode_t kc)
{
    if (kernel_keycode_names[kc] != NULL) {
        str_cat_printf (str, "%d(%s)", kc, kernel_keycode_names[kc]);
    } else {
        str_cat_printf (str, "%d", kc);
    }
}

void str_cat_mod_state (string_t *str,
                      struct xkb_state *xkb_state, struct xkb_keymap *xkb_keymap,
                      xkb_mod_index_t xkb_num_mods, enum xkb_state_component type)
{
    bool is_first = true;
    // :libxkbcommon_modifier_indices_are_consecutive
    for (int i=0; i<xkb_num_mods; i++) {
        if (xkb_state_mod_index_is_active (xkb_state, i, type)) {
            if (!is_first) {
                str_cat_c (str, ", ");
            }
            is_first = false;

            str_cat_printf (str, "%s", xkb_keymap_mod_get_name (xkb_keymap, i));
        }
    }
}

struct compare_key_foreach_clsr_t {
    string_t *msg;

    struct xkb_keymap *k1;
    struct xkb_keymap *k2;

    bool equal_keymaps;
};

void compare_key_foreach (struct xkb_keymap *keymap, xkb_keycode_t kc, void *data)
{
    struct compare_key_foreach_clsr_t *clsr = (struct compare_key_foreach_clsr_t*) data;
    struct xkb_keymap *k1 = clsr->k1;
    struct xkb_keymap *k2 = clsr->k2;
    string_t *msg = clsr->msg;

    // Check the number of layouts in the key is valid
    int k1_num_layouts = xkb_keymap_num_layouts_for_key (k1, kc);
    int k2_num_layouts = xkb_keymap_num_layouts_for_key (k2, kc);
    if (k1_num_layouts != k2_num_layouts) {
        str_cat_printf (msg, "Key %d has %d layouts in k1 but %d in k2.\n", kc, k1_num_layouts, k2_num_layouts);
        clsr->equal_keymaps = false;

    } else if (k1_num_layouts > 1) {
        // NOTE: What does it mean to have a number of layouts of 0? (yes, it
        // happens).
        str_cat_c (msg, "Compared keymaps have more than 1 layout, this is not supported yet.\n");
        clsr->equal_keymaps = false;
    }

    // Check the number of levels in the key is valid.
    int k1_num_levels = xkb_keymap_num_levels_for_key (k1, kc, 0);
    int k2_num_levels = xkb_keymap_num_levels_for_key (k2, kc, 0);
    if (clsr->equal_keymaps && k1_num_levels != k2_num_levels) {
        str_cat_printf (msg, "Key %d has %d levels in k1 but %d in k2.\n", kc, k1_num_levels, k2_num_levels);
        clsr->equal_keymaps = false;
    }

    // Compare all keysyms in each level.
    if (clsr->equal_keymaps) {
        for (int lvl=0; clsr->equal_keymaps && lvl<k1_num_levels; lvl++) {
            const xkb_keysym_t *k1_keysyms;
            const xkb_keysym_t *k2_keysyms;
            int k1_num_syms = xkb_keymap_key_get_syms_by_level (k1, kc, 0, lvl, &k1_keysyms);
            int k2_num_syms = xkb_keymap_key_get_syms_by_level (k2, kc, 0, lvl, &k2_keysyms);
            if (k1_num_syms != k2_num_syms) {
                str_cat_printf (msg, "Key %d has %d keysyms in k1 but %d in k2.\n", kc, k1_num_syms, k2_num_syms);
                clsr->equal_keymaps = false;

            } else {
                for (int idx=0; clsr->equal_keymaps && idx<k1_num_syms; idx++) {
                    if (k1_keysyms[idx] != k2_keysyms[idx]) {
                        str_cat_printf (msg, "k1[kc:%d][lvl:%d] -> %d != k1[kc:%d][lvl:%d] -> %d\n",
                                        kc, lvl, k1_keysyms[idx], kc, lvl, k2_keysyms[idx]);
                        clsr->equal_keymaps = false;
                    }
                }
            }
        }
    }
}

// This tet check that two keymaps have the same keysym table. This means that
// each keycode had the same keysyms in each level.
// NOTE: This doesn't guarantee that the two keymaps will behave in the same way
// as they may have different modifier or key type configurations.
bool keymap_equality_test (struct xkb_keymap *k1, struct xkb_keymap *k2, string_t *msg)
{
    assert (msg != NULL);

    struct compare_key_foreach_clsr_t clsr;
    clsr.k1 = k1;
    clsr.k2 = k2;
    clsr.msg = msg;
    clsr.equal_keymaps = true;

    if (xkb_keymap_num_layouts (k1) != xkb_keymap_num_layouts (k2)) {
        str_cat_c (msg, "Keymaps have different number of layouts.\n");
        clsr.equal_keymaps = false;
    }

    xkb_keymap_key_for_each (k1, compare_key_foreach, &clsr);
    xkb_keymap_key_for_each (k2, compare_key_foreach, &clsr);

    return clsr.equal_keymaps;
}

struct modifier_key_t {
    int kc;
    key_modifier_mask_t modifiers;
    enum action_type_t type;

    struct modifier_key_t *next;
};

templ_sort_ll(modifier_key_sort, struct modifier_key_t, a->kc < b->kc);

struct get_modifier_keys_list_clsr_t {
    mem_pool_t *pool;
    int num_mods;

    struct modifier_key_t *modifier_list_end;
    int modifier_list_len;
    struct modifier_key_t *modifier_list;
};

void append_modifier_key (struct get_modifier_keys_list_clsr_t *clsr,
                          int kc, key_modifier_mask_t mask, enum action_type_t type)
{
    struct modifier_key_t *new_key = mem_pool_push_struct (clsr->pool, struct modifier_key_t);
    new_key->next = NULL;
    new_key->kc = kc;
    new_key->type = type;

    new_key->modifiers = mask;

    if (clsr->modifier_list == NULL) {
        clsr->modifier_list = new_key;
    } else {
        clsr->modifier_list_end->next = new_key;
    }
    clsr->modifier_list_end = new_key;

    clsr->modifier_list_len++;
}

// This creates a canonical mask of all active modifiers of the specified type
// (set, locked or latched). We use it to be able to compare modifiers set
// across different keymaps.
// NOTE: As far as I can see libxkbcommon always returns real modifiers here.
// While computing a canonical representation of modifiers we assume this will
// be the case, if it ever stops being true we will assert.
key_modifier_mask_t get_canonical_real_mod_state (struct xkb_keymap *keymap,
                                                  struct xkb_state *state,
                                                  int num_mods,
                                                  enum xkb_state_component type)
{
    key_modifier_mask_t res = 0x0;
    char *real_modifiers[] = XKB_FILE_BACKEND_REAL_MODIFIER_NAMES_LIST;

    // :libxkbcommon_modifier_indices_are_consecutive
    for (int i=0; i<num_mods; i++) {
        if (xkb_state_mod_index_is_active (state, i, type)) {
            key_modifier_mask_t curr_mask = 0x0;
            const char *name = xkb_keymap_mod_get_name (keymap, i);
            for (int j=0; j<ARRAY_SIZE(real_modifiers); j++) {
                if (strcasecmp (name, real_modifiers[j]) == 0) {
                    curr_mask = 1<<j;
                    break;
                }
            }

            // If this assert fails, then it means we got a modifier not in the
            // real modifier array, maybe it was a virtual modifier? As far as
            // I've seen, libxkbcommon doesn't do this.
            assert (curr_mask != 0x0);
            res |= curr_mask;
        }
    }

    return res;
}

void get_modifier_keys_list_foreach (struct xkb_keymap *keymap, xkb_keycode_t kc, void *data)
{
    struct xkb_state *xkb_state = xkb_state_new(keymap);
    assert (xkb_state);

    struct get_modifier_keys_list_clsr_t *clsr =
        (struct get_modifier_keys_list_clsr_t*)data;

    enum xkb_state_component changed_components = xkb_state_update_key (xkb_state, kc+8, XKB_KEY_DOWN);
    if (changed_components) {
        if (changed_components & XKB_STATE_MODS_LOCKED) {
            key_modifier_mask_t mask =
                get_canonical_real_mod_state (keymap, xkb_state, clsr->num_mods, XKB_STATE_MODS_LOCKED);
            append_modifier_key (clsr, kc, mask, KEY_ACTION_TYPE_MOD_LOCK);

        } else if (changed_components & XKB_STATE_MODS_LATCHED) {
            key_modifier_mask_t mask =
                get_canonical_real_mod_state (keymap, xkb_state, clsr->num_mods, XKB_STATE_MODS_LATCHED);
            append_modifier_key (clsr, kc, mask, KEY_ACTION_TYPE_MOD_LATCH);

        } else if (changed_components & XKB_STATE_MODS_DEPRESSED) {
            key_modifier_mask_t mask =
                get_canonical_real_mod_state (keymap, xkb_state, clsr->num_mods, XKB_STATE_MODS_DEPRESSED);
            append_modifier_key (clsr, kc, mask, KEY_ACTION_TYPE_MOD_SET);
        }
    }

    xkb_state_unref(xkb_state);
}

struct modifier_key_t* get_modifier_keys_list (mem_pool_t *pool, struct xkb_keymap *keymap, int *len)
{
    struct get_modifier_keys_list_clsr_t clsr = {0};
    clsr.pool = pool;
    clsr.num_mods = xkb_keymap_num_mods (keymap);
    clsr.modifier_list_len = 0;
    xkb_keymap_key_for_each (keymap, get_modifier_keys_list_foreach, &clsr);

    if (len != NULL) {
        *len = clsr.modifier_list_len;
    }

    return clsr.modifier_list;
}

struct compare_key_states_foreach_clsr_t {
    struct modifier_key_t *mod_keys;
    int num_mod_keys;

    struct xkb_state *s1;
    struct xkb_state *s2;

    bool equal_states;

    // If the test fails, we store where it failed here.
    xkb_keycode_t differing_kc;
    int num_syms_1;
    int num_syms_2;
    xkb_keysym_t sym_1;
    xkb_keysym_t sym_2;
};

// NOTE: Assumes mod_key is sorted by kc. We are using binary search.
bool is_kc_mod_key (int kc, struct modifier_key_t *mod_keys, int num_mod_keys)
{
    int upper = num_mod_keys-1;
    int lower = 0;

    while (lower <= upper) {
        int mid = (upper+lower)/2;
        if (mod_keys[mid].kc < kc) {
            lower = mid+1;

        } else if (mod_keys[mid].kc > kc) {
            upper = mid-1;

        } else {
            return true;
        }
    }

    return false;
}

void compare_key_states_foreach (struct xkb_keymap *keymap, xkb_keycode_t kc, void *data)
{
    struct compare_key_states_foreach_clsr_t *clsr =
        (struct compare_key_states_foreach_clsr_t*)data;

    if (!is_kc_mod_key (kc, clsr->mod_keys, clsr->num_mod_keys)) {
        xkb_state_update_key (clsr->s1, kc+8, XKB_KEY_DOWN);
        xkb_state_update_key (clsr->s2, kc+8, XKB_KEY_DOWN);

        const xkb_keysym_t *syms_1, *syms_2;
        int num_syms_1 = xkb_state_key_get_syms (clsr->s1, kc, &syms_1);
        int num_syms_2 = xkb_state_key_get_syms (clsr->s2, kc, &syms_2);

        if (num_syms_1 == num_syms_2) {
            for (int i=0; clsr->equal_states && i<num_syms_1; i++) {
                if (syms_1[i] != syms_2[i]) {
                    clsr->equal_states = false;
                    clsr->differing_kc = kc-8;
                    clsr->sym_1 = syms_1[i];
                    clsr->sym_2 = syms_2[i];
                }
            }

        } else {
            clsr->equal_states = false;
            clsr->differing_kc = kc-8;
            clsr->num_syms_1 = num_syms_1;
            clsr->num_syms_2 = num_syms_2;
        }

        xkb_state_update_key (clsr->s1, kc+8, XKB_KEY_UP);
        xkb_state_update_key (clsr->s2, kc+8, XKB_KEY_UP);
    }
}

// This function takes pressed_keys, interpret each bit as a mask where each bit
// index refers to an element of the modifier_keys array, then it activates the
// modifiers of the corresponding keys. Keys that set a modifier are pressed,
// those that lock a modifier are pressed and released.
// TODO: Keys that latch modifiers are currently ignored.
void set_active_modifier_keys (uint32_t active_modifier_keys, struct modifier_key_t *mod_keys,
                               struct xkb_state *s1, struct xkb_state *s2)
{
    while (active_modifier_keys) {
        key_modifier_mask_t next_bit_mask = active_modifier_keys & -active_modifier_keys;

        struct modifier_key_t mod_key = mod_keys[bit_pos(next_bit_mask)];
        if (mod_key.type != KEY_ACTION_TYPE_MOD_LATCH) {
            // We don't test latch modifiers. They need a special
            // treatment because they are unset everytime a key is
            // pressed. Currently we press modifier keys, then press
            // each non modifier key compare the keysyms produced.
            //
            // They are considered modifier keys, though. Because we
            // don't want to press them (as if they were non
            // modifier keys) when comparing keysyms.
            xkb_state_update_key (s1, mod_key.kc+8, XKB_KEY_DOWN);
            xkb_state_update_key (s2, mod_key.kc+8, XKB_KEY_DOWN);

            // If the modifier is locked, then release the key.
            if (mod_key.type == KEY_ACTION_TYPE_MOD_LOCK) {
                xkb_state_update_key (s1, mod_key.kc+8, XKB_KEY_UP);
                xkb_state_update_key (s2, mod_key.kc+8, XKB_KEY_UP);
            }
        }

        active_modifier_keys = active_modifier_keys & (active_modifier_keys-1);
    }

}

void str_cat_active_modifier_keys (string_t *str, uint32_t active_modifier_keys,
                                   struct modifier_key_t *mod_keys)
{
    if (active_modifier_keys == 0) {
        str_cat_c (str, " none");

    } else {
        while (active_modifier_keys) {
            key_modifier_mask_t next_bit_mask = active_modifier_keys & -active_modifier_keys;
            struct modifier_key_t mod_key = mod_keys[bit_pos(next_bit_mask)];
            str_cat_c (str, " ");
            str_cat_kc (str, mod_key.kc);
            active_modifier_keys = active_modifier_keys & (active_modifier_keys-1);
        }
    }
    str_cat_c (str, "\n");
}

void str_cat_passed_modifier_tests (string_t *str, uint32_t last_active_modifier_keys,
                                    struct modifier_key_t *mod_keys)
{
    str_cat_c (str, " PASSED MODIFIER COMBINATIONS:\n");
    for (int passed_test = 0; passed_test < last_active_modifier_keys; passed_test++) {
        str_cat_c (str, " -");
        str_cat_active_modifier_keys (str, passed_test, mod_keys);
    }
    str_cat_c (str, "\n");

    str_cat_c (str, " FAILED MODIFIER COMBINATION:");
    str_cat_active_modifier_keys (str, last_active_modifier_keys, mod_keys);
}

// This test is a more functional equality test of the keymaps. The idea is to
// press all modifier combinations and check that the resulting keysyms in each
// key are the same. Some caveats of how the test works, (we could fix them but
// it probably will be overkill?):
//
//  - We only get modifiers from the first level, actions that set modifiers in
//    other key levels are ignored and not checked.
//  - We currently ignore latched modifiers.
//  - We only compare the keysyms of keys that don't set a modifier in their
//    first level. It's possible to have modifier keys that in an other level
//    produces a keysym, differences here won't be caught.
//  - We ignore keysyms of keys that set/lock modifiers (modifier keys).
//
// NOTE: This assumes that the keymaps passed the keymap_equality_test.
// NOTE: This has exponential complexity on the number of keys that trigger
// modifiers. We could do a faster test based on key type information. The
// problem is I don't see how we can get type information from libxkbcommon, so
// we would need to use our internal representation of keymaps, and that's what
// we want to check.
bool modifier_equality_test (struct xkb_keymap *k1, struct xkb_keymap *k2, string_t *msg)
{
    mem_pool_t pool = {0};
    bool are_equal = true;
    int num_mod_keys = 0;

    int mod_list_k1_len;
    struct modifier_key_t *mod_list_k1 = get_modifier_keys_list (&pool, k1, &mod_list_k1_len);
    modifier_key_sort (&mod_list_k1, mod_list_k1_len);

    int mod_list_k2_len;
    struct modifier_key_t *mod_list_k2 = get_modifier_keys_list (&pool, k2, &mod_list_k2_len);
    modifier_key_sort (&mod_list_k2, mod_list_k2_len);

    if (mod_list_k1_len != mod_list_k2_len) {
        str_cat_c (msg, "Keymaps don't have the same number of modifier keys.\n");
        are_equal = false;
    }

    // Check that both keymaps have the same modifiers.
    if (are_equal) {
        struct modifier_key_t *curr_mod_k1 = mod_list_k1, *curr_mod_k2 = mod_list_k2;
        while (are_equal == true && curr_mod_k1 != NULL && curr_mod_k2 != NULL) {
            if (curr_mod_k1->kc != curr_mod_k2->kc) {
                str_cat_c (msg, "Keymaps don't map modifiers to the same keys.\n");
                are_equal = false;

            } else if (curr_mod_k1->modifiers != curr_mod_k2->modifiers) {
                str_cat_printf (msg, "Keymaps set, lock or latch different real modifiers with key %d.\n",
                                curr_mod_k1->kc);
                are_equal = false;
            }

            num_mod_keys++;

            curr_mod_k1 = curr_mod_k1->next;
            curr_mod_k2 = curr_mod_k2->next;
        }


        // We checked the lengths of the lists of modifier keys before. This
        // must hold here
        assert (curr_mod_k1 == NULL && curr_mod_k2 == NULL);
    }

    int max_mod_keys = MAX_MODIFIERS_TO_TEST;
    if (are_equal) {
        struct compare_key_states_foreach_clsr_t clsr = {0};

        // Put modifier key nodes into an array sorted by keycode. This is required
        // so we can check in a fast way if a keycode is a modifier key.
        struct modifier_key_t *curr_mod_k1 = mod_list_k1;
        clsr.num_mod_keys = num_mod_keys;
        clsr.mod_keys = mem_pool_push_array (&pool, num_mod_keys, struct modifier_key_t);
        for (int i=0; i<num_mod_keys; i++) {
            clsr.mod_keys[i] = *curr_mod_k1;
            clsr.mod_keys[i].next = NULL;

            curr_mod_k1 = curr_mod_k1->next;
        }

        // Iterate all num_mod_keys^2 combinations and check that the resulting
        // keysyms are the same.
        // NOTE: This grows exponentially with the number of modifier keys in a
        // layout!
        if (num_mod_keys <= max_mod_keys) {
            for (uint32_t pressed_keys = 0; are_equal && pressed_keys<(1<<num_mod_keys); pressed_keys++) {

                struct xkb_state *s1 = xkb_state_new(k1);
                assert (s1);

                struct xkb_state *s2 = xkb_state_new(k2);
                assert (s2);

                set_active_modifier_keys (pressed_keys, clsr.mod_keys, s1, s2);

                clsr.s1 = s1;
                clsr.s2 = s2;
                clsr.equal_states = true;
                xkb_keymap_key_for_each (k1, compare_key_states_foreach, &clsr);

                if (!clsr.equal_states && msg != NULL) {
                    str_cat_printf (msg, "Modifiers produce different keysyms.\n");

                    str_cat_passed_modifier_tests (msg, pressed_keys, clsr.mod_keys);

                    str_cat_c (msg, " kc: ");
                    str_cat_kc (msg, clsr.differing_kc);
                    str_cat_c (msg, "\n");
                    if (clsr.num_syms_1 == clsr.num_syms_2) {
                        char buff[64];
                        xkb_keysym_get_name (clsr.sym_1, buff, ARRAY_SIZE(buff));
                        str_cat_printf (msg, " sym_1: %s\n", buff);

                        xkb_keysym_get_name (clsr.sym_2, buff, ARRAY_SIZE(buff));
                        str_cat_printf (msg, " sym_2: %s\n", buff);

                    } else {
                        str_cat_printf (msg, " num_syms_1: %d\n", clsr.num_syms_1);
                        str_cat_printf (msg, " num_syms_2: %d\n", clsr.num_syms_2);
                    }

                    are_equal = false;
                }

                xkb_state_unref(s1);
                xkb_state_unref(s2);
            }
        }
    }

    if (are_equal && num_mod_keys > max_mod_keys) {
        str_cat_printf (msg, "We don't do modifier tests on keymaps with more than %d modifier keys.\n",
                        max_mod_keys);
    }

    mem_pool_destroy (&pool);
    return are_equal;
}

// This tests that LEDs in both keymaps are equivalent, but not identical. We
// iterate modifier key combinations like for the modifier_equality_test(), then
// for each LED that is activated in a keymap, we expect it also to be defined
// in the other keymap and to also be active. This doesn't check LEDs that can't
// be activated via a modifier key combination.
// NOTE: Assumes that modifier_equality_test() passed.
bool led_equality_test (struct xkb_keymap *k1, struct xkb_keymap *k2, string_t *msg)
{
    mem_pool_t pool = {0};
    bool are_equal = true;
    int num_mod_keys = 0;

    int mod_list_k1_len;
    struct modifier_key_t *mod_list_k1 = get_modifier_keys_list (&pool, k1, &mod_list_k1_len);
    modifier_key_sort (&mod_list_k1, mod_list_k1_len);

    int mod_list_k2_len;
    struct modifier_key_t *mod_list_k2 = get_modifier_keys_list (&pool, k2, &mod_list_k2_len);
    modifier_key_sort (&mod_list_k2, mod_list_k2_len);

    assert (mod_list_k1_len == mod_list_k2_len);
    num_mod_keys = mod_list_k1_len;

    int num_leds_k1 = xkb_keymap_num_leds (k1);
    int num_leds_k2 = xkb_keymap_num_leds (k2);
    // We don't check num_leds_k1 == num_leds_k2 because we are removing
    // indicators from the original xkb files. Indicators for controls are
    // ignored, also indicators for virtual modifiers that don't get defined.
    // Indicators for groups may be useful if we ever support groups, currently
    // those are removed too.

    int max_mod_keys = MAX_MODIFIERS_TO_TEST;
    if (are_equal) {
        // Put modifier key nodes into an array sorted by keycode. This is used
        // for quick lookuop of modifier keys.
        struct modifier_key_t *curr_mod_k1 = mod_list_k1;
        struct modifier_key_t *mod_keys = mem_pool_push_array (&pool, num_mod_keys, struct modifier_key_t);
        for (int i=0; i<num_mod_keys; i++) {
            mod_keys[i] = *curr_mod_k1;
            mod_keys[i].next = NULL;

            curr_mod_k1 = curr_mod_k1->next;
        }

        // Iterate all num_mod_keys^2 combinations and check that LEDs work the
        // same.
        // NOTE: This grows exponentially with the number of modifier keys in a
        // layout!
        if (num_mod_keys <= max_mod_keys) {
            for (uint32_t pressed_keys = 0; are_equal && pressed_keys<(1<<num_mod_keys); pressed_keys++) {

                struct xkb_state *s1 = xkb_state_new(k1);
                assert (s1);

                struct xkb_state *s2 = xkb_state_new(k2);
                assert (s2);

                set_active_modifier_keys (pressed_keys, mod_keys, s1, s2);

                const char *ind_name;
                bool ind_1, ind_2;
                {
                    for (int ind_idx = 0; are_equal && ind_idx < num_leds_k1; ind_idx++) {
                        ind_name = xkb_keymap_led_get_name (k1, ind_idx);
                        if (ind_name != NULL && xkb_state_led_name_is_active (s1, ind_name)) {
                            ind_1 = 1;
                            if (xkb_keymap_led_get_index (k2, ind_name) == XKB_LED_INVALID) {
                                are_equal = false;
                                str_cat_printf (msg, "Indicator '%s' set in k1 but undefined in k2", ind_name);
                            }

                            if (are_equal && ind_name != NULL) {
                                if (!xkb_state_led_name_is_active (s2, ind_name)) {
                                    ind_2 = 0;
                                    are_equal = false;
                                }
                            }
                        }
                    }

                    for (int ind_idx = 0; are_equal && ind_idx < num_leds_k2; ind_idx++) {
                        ind_name = xkb_keymap_led_get_name (k2, ind_idx);
                        if (ind_name != NULL && xkb_state_led_name_is_active (s2, ind_name)) {
                            ind_2 = 1;
                            if (xkb_keymap_led_get_index (k1, ind_name) == XKB_LED_INVALID) {
                                are_equal = false;
                                str_cat_printf (msg, "Indicator '%s' defined in k2 but not in k1", ind_name);
                            }

                            if (are_equal) {
                                if (!xkb_state_led_name_is_active (s1, ind_name)) {
                                    ind_1 = 0;
                                    are_equal = false;
                                }
                            }
                        }
                    }
                }

                if (!are_equal && msg != NULL) {
                    str_cat_printf (msg, "Modifiers produce different keysyms.\n");

                    str_cat_passed_modifier_tests (msg, pressed_keys, mod_keys);
                    str_cat_printf (msg, "  Indicator 1: %s -> %d\n", ind_name, ind_1?1:0);
                    str_cat_printf (msg, "  Indicator 2: %s -> %d\n", ind_name, ind_2?1:0);

                    are_equal = false;
                }

                xkb_state_unref(s1);
                xkb_state_unref(s2);
            }
        }
    }

    if (are_equal && num_mod_keys > max_mod_keys) {
        str_cat_printf (msg, "We don't do modifier tests on keymaps with more than %d modifier keys.\n",
                        max_mod_keys);
    }

    mem_pool_destroy (&pool);
    return are_equal;
}

struct print_modifier_info_foreach_clsr_t {
    xkb_mod_index_t xkb_num_mods;
    string_t *str;
};

void print_modifier_info_foreach (struct xkb_keymap *keymap, xkb_keycode_t kc, void *data)
{
    struct print_modifier_info_foreach_clsr_t *clsr =
        (struct print_modifier_info_foreach_clsr_t*)data;
    string_t *str = clsr->str;

    struct xkb_state *xkb_state = xkb_state_new(keymap);
    if (!xkb_state) {
        str_cat_c (str, "could not create xkb state.\n");
        return;
    }

    xkb_mod_index_t xkb_num_mods = clsr->xkb_num_mods;

    enum xkb_state_component changed_components = xkb_state_update_key (xkb_state, kc+8, XKB_KEY_DOWN);
    if (changed_components) {
        str_cat_printf (str, " %s (%d): ", kernel_keycode_names[kc], kc);
        if (changed_components & XKB_STATE_MODS_DEPRESSED) {
            str_cat_c (str, "Sets(");
            str_cat_mod_state (str, xkb_state, keymap, xkb_num_mods, XKB_STATE_MODS_DEPRESSED);
            str_cat_c (str, ") ");
        }

        if (changed_components & XKB_STATE_MODS_LATCHED) {
            str_cat_c (str, "Latches(");
            str_cat_mod_state (str, xkb_state, keymap, xkb_num_mods, XKB_STATE_MODS_LATCHED);
            str_cat_c (str, ") ");
        }

        if (changed_components & XKB_STATE_MODS_LOCKED) {
            str_cat_c (str, "Locks(");
            str_cat_mod_state (str, xkb_state, keymap, xkb_num_mods, XKB_STATE_MODS_LOCKED);
            str_cat_c (str, ") ");
        }

        if (changed_components & XKB_STATE_MODS_EFFECTIVE) {
            // Is this just an OR of the other modifier masks? or is
            // this related to "consumed mods".
            str_cat_c (str, "Effective(");
            str_cat_mod_state (str, xkb_state, keymap, xkb_num_mods, XKB_STATE_MODS_EFFECTIVE);
            str_cat_c (str, ") ");
        }

        if (changed_components & XKB_STATE_LAYOUT_DEPRESSED ||
            changed_components & XKB_STATE_LAYOUT_LATCHED ||
            changed_components & XKB_STATE_LAYOUT_LOCKED ||
            changed_components & XKB_STATE_LAYOUT_EFFECTIVE ) {
            str_cat_c (str, "LayoutChange ");
        }

        if (changed_components & XKB_STATE_LEDS) {
            str_cat_c (str, "LedsChange: ");
            bool is_first = true;
            int num_leds= xkb_keymap_num_leds (keymap);
            for (int ind_idx = 0; ind_idx < num_leds; ind_idx++) {
                if (xkb_state_led_index_is_active (xkb_state, ind_idx)) {
                    // NOTE: Looks like undefined leds are active by default? I
                    // would've expected the opposite. We check here that the
                    // led is valid by looking up its name.
                    const char *name = xkb_keymap_led_get_name (keymap, ind_idx);
                    if (name != NULL) {
                        if (!is_first) {
                            str_cat_c (str, ", ");
                        }
                        is_first = false;

                        str_cat_c (str, name);
                        str_cat_printf (str, "(%d)", ind_idx);
                    }
                }
            }
        }

        str_cat_c (str, "\n");
    }

    xkb_state_unref(xkb_state);
}

void str_cat_xkbcommon_modifier_info (string_t *str, struct xkb_keymap *keymap)
{
    str_cat_c (str, "Modifiers: ");
    // :libxkbcommon_modifier_indices_are_consecutive
    xkb_mod_index_t xkb_num_mods = xkb_keymap_num_mods (keymap);
    for (int i=0; i<xkb_num_mods; i++) {
        str_cat_printf (str, "%s", xkb_keymap_mod_get_name (keymap, i));
        if (i < xkb_num_mods-1) {
            str_cat_c (str, ", ");
        }
    }

    str_cat_c (str, "\n\nModifier mapping:\n");
    // Iterate all keycodes and detect those that change the state of a
    // modifier.
    struct print_modifier_info_foreach_clsr_t clsr;
    clsr.xkb_num_mods = xkb_num_mods;
    clsr.str = str;
    xkb_keymap_key_for_each (keymap, print_modifier_info_foreach, &clsr);
}

enum input_type_t {
    INPUT_NONE,
    INPUT_RMLVO_NAMES,
    INPUT_XKB_FILE
};

enum crash_safety_mode_t {
    CRASH_MODE_SAFE,
    CRASH_MODE_UNSAFE
};

// Every time we iterate over the modifiers on a keymap we assume indices are
// always consecutive. libxkbcommon's documentation does not explicitly
// guarantees that but it looks like this is true, if this ever becomes false,
// we will assert in this function. We call this whenever we create a new keymap
// using libxkbcommon.
// :libxkbcommon_modifier_indices_are_consecutive
void assert_consecutive_modifiers (struct xkb_keymap *keymap)
{
    xkb_mod_index_t xkb_num_mods = xkb_keymap_num_mods (keymap);
    for (int i=0; i<xkb_num_mods; i++) {
        const char *name = xkb_keymap_mod_get_name (keymap, i);
        assert (name != NULL);
    }
}

// TODO: Do this in a child process to guard against segmentation faults
bool writer_test (struct keyboard_layout_t *internal_keymap,
                  string_t *writer_keymap_str,
                  string_t *result)
{
    bool success = true;

    struct status_t status = {0};
    xkb_file_write (internal_keymap, writer_keymap_str, &status);

    if (status_is_error (&status)) {
        str_cat_c (result, "Internal xkb writer failed.\n");
        str_cat_status (result, &status);
        success = false;
    }

    return success;
}

void printf_successful_layout (char *layout)
{
    int width = strlen(layout) + 2;
    printf ("%s: ", layout);
    while (width < TEST_NAME_WIDTH + TEST_INDENT - 1) {
        printf (".");
        width++;
    }
    printf (" %s", SUCCESS);
}

void str_cat_test_name (string_t *str, char *test_name)
{
    size_t final_width = str_len(str) + TEST_NAME_WIDTH;
    str_cat_c (str, test_name);
    str_cat_c (str, " ");
    while (str_len(str) < final_width-1) {
        str_cat_c (str, ".");
    }
    str_cat_c (str, " ");
}

// Names for shared memory objects are global to the system and they will still
// be defined if the object wasn't unlinked or the last execution of the program
// crashed. This macro is used to create a name that will not collide.
// TODO: If we had a full language at compile time like in JAI we could create a
// much more meaningful name here. Create a version that is scoped and
// automatically calls UNLINK_SHARED then for variables that span multiple
// scopes get a handle that UNLINK_SHARED receives.
//
// TODO: Stop doing this. I don't think obscuring the fact that you need to
// choose a name for a shared variable is useful. Better make it part of the
// macro so the user has to think about how to handle this.
#define SHARED_VARIABLE_NAME(NAME) "/" #NAME ":SHARED_VARIABLE_WM1WNTK8XM"

#define NEW_SHARED_VARIABLE(TYPE,SYMBOL,VALUE) \
    NEW_SHARED_VARIABLE_NAMED(TYPE,SYMBOL,VALUE,SHARED_VARIABLE_NAME(SYMBOL))
#define UNLINK_SHARED_VARIABLE(SYMBOL) UNLINK_SHARED_VARIABLE_NAMED(SHARED_VARIABLE_NAME(SYMBOL))

void wait_and_cat_output (mem_pool_t *pool, bool *success,
                          char *stdout_fname, char *stderr_fname,
                          string_t *result)
{
    int child_status;
    wait (&child_status);

    if (!WIFEXITED(child_status)) {
        *success = false;
    }

    if (!*success) {
        str_cat_c (result, FAIL);

        string_t child_output = {0};
        char *stdout_str = full_file_read (pool, stdout_fname, NULL);
        if (*stdout_str != '\0') {
            str_cat_c (&child_output, ECMA_CYAN("stdout:\n"));
            str_cat_indented_c (&child_output, stdout_str, 2);
        }

        char *stderr_str = full_file_read (pool, stderr_fname, NULL);
        if (*stderr_str != '\0') {
            str_cat_c (&child_output, ECMA_CYAN("stderr:\n"));
            str_cat_indented_c (&child_output, stderr_str, 2);
        }

        if (!WIFEXITED(child_status)) {
            str_cat_printf (&child_output, "Exited abnormally with status: %d\n", child_status);
        }

        str_cat_indented (result, &child_output, 2);
        str_free (&child_output);

    } else {
        str_cat_c (result, SUCCESS);
    }

    unlink (stdout_fname);
    unlink (stderr_fname);
}

bool test_file_parsing (enum crash_safety_mode_t crash_safety,
                        struct xkb_context *xkb_ctx,
                        char *xkb_str,
                        struct xkb_keymap **libxkbcommon_keymap, struct keyboard_layout_t *internal_keymap,
                        string_t *result)
{
    bool retval = true;
    char *stdout_fname = "tmp_stdout";
    char *stderr_fname = "tmp_stderr";
    mem_pool_t pool = {0};

    NEW_SHARED_VARIABLE (bool, success, true);

    str_cat_test_name (result, "libxkbcommon parser");
    {
        if (fork() == 0) {
            freopen (stdout_fname, "w", stdout);
            setvbuf (stdout, NULL, _IONBF, 0);

            freopen (stderr_fname, "w", stderr);
            setvbuf (stderr, NULL, _IONBF, 0);

            // Here we use the xkb_ctx created before, because this is a process
            // and not a thread, unrefing it should cause no problems as it's a
            // copy of the one in the parent process.
            struct xkb_keymap *keymap =
                xkb_keymap_new_from_string(xkb_ctx, xkb_str,
                                           XKB_KEYMAP_FORMAT_TEXT_V1,
                                           XKB_KEYMAP_COMPILE_NO_FLAGS);
            if (!keymap) {
                *success = false;
            } else {
                assert_consecutive_modifiers (keymap);
            }

            if (keymap) xkb_keymap_unref(keymap);
            if (xkb_ctx) xkb_context_unref(xkb_ctx);

            exit(0);
        }

        wait_and_cat_output (&pool, success, stdout_fname, stderr_fname, result);
    }
    retval = *success;

    str_cat_test_name (result, "internal parser");
    {
        *success = true;

        if (fork() == 0) {
            freopen (stdout_fname, "w", stdout);
            setvbuf (stdout, NULL, _IONBF, 0);

            freopen (stderr_fname, "w", stderr);
            setvbuf (stderr, NULL, _IONBF, 0);

            struct keyboard_layout_t keymap = {0};
            string_t log = {0};
            if (!xkb_file_parse_verbose (xkb_str, &keymap, &log)) {
                *success = false;
                printf ("%s", str_data(&log));
            }

            str_free (&log);
            keyboard_layout_destroy (&keymap);

            exit(0);
        }

        wait_and_cat_output (&pool, success, stdout_fname, stderr_fname, result);
    }

    retval = retval && *success;
    UNLINK_SHARED_VARIABLE (success);

    // If none of the parsers failed, and the caller wants the paring keymaps,
    // parse layouts again and set them.
    if (retval || crash_safety == CRASH_MODE_UNSAFE) {
        if (internal_keymap) {
            *internal_keymap = ZERO_INIT(struct keyboard_layout_t);
            xkb_file_parse_verbose (xkb_str, internal_keymap, NULL);
        }

        if (libxkbcommon_keymap) {
            *libxkbcommon_keymap =
                xkb_keymap_new_from_string(xkb_ctx, xkb_str,
                                           XKB_KEYMAP_FORMAT_TEXT_V1,
                                           XKB_KEYMAP_COMPILE_NO_FLAGS);
        }
    }

    mem_pool_destroy (&pool);

    return retval;
}

bool test_xkb_file (enum crash_safety_mode_t crash_safety,
                    string_t *input_str,
                    string_t *result, string_t *info,
                    string_t *writer_keymap_str, string_t *writer_keymap_str_2)
{
    assert (input_str != NULL && result != NULL &&
            writer_keymap_str != NULL && writer_keymap_str_2 != NULL);
    bool success = true;

    struct xkb_context *xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!xkb_ctx) {
        str_cat_c (result, "could not create xkb context.\n");
        success = false;
    }

    struct xkb_keymap *input_libxkbcommon_keymap = NULL;
    struct keyboard_layout_t input_internal_keymap = {0};
    if (success) {
        str_cat_test_name (result, "Input parsing Test");
        string_t log = {0};
        if (!test_file_parsing (crash_safety,
                                xkb_ctx, str_data(input_str),
                                &input_libxkbcommon_keymap, &input_internal_keymap,
                                &log)) {
            str_cat_c (result, FAIL);
            str_cat_indented (result, &log, 1);
            success = false;
        } else {
            str_cat_c (result, SUCCESS);
        }
        str_free (&log);
    }

    if (success) {
        str_cat_test_name (result, "Writer test");
        string_t log = {0};
        if (!writer_test (&input_internal_keymap, writer_keymap_str, &log)) {
            str_cat_c (result, FAIL);
            str_cat_indented (result, &log, 1);
            success = false;
        } else {
            str_cat_c (result, SUCCESS);
        }
        str_free (&log);
    }

    struct xkb_keymap *writer_output_libxkbcommon_keymap = NULL;
    struct keyboard_layout_t writer_output_internal_keymap = {0};
    if (success) {
        str_cat_test_name (result, "Writer output parsing test");
        string_t log = {0};
        if (!test_file_parsing (crash_safety,
                                xkb_ctx, str_data(writer_keymap_str),
                                &writer_output_libxkbcommon_keymap, &writer_output_internal_keymap,
                                &log)) {
            str_cat_c (result, FAIL);
            str_cat_indented (result, &log, 1);
            success = false;
        } else {
            str_cat_c (result, SUCCESS);
        }
        str_free (&log);
    }

    if (success) {
        str_cat_test_name (result, "Symbol Equality Test");
        string_t tmp = {0};
        if (!keymap_equality_test (input_libxkbcommon_keymap, writer_output_libxkbcommon_keymap, &tmp)) {
            str_cat_c (result, FAIL);
            str_cat (result, &tmp);
            success = false;
        } else {
            str_cat_c (result, SUCCESS);
        }
    }

    if (success) {
        str_cat_test_name (result, "Modifier Equality Test");
        string_t tmp = {0};
        if (!modifier_equality_test (input_libxkbcommon_keymap, writer_output_libxkbcommon_keymap, &tmp)) {
            str_cat_c (result, FAIL);
            str_cat (result, &tmp);
            success = false;
        } else {
            str_cat_c (result, SUCCESS);
        }
    }

    if (success) {
        struct keyboard_layout_t keymap = {0};
        str_cat_test_name (result, "Idempotency Test");

        if (success) {
            struct status_t status = {0};
            xkb_file_write (&writer_output_internal_keymap, writer_keymap_str_2, &status);

            if (status_is_error (&status)) {
                str_cat_c (result, FAIL);
                str_cat_status (result, &status);
                str_cat_c (result, "Can't write our own output.\n");
                success = false;
            }
        }

        if(success) {
            if (strcmp (str_data(writer_keymap_str), str_data(writer_keymap_str_2)) != 0) {
                str_cat_c (result, FAIL);
                str_cat_c (result, "Parsing our own output does not generate identical XKB files.\n");
                success = false;

            } else {
                str_cat_c (result, SUCCESS);
            }
        }

        keyboard_layout_destroy (&keymap);
    }

    if (success) {
        str_cat_test_name (result, "LED Equality Test");
        string_t tmp = {0};
        if (!led_equality_test (input_libxkbcommon_keymap, writer_output_libxkbcommon_keymap, &tmp)) {
            str_cat_c (result, FAIL);
            str_cat (result, &tmp);
            success = false;
        } else {
            str_cat_c (result, SUCCESS);
        }
    }

    if (info != NULL) {
        // Print parser input information
        if (input_libxkbcommon_keymap) {
            str_cat_c (info, ECMA_MAGENTA("\nParser input info (libxkbcommon):\n"));

            string_t tmp = {0};
            str_cat_xkbcommon_modifier_info (&tmp, input_libxkbcommon_keymap);
            str_cat_indented (info, &tmp, 1);
            str_free (&tmp);
        }

        // Print writer output information
        if (writer_output_libxkbcommon_keymap) {
            str_cat_c (info, ECMA_MAGENTA("\nWriter output info (libxkbcommon):\n"));

            string_t tmp = {0};
            str_cat_xkbcommon_modifier_info (&tmp, writer_output_libxkbcommon_keymap);
            str_cat_indented (info, &tmp, 1);

            // TODO: Maybe don't parse the layout again here? get this from the original
            // call inside the writeback test. Or... maybe just don't but the writeback
            // test in a separate function?.
            struct keyboard_layout_t keymap = {0};
            str_cat_c (info, ECMA_MAGENTA("\nXKB parser info:\n"));

            str_set (&tmp, "");
            xkb_file_parse_verbose (str_data(input_str), &keymap, &tmp);
            str_cat_indented (info, &tmp, 1);

            keyboard_layout_destroy (&keymap);
            str_free (&tmp);
        }
    }

    keyboard_layout_destroy (&input_internal_keymap);
    keyboard_layout_destroy (&writer_output_internal_keymap);

    if (input_libxkbcommon_keymap) xkb_keymap_unref(input_libxkbcommon_keymap);
    if (writer_output_libxkbcommon_keymap) xkb_keymap_unref(writer_output_libxkbcommon_keymap);
    if (xkb_ctx) xkb_context_unref(xkb_ctx);

    return success;
}

void xkb_str_from_rmlvo (char *rules, char *model, char *layout, char *variant, char *options, string_t *xkb_str)
{
    assert (xkb_str != NULL);

    // NOTE: This is a slow process, and there is a high chance of messing the
    // user's current layout. In the actual application we should have a
    // predefined library of base layouts in resolved xkb form. We can use this
    // code to generate that library.
    // TODO: Is there a way to get this from libxkbcommon? if so, that should be
    // faster, we should do that instead of calling the bash script.
    string_t cmd = str_new ("./tests/get_xkb_str.sh");
    if (rules != NULL) {
        str_cat_printf (&cmd, " -rules %s", rules);
    }

    if (model != NULL) {
        str_cat_printf (&cmd, " -model %s", model);
    }

    if (layout != NULL) {
        str_cat_printf (&cmd, " -layout %s", layout);
    }

    if (variant != NULL) {
        str_cat_printf (&cmd, " -variant %s", variant);
    }

    // TODO: setxkbmap only receives one option per -option argument, I
    // think we probably want to do the same? but our CLI parser only
    // hanldles a single -o. For now I don't care because I won't be testing
    // layouts that have options set, probably I won't even support them.
    if (options != NULL) {
        str_cat_printf (&cmd, " -option %s", options);
    }

    FILE* cmd_stdout = popen (str_data(&cmd), "r");
    if (cmd_stdout != NULL) {

        char buff[1024];
        size_t total_bytes = 0;
        size_t bytes_read = 0;
        do {
            bytes_read = fread (buff, 1, ARRAY_SIZE(buff), cmd_stdout);
            strn_cat_c (xkb_str, buff, bytes_read);
            total_bytes += bytes_read;

        } while (bytes_read == ARRAY_SIZE(buff));
        str_cat_c (xkb_str, "\0");

        if (ferror (cmd_stdout)) {
            printf ("An error occurred while reading output of script.");
        }

        assert (feof (cmd_stdout));

        int exit_status = pclose (cmd_stdout);
        if (exit_status != 0) {
            printf ("Command exited with %d.\n", exit_status%255);
        }
    }

    str_free (&cmd);

}

void xkb_str_from_file (char *fname, string_t *xkb_str)
{
    // TODO: Make a file read that writes directly to a string_t?, maybe don't
    // use a string_t for input_str?
    mem_pool_t tmp = {0};
    char *data = full_file_read (&tmp, fname, NULL);
    str_set (xkb_str, data);
    mem_pool_destroy (&tmp);
}

struct iterate_tests_dir_clsr_t {
    string_t *result;
    string_t *writer_keymap_str;
    string_t *writer_keymap_str_2;

    // Just used for layout output styling. We want to add a linebreak between
    // blocks of successful layouts and failures.
    bool prev_layout_success;
};

ITERATE_DIR_CB(iterate_tests_dir)
{
    if (!is_dir) {
        char *extension = get_extension (fname);
        if (extension && strncmp (extension, "xkb", 3) == 0) {
            string_t input_str = {0};
            struct iterate_tests_dir_clsr_t *clsr = (struct iterate_tests_dir_clsr_t*)data;
            xkb_str_from_file (fname, &input_str);

            str_set (clsr->result, "");
            str_set (clsr->writer_keymap_str, "");
            str_set (clsr->writer_keymap_str_2, "");
            bool success =
                test_xkb_file (CRASH_MODE_SAFE,
                               &input_str, clsr->result, NULL,
                               clsr->writer_keymap_str, clsr->writer_keymap_str_2);

            if (success) {
                printf_successful_layout (fname);
                clsr->prev_layout_success = true;

            } else {
                if (clsr->prev_layout_success) {
                    printf ("\n");
                }
                printf ("%s:\n", fname);
                printf_indented (str_data(clsr->result), TEST_INDENT);
                printf ("\n");
                clsr->prev_layout_success = false;
            }
            str_free (&input_str);
        }
    }
}

int main (int argc, char **argv)
{
    init_kernel_keycode_names ();
    init_xkb_keycode_names ();

    bool success = true;

    enum input_type_t input_type;

    // Data if input_type is INPUT_XKB_FILE
    char *input_file = NULL;

    // Data if input_type is INPUT_RMLVO_NAMES
    // TODO: I think a value of NULL will set things to libxkbcommon's default
    // value, is there a way we can determin whet it's using? (programatically,
    // not just reading the code). Should we have our own default values so we
    // are always sure qwhat is being used?.
    char *rules = get_cli_arg_opt ("-r", argv, argc);
    char *model = get_cli_arg_opt ("-m", argv, argc);
    char *layout = get_cli_arg_opt ("-l", argv, argc);
    char *variant = get_cli_arg_opt ("-v", argv, argc);
    char *options = get_cli_arg_opt ("-o", argv, argc);

    if (rules == NULL && model == NULL && layout == NULL &&
        variant == NULL && options == NULL) {
        char *default_argument = get_cli_no_opt_arg (argv, argc);

        if (default_argument != NULL) {
            char *extension = get_extension (default_argument);
            if (extension == NULL) {
                // TODO: Check that this is an existing layout name.
                layout = default_argument;
                input_type = INPUT_RMLVO_NAMES;

            } else if (strncmp (extension, "xkb", 3) == 0) {
                input_file = abs_path(default_argument, NULL);
                input_type = INPUT_XKB_FILE;

            } else {
                printf ("Invalid arguments.\n");
                success = false;
            }

        } else {
            input_type = INPUT_NONE;
        }

    } else {
        input_type = INPUT_RMLVO_NAMES;
    }

    bool file_output_enabled = get_cli_bool_opt ("--write-output", argv, argc);

    if (!success) {
        // TODO: Show a message here. Usage documentation?
        return 1;
    }

    string_t input_str = {0};
    string_t result = {0};
    string_t writer_keymap_str = {0};
    string_t writer_keymap_str_2 = {0};

    if (input_type == INPUT_NONE) {
        char *absolute_path = abs_path ("./tests", NULL);
        struct iterate_tests_dir_clsr_t clsr;
        clsr.result = &result;
        clsr.writer_keymap_str = &writer_keymap_str;
        clsr.writer_keymap_str_2 = &writer_keymap_str_2;
        clsr.prev_layout_success = false;
        iterate_dir (absolute_path, iterate_tests_dir, &clsr);

        free (absolute_path);
        str_free (&input_str);
        str_free (&result);

    } else {
        string_t info = {0};

        // Get an xkb string from the CLI input
        if (input_type == INPUT_RMLVO_NAMES) {
            xkb_str_from_rmlvo (rules, model, layout, variant, options, &input_str);
        } else if (input_type == INPUT_XKB_FILE) {
            xkb_str_from_file (input_file, &input_str);
        }

        enum crash_safety_mode_t crash_safety = CRASH_MODE_SAFE;
        if (get_cli_bool_opt ("--unsafe", argv, argc)) {
            crash_safety = CRASH_MODE_UNSAFE;
        }
        test_xkb_file (crash_safety, &input_str, &result, &info, &writer_keymap_str, &writer_keymap_str_2);

        if (file_output_enabled) {
            str_cat_c (&info, "\n");
            if (str_len(&input_str) != 0) {
                str_cat_c (&info, ECMA_CYAN("Wrote xkb parser input to: parser_input.xkb\n"));
                full_file_write (str_data(&input_str), str_len(&input_str), "parser_input.xkb");
            }

            if (str_len(&writer_keymap_str) != 0) {
                str_cat_c (&info, ECMA_CYAN("Wrote xkb writer output to: writer_output.xkb\n"));
                full_file_write (str_data(&writer_keymap_str), str_len(&writer_keymap_str),
                                 "writer_output.xkb");
            }

            if (str_len(&writer_keymap_str_2) != 0) {
                str_cat_c (&info, ECMA_CYAN("Wrote xkb 2nd time writer output to: writer_output_2.xkb\n"));
                full_file_write (str_data(&writer_keymap_str_2), str_len(&writer_keymap_str_2),
                                 "writer_output_2.xkb");
            }
        }

        printf ("%s", str_data(&result));
        printf ("%s", str_data(&info));
        str_free (&info);
    }

    if (input_file != NULL) free (input_file);
    str_free (&writer_keymap_str);
    str_free (&writer_keymap_str_2);
    str_free (&result);
    str_free (&input_str);

    return 0;
}
