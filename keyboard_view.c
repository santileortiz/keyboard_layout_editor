/*
 * Copiright (C) 2018 Santiago León O.
 */

enum keyboard_view_mode_t {
    KEYBOARD_VIEW_PREVIEW,
    KEYBOARD_VIEW_EDIT
};

enum keyboard_view_edit_mode_t {
    KEYBOARD_VIEW_EDIT_DEFAULT,
    KEYBOARD_VIEW_EDIT_KEYCODE_KEYPRESS,
    //KEYBOARD_VIEW_EDIT_KEYCODE_LOOKUP,
    //KEYBOARD_VIEW_EDIT_KEY_WIDTH,
    //KEYBOARD_VIEW_EDIT_KEY_POSITION
};

struct key_t {
    int kc; //keycode
    float width; // normalized to default_key_size
    bool is_pressed;
    struct key_t *next_key;
};

struct row_t {
    float height; // normalized to default_key_size
    struct row_t *next_row;

    struct key_t *first_key;
    struct key_t *last_key;
};

struct keyboard_view_t {
    mem_pool_t *pool;

    // Array of key_t pointers indexed by keycode. Provides fast access to keys
    // from a keycode. It's about 6KB in memory, maybe too much?
    struct key_t *keys_by_kc[KEY_MAX];

    // For now we represent the geometry of the keyboard by having a linked list
    // of rows, each of which is a linked list of keys.
    struct row_t *first_row;
    struct row_t *last_row;

    // xkbcommon state
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;

    // GUI related information and state
    GtkWidget *widget;
    float default_key_size; // In pixels
    int clicked_kc;
    struct key_t *selected_key;
    enum keyboard_view_mode_t keyboard_view_mode;
    enum keyboard_view_edit_mode_t edit_mode;
};

#define kv_new_row(pool,kbd) kv_new_row_h(pool,kbd,1)
void kv_new_row_h (mem_pool_t *pool, struct keyboard_view_t *kv, float height)
{
    struct row_t *new_row = (struct row_t*)pom_push_struct (pool, struct row_t);
    *new_row = (struct row_t){0};
    new_row->height = height;

    if (kv->last_row != NULL) {
        kv->last_row->next_row = new_row;
        kv->last_row = new_row;

    } else {
        kv->first_row = new_row;
        kv->last_row = new_row;
    }
}

#define kv_add_key(pool,kbd,keycode) kv_add_key_w(pool,kbd,keycode,1)
void kv_add_key_w (mem_pool_t *pool, struct keyboard_view_t *kv, int keycode, float width)
{
    struct key_t *new_key = (struct key_t*)pom_push_struct (pool, struct key_t);
    kv->keys_by_kc[keycode] = new_key;
    *new_key = (struct key_t){0};
    new_key->width = width;
    new_key->kc = keycode;

    struct row_t *curr_row = kv->last_row;
    assert (curr_row != NULL && "Must create a row before adding a key.");

    if (curr_row->last_key != NULL) {
        curr_row->last_key->next_key = new_key;
        curr_row->last_key = new_key;

    } else {
        curr_row->first_key = new_key;
        curr_row->last_key = new_key;
    }
}

void kv_get_size (struct keyboard_view_t *kv, double *width, double *height)
{
    struct row_t *curr_row = kv->first_row;

    double w = 0;
    double h = 0;
    while (curr_row != NULL) {
        h += curr_row->height*kv->default_key_size;

        double row_w = 0;
        struct key_t *curr_key = curr_row->first_key;
        while (curr_key != NULL) {
            row_w += curr_key->width*kv->default_key_size;
            curr_key = curr_key->next_key;
        }

        if (row_w > w) {
            w = row_w;
        }

        curr_row = curr_row->next_row;
    }

    if (width != NULL) {
        *width = w;
    }

    if (height != NULL) {
        *height = h;
    }
}

