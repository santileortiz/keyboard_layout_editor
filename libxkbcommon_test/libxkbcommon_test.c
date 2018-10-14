#include <gtk/gtk.h>
#include <xkbcommon/xkbcommon.h>

int num_mods;
const char **mod_names;
struct xkb_state *state;

void gdk_modifier_type_print (GdkModifierType mods)
{
    if (mods & GDK_SHIFT_MASK) {
        printf (" SHIFT_MASK");
    }

    if (mods & GDK_LOCK_MASK) {
        printf (" LOCK_MASK");
    }

    if (mods & GDK_CONTROL_MASK) {
        printf (" CONTROL_MASK");
    }

    if (mods & GDK_MOD1_MASK) {
        printf (" MOD1_MASK");
    }

    if (mods & GDK_MOD2_MASK) {
        printf (" MOD2_MASK");
    }

    if (mods & GDK_MOD3_MASK) {
        printf (" MOD3_MASK");
    }

    if (mods & GDK_MOD4_MASK) {
        printf (" MOD4_MASK");
    }

    if (mods & GDK_MOD5_MASK) {
        printf (" MOD5_MASK");
    }

    if (mods & GDK_BUTTON1_MASK) {
        printf (" BUTTON1_MASK");
    }

    if (mods & GDK_BUTTON2_MASK) {
        printf (" BUTTON2_MASK");
    }

    if (mods & GDK_BUTTON3_MASK) {
        printf (" BUTTON3_MASK");
    }

    if (mods & GDK_BUTTON4_MASK) {
        printf (" BUTTON4_MASK");
    }

    if (mods & GDK_BUTTON5_MASK) {
        printf (" BUTTON5_MASK");
    }

    if (mods & GDK_SUPER_MASK) {
        printf (" SUPER_MASK");
    }

    if (mods & GDK_HYPER_MASK) {
        printf (" HYPER_MASK");
    }

    if (mods & GDK_META_MASK) {
        printf (" META_MASK");
    }

    if (mods & ~GDK_MODIFIER_MASK) {
        printf (" (");
        if (mods & GDK_MODIFIER_RESERVED_13_MASK) {
            printf (" GDK_MODIFIER_RESERVED_13_MASK");
        }

        if (mods & GDK_MODIFIER_RESERVED_14_MASK) {
            printf (" GDK_MODIFIER_RESERVED_14_MASK");
        }

        if (mods & GDK_MODIFIER_RESERVED_15_MASK) {
            printf (" GDK_MODIFIER_RESERVED_15_MASK");
        }

        if (mods & GDK_MODIFIER_RESERVED_16_MASK) {
            printf (" GDK_MODIFIER_RESERVED_16_MASK");
        }

        if (mods & GDK_MODIFIER_RESERVED_17_MASK) {
            printf (" GDK_MODIFIER_RESERVED_17_MASK");
        }

        if (mods & GDK_MODIFIER_RESERVED_18_MASK) {
            printf (" GDK_MODIFIER_RESERVED_18_MASK");
        }

        if (mods & GDK_MODIFIER_RESERVED_19_MASK) {
            printf (" GDK_MODIFIER_RESERVED_19_MASK");
        }

        if (mods & GDK_MODIFIER_RESERVED_20_MASK) {
            printf (" GDK_MODIFIER_RESERVED_20_MASK");
        }

        if (mods & GDK_MODIFIER_RESERVED_21_MASK) {
            printf (" GDK_MODIFIER_RESERVED_21_MASK");
        }

        if (mods & GDK_MODIFIER_RESERVED_22_MASK) {
            printf (" GDK_MODIFIER_RESERVED_22_MASK");
        }

        if (mods & GDK_MODIFIER_RESERVED_23_MASK) {
            printf (" GDK_MODIFIER_RESERVED_23_MASK");
        }

        if (mods & GDK_MODIFIER_RESERVED_24_MASK) {
            printf (" GDK_MODIFIER_RESERVED_24_MASK");
        }

        if (mods & GDK_MODIFIER_RESERVED_25_MASK) {
            printf (" GDK_MODIFIER_RESERVED_25_MASK");
        }

        if (mods & GDK_MODIFIER_RESERVED_29_MASK) {
            printf (" GDK_MODIFIER_RESERVED_29_MASK");
        }

        if (mods & GDK_RELEASE_MASK) {
            printf (" GDK_RELEASE_MASK");
        }

        printf (" )");
    }
}