// Simple default keyboard geometry.
// NOTE: Keycodes are used as defined in the linux kernel. To translate them
// into X11 keycodes offset them by 8 (x11_kc = kc+8).
void keyboard_view_build_default_geometry (mem_pool_t *pool, struct keyboard_view_t *kv)
{
    kv->default_key_size = 56; // Should be divisible by 4 so everything is pixel perfect
    kv_new_row (pool, kv);
    kv_add_key (pool, kv, KEY_ESC);
    kv_add_key (pool, kv, KEY_F1);
    kv_add_key (pool, kv, KEY_F2);
    kv_add_key (pool, kv, KEY_F3);
    kv_add_key (pool, kv, KEY_F4);
    kv_add_key (pool, kv, KEY_F5);
    kv_add_key (pool, kv, KEY_F6);
    kv_add_key (pool, kv, KEY_F7);
    kv_add_key (pool, kv, KEY_F8);
    kv_add_key (pool, kv, KEY_F9);
    kv_add_key (pool, kv, KEY_F10);
    kv_add_key (pool, kv, KEY_F11);
    kv_add_key (pool, kv, KEY_F12);
    kv_add_key (pool, kv, KEY_NUMLOCK);
    kv_add_key (pool, kv, KEY_SCROLLLOCK);
    kv_add_key (pool, kv, KEY_INSERT);

    kv_new_row (pool, kv);
    kv_add_key (pool, kv, KEY_GRAVE);
    kv_add_key (pool, kv, KEY_1);
    kv_add_key (pool, kv, KEY_2);
    kv_add_key (pool, kv, KEY_3);
    kv_add_key (pool, kv, KEY_4);
    kv_add_key (pool, kv, KEY_5);
    kv_add_key (pool, kv, KEY_6);
    kv_add_key (pool, kv, KEY_7);
    kv_add_key (pool, kv, KEY_8);
    kv_add_key (pool, kv, KEY_9);
    kv_add_key (pool, kv, KEY_0);
    kv_add_key (pool, kv, KEY_MINUS);
    kv_add_key (pool, kv, KEY_EQUAL);
    kv_add_key_w (pool, kv, KEY_BACKSPACE, 2);
    kv_add_key (pool, kv, KEY_HOME);

    kv_new_row (pool, kv);
    kv_add_key_w (pool, kv, KEY_TAB, 1.5);
    kv_add_key (pool, kv, KEY_Q);
    kv_add_key (pool, kv, KEY_W);
    kv_add_key (pool, kv, KEY_E);
    kv_add_key (pool, kv, KEY_R);
    kv_add_key (pool, kv, KEY_T);
    kv_add_key (pool, kv, KEY_Y);
    kv_add_key (pool, kv, KEY_U);
    kv_add_key (pool, kv, KEY_I);
    kv_add_key (pool, kv, KEY_O);
    kv_add_key (pool, kv, KEY_P);
    kv_add_key (pool, kv, KEY_LEFTBRACE);
    kv_add_key (pool, kv, KEY_RIGHTBRACE);
    kv_add_key_w (pool, kv, KEY_BACKSLASH, 1.5);
    kv_add_key (pool, kv, KEY_PAGEUP);

    kv_new_row (pool, kv);
    kv_add_key_w (pool, kv, KEY_CAPSLOCK, 1.75);
    kv_add_key (pool, kv, KEY_A);
    kv_add_key (pool, kv, KEY_S);
    kv_add_key (pool, kv, KEY_D);
    kv_add_key (pool, kv, KEY_F);
    kv_add_key (pool, kv, KEY_G);
    kv_add_key (pool, kv, KEY_H);
    kv_add_key (pool, kv, KEY_J);
    kv_add_key (pool, kv, KEY_K);
    kv_add_key (pool, kv, KEY_L);
    kv_add_key (pool, kv, KEY_SEMICOLON);
    kv_add_key (pool, kv, KEY_APOSTROPHE);
    kv_add_key_w (pool, kv, KEY_ENTER, 2.25);
    kv_add_key (pool, kv, KEY_PAGEDOWN);

    kv_new_row (pool, kv);
    kv_add_key_w (pool, kv, KEY_LEFTSHIFT, 2.25);
    kv_add_key (pool, kv, KEY_Z);
    kv_add_key (pool, kv, KEY_X);
    kv_add_key (pool, kv, KEY_C);
    kv_add_key (pool, kv, KEY_V);
    kv_add_key (pool, kv, KEY_B);
    kv_add_key (pool, kv, KEY_N);
    kv_add_key (pool, kv, KEY_M);
    kv_add_key (pool, kv, KEY_COMMA);
    kv_add_key (pool, kv, KEY_DOT);
    kv_add_key (pool, kv, KEY_SLASH);
    kv_add_key_w (pool, kv, KEY_RIGHTSHIFT, 1.75);
    kv_add_key (pool, kv, KEY_UP);
    kv_add_key (pool, kv, KEY_END);

    kv_new_row (pool, kv);
    kv_add_key_w (pool, kv, KEY_LEFTCTRL, 1.5);
    kv_add_key_w (pool, kv, KEY_LEFTMETA, 1.5);
    kv_add_key_w (pool, kv, KEY_LEFTALT, 1.5);
    kv_add_key_w (pool, kv, KEY_SPACE, 5.5);
    kv_add_key_w (pool, kv, KEY_RIGHTALT, 1.5);
    kv_add_key_w (pool, kv, KEY_RIGHTCTRL, 1.5);
    kv_add_key (pool, kv, KEY_LEFT);
    kv_add_key (pool, kv, KEY_DOWN);
    kv_add_key (pool, kv, KEY_RIGHT);
}

void keyboard_view_queue_draw (struct keyboard_view_t *kv)
{
    gtk_widget_queue_draw (kv->widget);
}

void cr_render_key_label (cairo_t *cr, const char *label, double x, double y, double width, double height)
{
    PangoLayout *text_layout;
    {
        PangoFontDescription *font_desc = pango_font_description_new ();
        pango_font_description_set_family (font_desc, "Open Sans");
        pango_font_description_set_size (font_desc, 13*PANGO_SCALE);
        pango_font_description_set_weight (font_desc, PANGO_WEIGHT_NORMAL);

        text_layout = pango_cairo_create_layout (cr);
        pango_layout_set_font_description (text_layout, font_desc);
        pango_font_description_free(font_desc);
    }

    pango_layout_set_text (text_layout, label, strlen(label));

    // Reduce font size untill the lable fits inside the key.
    // NOTE: This should be just a fallback for special cases, most keys should
    // have a lable that fits inside them.
    PangoRectangle logical;
    int font_size = 13;
    pango_layout_get_pixel_extents (text_layout, NULL, &logical);
    while ((logical.width + 4 >= width || logical.height >= height) && font_size > 0) {
        font_size--;
        PangoFontDescription *font_desc =
            pango_font_description_copy (pango_layout_get_font_description (text_layout));
        pango_font_description_set_size (font_desc, font_size*PANGO_SCALE);
        pango_layout_set_font_description (text_layout, font_desc);
        pango_layout_get_pixel_extents (text_layout, NULL, &logical);
        pango_font_description_free(font_desc);
    }

    if (logical.width < width && logical.height < height) {
        double text_x_pos = x + (width - logical.width)/2;
        double text_y_pos = y + (height - logical.height)/2;

        cairo_set_source_rgb (cr, 0,0,0);
        cairo_move_to (cr, text_x_pos, text_y_pos);
        pango_cairo_show_layout (cr, text_layout);

    } else {
        // We don't want to resize keys so we instead should make sure that this
        // never happens.
        printf ("Skipping rendering for label: %s\n", label);
    }
    g_object_unref (text_layout);
}