static gint key_press (GtkWidget *widget, GdkEventKey *event)
{
    //printf ("Mods: ");
    //gdk_modifier_type_print (event->state);
    //printf ("\n");
    //gdk_keymap_print_intents ();
    //if (!gtk_im_context_filter_keypress (im_context, event)) {
    //    printf ("(Ignored)\n");
    //}

    GdkDisplay *display = gdk_window_get_display (event->window);
    GdkModifierType no_text_input_mask =
        gdk_keymap_get_modifier_mask (gdk_keymap_get_for_display (display),
                                      GDK_MODIFIER_INTENT_NO_TEXT_INPUT);
    gdk_modifier_type_print (no_text_input_mask);
    printf ("\n");

    xkb_keycode_t keycode = event->hardware_keycode;
    enum xkb_state_component changed;
    printf ("Type: ");
    if (event->type == GDK_KEY_PRESS) {
        printf ("KEY_PRESS\n");
        changed = xkb_state_update_key(state, keycode, XKB_KEY_DOWN);
    } else if (event->type == GDK_KEY_RELEASE) {
        printf ("KEY_RELEASE\n");
        changed = xkb_state_update_key(state, keycode, XKB_KEY_UP);
    } else
        printf ("Invalid event.");

    xkb_keysym_t keysym = xkb_state_key_get_one_sym(state, event->hardware_keycode);

    char keysym_name[64];
    xkb_keysym_get_name(keysym, keysym_name, sizeof(keysym_name));

    printf ("keysym: %s\n", keysym_name);

    int size = xkb_state_key_get_utf8(state, keycode, NULL, 0) + 1;
    if (size > 1) {
        char *buffer = malloc (size);
        xkb_state_key_get_utf8(state, keycode, buffer, size);
        printf ("UTF-8: %s\n", buffer);
    }

    printf ("Effective Mods: ");
    for (int i=0; i<num_mods; i++) {
        if (xkb_state_mod_index_is_active(state, i, XKB_STATE_MODS_EFFECTIVE)) {
            printf ("%s ", mod_names[i]);
        }
    }
    printf ("\n");

    printf ("Consumed Mods (XKB): ");
    for (int i=0; i<num_mods; i++) {
        if (xkb_state_mod_index_is_consumed2 (state, keycode, i, XKB_CONSUMED_MODE_XKB)) {
            printf ("%s ", mod_names[i]);
        }
    }
    printf ("\n");

    printf ("Consumed Mods (GTK): ");
    for (int i=0; i<num_mods; i++) {
        if (xkb_state_mod_index_is_consumed2 (state, keycode, i, XKB_CONSUMED_MODE_GTK)) {
            printf ("%s ", mod_names[i]);
        }
    }
    printf ("\n");

    printf ("\n");

    return FALSE;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf ("Specify a layout name.\n");
    }

    GtkWidget *window;
    GtkWidget *entry;

    gtk_init(&argc, &argv);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_window_set_title(GTK_WINDOW(window), "GTK window");

    entry = gtk_entry_new();
    gtk_container_add(GTK_CONTAINER(window), entry);

    g_signal_connect (window, "key-press-event",
                      G_CALLBACK (key_press), NULL);
    g_signal_connect (window, "key-release-event",
                      G_CALLBACK (key_press), NULL);
    g_signal_connect(window, "destroy",
                     G_CALLBACK(gtk_main_quit), NULL);

    gtk_widget_show_all(window);

    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!ctx) {
        printf ("Could not create xkb context.");
    }

    struct xkb_rule_names names = {
        .rules = NULL,
        .model = "pc105",
        .layout = argv[1],
        .variant = NULL,
        .options = NULL
    };
    struct xkb_keymap *keymap = xkb_keymap_new_from_names(ctx, &names,
                                                          XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap) {
        printf ("Could not create keymap.");
    }

    num_mods = xkb_keymap_num_mods (keymap);
    mod_names = malloc (sizeof(char *)*num_mods);
    printf ("Num Mods: %d\n", num_mods);
    for (int i=0; i<num_mods; i++) {
        mod_names[i] = xkb_keymap_mod_get_name(keymap, i);
        printf ("%s ", mod_names[i]);
    }
    printf ("\n");

    state = xkb_state_new(keymap);
    if (!state) {
        printf ("Could not create xkb state.");
    }

    gtk_main();

    return 0;
}