void cr_render_key (cairo_t *cr, double x, double y, double width, double height,
                    const char *label, dvec4 color)
{
    float margin = 5;
    float top_margin = 2;
    cr_rounded_box (cr, x+0.5, y+0.5,
                    width-1,
                    height-1,
                    5);
    cairo_set_source_rgb (cr, ARGS_RGB(color));
    cairo_fill_preserve (cr);

    cairo_set_source_rgba (cr, 0, 0, 0, 0.05);
    cairo_fill_preserve (cr);

    cairo_set_source_rgb (cr, 0, 0, 0);
    cairo_stroke (cr);

    float cap_x = x+margin+0.5;
    float cap_y = y+top_margin+0.5;
    float cap_w = width-2*margin-1;
    float cap_h = height-2*margin-1;
    cr_rounded_box (cr, cap_x, cap_y,
                    cap_w, cap_h,
                    5);
    cairo_set_source_rgb (cr, ARGS_RGB(color));
    cairo_fill_preserve (cr);

    cairo_set_source_rgba (cr, 0, 0, 0, 0.1);
    cairo_stroke (cr);

    cr_render_key_label (cr, label, cap_x, cap_y, cap_w, cap_h);
}

void keyboard_view_get_margins (struct keyboard_view_t *kv, double *left_margin, double *top_margin)
{
    double kbd_width, kbd_height;
    kv_get_size (kv, &kbd_width, &kbd_height);

    if (left_margin != NULL) {
        *left_margin = 0;
        int canvas_w = gtk_widget_get_allocated_width (kv->widget);
        if (kbd_width < canvas_w) {
            *left_margin = floor((canvas_w - kbd_width)/2);
        }
    }

    if (top_margin != NULL) {
        *top_margin = 0;
        int canvas_h = gtk_widget_get_allocated_height (kv->widget);
        if (kbd_height < canvas_h) {
            *top_margin = floor((canvas_h - kbd_height)/2);
        }
    }
}

char *keysym_representations[][2] = {
      {"Alt_L", "Alt"},
      {"Alt_R", "AltGr"},
      {"ISO_Level3_Shift", "AltGr"},
      {"Control_L", "Ctrl"},
      {"Control_R", "Ctrl"},
      {"Shift_L", "Shift"},
      {"Shift_R", "Shift"},
      {"Caps_Lock", "CapsLock"},
      {"Super_L", "⌘ "},
      {"Super_R", "⌘ "},
      {"Prior", "Page\nUp"},
      {"Next", "Page\nDown"},
      {"Num_Lock", "Num\nLock"},
      {"Scroll_Lock", "Scroll\nLock"},
      {"Escape", "Esc"},
      {"Up", "↑"},
      {"Down", "↓"},
      {"Right", "→"},
      {"Left", "←"},
      {"Return", "↵ "}
      };

gboolean keyboard_view_render (GtkWidget *widget, cairo_t *cr, gpointer data)
{
    struct keyboard_view_t *kv = keyboard_view;
    cairo_set_source_rgba (cr, 1, 1, 1, 1);
    cairo_paint(cr);

    cairo_set_line_width (cr, 1);

    mem_pool_t pool = {0};
    double left_margin, top_margin;
    keyboard_view_get_margins (keyboard_view, &left_margin, &top_margin);

    double y_pos = top_margin;
    struct row_t *curr_row = kv->first_row;
    while (curr_row != NULL) {
        struct key_t *curr_key = curr_row->first_key;
        double x_pos = left_margin;
        double key_height = curr_row->height*kv->default_key_size;
        while (curr_key != NULL) {
            double key_width = curr_key->width*kv->default_key_size;

            // Compute the label for the key
            char buff[64];
            buff[0] = '\0';
            if (keyboard_view->keyboard_view_mode == KEYBOARD_VIEW_PREVIEW) {
                if (curr_key->kc == KEY_FN) {
                    strcpy (buff, "Fn");
                }

                xkb_keysym_t keysym;
                if (buff[0] == '\0') {
                    int buff_len = 0;
                    keysym = xkb_state_key_get_one_sym(keyboard_view->xkb_state, curr_key->kc + 8);
                    buff_len = xkb_keysym_to_utf8(keysym, buff, sizeof(buff) - 1);
                    buff[buff_len] = '\0';
                }

                if (buff[0] == '\0' || // Keysym is non printable
                    buff[0] == ' ' ||
                    buff[0] == '\x1b' || // Escape
                    buff[0] == '\n' ||
                    buff[0] == '\r' ||
                    buff[0] == '\b' ||
                    buff[0] == '\t' )
                {
                    xkb_keysym_get_name(keysym, buff, ARRAY_SIZE(buff)-1);
                    if (strcmp (buff, "NoSymbol") == 0) {
                        buff[0] = '\0';
                    }

                    int i;
                    for (i=0; i < ARRAY_SIZE(keysym_representations); i++) {
                        if (strcmp (buff, keysym_representations[i][0]) == 0) {
                            strcpy (buff, keysym_representations[i][1]);
                            break;
                        }
                    }
                }
            } else {
                snprintf (buff, ARRAY_SIZE(buff), "%i", curr_key->kc);
            }

            // Draw the key with the appropiate label/color
            if (keyboard_view->selected_key != NULL && curr_key->kc == keyboard_view->selected_key->kc) {
                cr_render_key (cr, x_pos, y_pos, key_width, key_height, "", RGB_HEX(0xe34442));

            } else if (curr_key->is_pressed || curr_key->kc == keyboard_view->clicked_kc) {
                cr_render_key (cr, x_pos, y_pos, key_width, key_height, buff, RGB_HEX(0x90de4d));

            } else {
                cr_render_key (cr, x_pos, y_pos, key_width, key_height, buff, RGB(1,1,1));
            }

            x_pos += key_width;
            curr_key = curr_key->next_key;
        }

        y_pos += key_height;
        curr_row = curr_row->next_row;
    }

    mem_pool_destroy (&pool);
    return FALSE;
}

gboolean key_press_handler (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    struct keyboard_view_t *kv = keyboard_view;
    uint16_t kc = ((GdkEventKey*)event)->hardware_keycode;
    struct key_t *key = kv->keys_by_kc[kc-8];
    switch (keyboard_view->keyboard_view_mode) {
        case KEYBOARD_VIEW_PREVIEW:
            if (key != NULL) {
                key->is_pressed = true;
            }
            xkb_state_update_key(keyboard_view->xkb_state, kc, XKB_KEY_DOWN);
            break;
        case KEYBOARD_VIEW_EDIT:
            if (keyboard_view->edit_mode == KEYBOARD_VIEW_EDIT_KEYCODE_KEYPRESS) {
                if (key == NULL) {
                    // NOTE: We only update the keycode if it wasn't assigned
                    // before.
                    kv->keys_by_kc[keyboard_view->selected_key->kc] = NULL;
                    keyboard_view->selected_key->kc = kc-8;
                    kv->keys_by_kc[kc-8] = keyboard_view->selected_key;
                }

                keyboard_view->selected_key = NULL;
                keyboard_view->edit_mode = KEYBOARD_VIEW_EDIT_DEFAULT;
                ungrab_input (NULL, NULL);
            }

            if (key != NULL) {
                key->is_pressed = true;
            }
            break;
        default:
            invalid_code_path;
    }

    keyboard_view_queue_draw (keyboard_view);
    return TRUE;
}

gboolean key_release_handler (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    struct keyboard_view_t *kv = keyboard_view;
    uint16_t kc = ((GdkEventKey*)event)->hardware_keycode;
    struct key_t *key = kv->keys_by_kc[kc-8];
    if (key != NULL) {
        key->is_pressed = false;
    }
    xkb_state_update_key(keyboard_view->xkb_state, kc, XKB_KEY_UP);
    keyboard_view_queue_draw (keyboard_view);
    return TRUE;
}

struct key_t* keyboard_view_get_key (struct keyboard_view_t *kv, double x, double y, GdkRectangle *rect)
{
    double kbd_x, kbd_y;
    keyboard_view_get_margins (kv, &kbd_x, &kbd_y);

    struct row_t *curr_row = kv->first_row;
    while (curr_row != NULL) {
        kbd_y += curr_row->height*kv->default_key_size;
        if (kbd_y > y) {
            break;
        }

        curr_row = curr_row->next_row;
    }

    struct key_t *curr_key = NULL;
    if (curr_row != NULL) {
        curr_key = curr_row->first_key;
        while (curr_key != NULL) {
            kbd_x += curr_key->width*kv->default_key_size;
            if (kbd_x > x) {
                break;
            }

            curr_key = curr_key->next_key;
        }
    }

    if (curr_key != NULL && rect != NULL) {
        rect->height = curr_row->height*kv->default_key_size;
        rect->y = kbd_y - rect->height;

        rect->width = curr_key->width*kv->default_key_size;
        rect->x = kbd_x - rect->width;
    }

    return curr_key;
}

gboolean keyboard_view_button_press (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    GdkEventButton *button_event = (GdkEventButton*)event;
    if (button_event->type == GDK_2BUTTON_PRESS || button_event->type == GDK_3BUTTON_PRESS) {
        // Ignore double and triple clicks.
        return FALSE;
    }

    struct key_t *key = keyboard_view_get_key (keyboard_view, button_event->x, button_event->y, NULL);
    if (key != NULL) {
        switch (keyboard_view->keyboard_view_mode) {
            case KEYBOARD_VIEW_PREVIEW:
                keyboard_view->clicked_kc = key->kc;
                xkb_state_update_key(keyboard_view->xkb_state, key->kc+8, XKB_KEY_DOWN);
                break;
            case KEYBOARD_VIEW_EDIT:
                break;
            default:
                invalid_code_path;
        }
        keyboard_view_queue_draw (keyboard_view);
    }
    return TRUE;
}

gboolean keyboard_view_button_release (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    switch (keyboard_view->keyboard_view_mode) {
        case KEYBOARD_VIEW_PREVIEW:
            xkb_state_update_key(keyboard_view->xkb_state, keyboard_view->clicked_kc+8, XKB_KEY_UP);
            keyboard_view->clicked_kc = 0;
            break;
        case KEYBOARD_VIEW_EDIT:
            {
                // NOTE: We handle this on release because we are taking a grab
                // of all input. Doing so on a key press breaks GTK's grab
                // created before sending the event, which may cause trouble.
                GdkEventButton *button_event = (GdkEventButton*)event;
                struct key_t *key = keyboard_view_get_key (keyboard_view, button_event->x, button_event->y, NULL);
                if (keyboard_view->edit_mode == KEYBOARD_VIEW_EDIT_DEFAULT) {
                    keyboard_view->selected_key = key;
                    grab_input (NULL, NULL);
                    keyboard_view->edit_mode = KEYBOARD_VIEW_EDIT_KEYCODE_KEYPRESS;

                } else if (keyboard_view->edit_mode == KEYBOARD_VIEW_EDIT_KEYCODE_KEYPRESS) {
                    if (key == keyboard_view->selected_key) {
                        ungrab_input (NULL, NULL);
                        keyboard_view->selected_key = NULL;
                        keyboard_view->edit_mode = KEYBOARD_VIEW_EDIT_DEFAULT;
                    } else {
                        // We will now edit the newly clicked key.
                        keyboard_view->selected_key = key;
                    }
                }
            } break;
        default:
            invalid_code_path;
    }
    keyboard_view_queue_draw (keyboard_view);
    return TRUE;
}

gboolean keyboard_view_tooltip_handler (GtkWidget *widget, gint x, gint y,
                                        gboolean keyboard_mode, GtkTooltip *tooltip,
                                        gpointer user_data)
{
    if (keyboard_mode) {
        return FALSE;
    }

    GdkRectangle rect = {0};
    struct key_t *key = keyboard_view_get_key (keyboard_view, x, y, &rect);
    if (key != NULL) {
        gtk_tooltip_set_text (tooltip, keycode_names[key->kc]);

        gtk_tooltip_set_tip_area (tooltip, &rect);
        return TRUE;
    } else {
        return FALSE;
    }
}

void keyboard_view_set_keymap (struct keyboard_view_t *kv, const char *keymap_name)
{
    if (kv->xkb_keymap != NULL) {
        xkb_keymap_unref(kv->xkb_keymap);
    }

    if (kv->xkb_state != NULL) {
        xkb_state_unref(kv->xkb_state);
    }

    mem_pool_t pool = {0};
    char *keymap_str = reconstruct_installed_custom_layout (&pool, keymap_name);

    struct xkb_context *xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!xkb_ctx) {
        printf ("Error creating xkb_context.\n");
    }

    kv->xkb_keymap = xkb_keymap_new_from_string (xkb_ctx, keymap_str,
                                                 XKB_KEYMAP_FORMAT_TEXT_V1,
                                                 XKB_KEYMAP_COMPILE_NO_FLAGS);
    xkb_context_unref (xkb_ctx);

    if (!kv->xkb_keymap) {
        printf ("Error creating xkb_keymap.\n");
    }

    kv->xkb_state = xkb_state_new(kv->xkb_keymap);
    if (!kv->xkb_state) {
        printf ("Error creating xkb_state.\n");
    }

    keyboard_view_queue_draw (kv);

    mem_pool_destroy (&pool);
}

// I'm experimenting with some ownership patterns here. The idea is that when
// creating a new keyboard_view_t the caller can pass a mem_pool_t, in which
// case everything will be freed when that pool is freed. If instead NULL is
// passed, then we bootstrap a mem_pool_t and store everything there, in this
// case keyboard_view_destroy() must be called when it is no longer needed.
struct keyboard_view_t* keyboard_view_new (mem_pool_t *pool)
{
    GtkWidget *kv_widget = gtk_drawing_area_new ();
    gtk_widget_add_events (kv_widget, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    gtk_widget_set_vexpand (kv_widget, TRUE);
    gtk_widget_set_hexpand (kv_widget, TRUE);
    g_signal_connect (G_OBJECT (kv_widget), "draw", G_CALLBACK (keyboard_view_render), NULL);
    g_signal_connect (G_OBJECT (kv_widget), "button-press-event", G_CALLBACK (keyboard_view_button_press), NULL);
    g_signal_connect (G_OBJECT (kv_widget), "button-release-event", G_CALLBACK (keyboard_view_button_release), NULL);
    g_object_set_property_bool (G_OBJECT (kv_widget), "has-tooltip", true);
    g_signal_connect (G_OBJECT (kv_widget), "query-tooltip", G_CALLBACK (keyboard_view_tooltip_handler), NULL);
    gtk_widget_show (kv_widget);

    if (pool == NULL) {
        mem_pool_t bootstrap_pool = {0};
        pool = mem_pool_push_size (&bootstrap_pool, sizeof(mem_pool_t));
        *pool = bootstrap_pool;
    }

    struct keyboard_view_t *res = mem_pool_push_size (pool, sizeof(struct keyboard_view_t));
    *res = ZERO_INIT(struct keyboard_view_t);
    keyboard_view_build_default_geometry (pool, res);
    res->pool = pool;
    res->widget = kv_widget;
    res->keyboard_view_mode = KEYBOARD_VIEW_EDIT;

    return res;
}

void keyboard_view_destroy (struct keyboard_view_t *kv)
{
    mem_pool_destroy (kv->pool);
}

