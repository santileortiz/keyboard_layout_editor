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

struct xkb_context *xkb_ctx = NULL;
struct xkb_keymap *xkb_keymap = NULL;
struct xkb_state *xkb_state = NULL;

// For now kbd is the only thing stored inside kbd_pool, if there are things
// with the same lifetime, then we should rename this and store them here too.
// If it turns out kbd has a unique lifetime then better make kbd_pool a member
// of keyboard_t and create a kbd_free function.
mem_pool_t kbd_pool = {0};
struct keyboard_t *kbd = NULL;
int clicked_kc = 0;
struct key_t *selected_key = NULL;
enum keyboard_view_mode_t keyboard_view_mode = KEYBOARD_VIEW_EDIT;
enum keyboard_view_edit_mode_t edit_mode = KEYBOARD_VIEW_EDIT_DEFAULT;

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

struct keyboard_t {
    float default_key_size; // In pixels
    // Array of key_t pointers indexed by keycode. Provides fast access to keys
    // from a keycode. It's about 6KB in memory, maybe too much?
    struct key_t *keys_by_kc[KEY_MAX];
    struct row_t *first_row;
    struct row_t *last_row;
};

#define kbd_new_row(pool,kbd) kbd_new_row_h(pool,kbd,1)
void kbd_new_row_h (mem_pool_t *pool, struct keyboard_t *kbd, float height)
{
    struct row_t *new_row = (struct row_t*)pom_push_struct (pool, struct row_t);
    *new_row = (struct row_t){0};
    new_row->height = height;

    if (kbd->last_row != NULL) {
        kbd->last_row->next_row = new_row;
        kbd->last_row = new_row;

    } else {
        kbd->first_row = new_row;
        kbd->last_row = new_row;
    }
}

#define kbd_add_key(pool,kbd,keycode) kbd_add_key_w(pool,kbd,keycode,1)
void kbd_add_key_w (mem_pool_t *pool, struct keyboard_t *kbd, int keycode, float width)
{
    struct key_t *new_key = (struct key_t*)pom_push_struct (pool, struct key_t);
    kbd->keys_by_kc[keycode] = new_key;
    *new_key = (struct key_t){0};
    new_key->width = width;
    new_key->kc = keycode;

    struct row_t *curr_row = kbd->last_row;
    assert (curr_row != NULL && "Must create a row before adding a key.");

    if (curr_row->last_key != NULL) {
        curr_row->last_key->next_key = new_key;
        curr_row->last_key = new_key;

    } else {
        curr_row->first_key = new_key;
        curr_row->last_key = new_key;
    }
}

void kbd_get_size (struct keyboard_t *kbd, double *width, double *height)
{
    struct row_t *curr_row = kbd->first_row;

    double w = 0;
    double h = 0;
    while (curr_row != NULL) {
        h += curr_row->height*kbd->default_key_size;

        double row_w = 0;
        struct key_t *curr_key = curr_row->first_key;
        while (curr_key != NULL) {
            row_w += curr_key->width*kbd->default_key_size;
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

// Simple keyboard geometry.
// NOTE: Keycodes are used as defined in the linux kernel. To translate them
// into X11 keycodes offset them by 8 (x11_kc = kc+8).
struct keyboard_t* build_keyboard (mem_pool_t *pool)
{
    struct keyboard_t *result = (struct keyboard_t*)pom_push_struct (pool, struct keyboard_t);
    *result = (struct keyboard_t){0};
    result->default_key_size = 56; // Should be divisible by 4 so everything is pixel perfect
    kbd_new_row (pool, result);
    kbd_add_key (pool, result, KEY_ESC);
    kbd_add_key (pool, result, KEY_F1);
    kbd_add_key (pool, result, KEY_F2);
    kbd_add_key (pool, result, KEY_F3);
    kbd_add_key (pool, result, KEY_F4);
    kbd_add_key (pool, result, KEY_F5);
    kbd_add_key (pool, result, KEY_F6);
    kbd_add_key (pool, result, KEY_F7);
    kbd_add_key (pool, result, KEY_F8);
    kbd_add_key (pool, result, KEY_F9);
    kbd_add_key (pool, result, KEY_F10);
    kbd_add_key (pool, result, KEY_F11);
    kbd_add_key (pool, result, KEY_F12);
    kbd_add_key (pool, result, KEY_NUMLOCK);
    kbd_add_key (pool, result, KEY_SCROLLLOCK);
    kbd_add_key (pool, result, KEY_INSERT);

    kbd_new_row (pool, result);
    kbd_add_key (pool, result, KEY_GRAVE);
    kbd_add_key (pool, result, KEY_1);
    kbd_add_key (pool, result, KEY_2);
    kbd_add_key (pool, result, KEY_3);
    kbd_add_key (pool, result, KEY_4);
    kbd_add_key (pool, result, KEY_5);
    kbd_add_key (pool, result, KEY_6);
    kbd_add_key (pool, result, KEY_7);
    kbd_add_key (pool, result, KEY_8);
    kbd_add_key (pool, result, KEY_9);
    kbd_add_key (pool, result, KEY_0);
    kbd_add_key (pool, result, KEY_MINUS);
    kbd_add_key (pool, result, KEY_EQUAL);
    kbd_add_key_w (pool, result, KEY_BACKSPACE, 2);
    kbd_add_key (pool, result, KEY_HOME);

    kbd_new_row (pool, result);
    kbd_add_key_w (pool, result, KEY_TAB, 1.5);
    kbd_add_key (pool, result, KEY_Q);
    kbd_add_key (pool, result, KEY_W);
    kbd_add_key (pool, result, KEY_E);
    kbd_add_key (pool, result, KEY_R);
    kbd_add_key (pool, result, KEY_T);
    kbd_add_key (pool, result, KEY_Y);
    kbd_add_key (pool, result, KEY_U);
    kbd_add_key (pool, result, KEY_I);
    kbd_add_key (pool, result, KEY_O);
    kbd_add_key (pool, result, KEY_P);
    kbd_add_key (pool, result, KEY_LEFTBRACE);
    kbd_add_key (pool, result, KEY_RIGHTBRACE);
    kbd_add_key_w (pool, result, KEY_BACKSLASH, 1.5);
    kbd_add_key (pool, result, KEY_PAGEUP);

    kbd_new_row (pool, result);
    kbd_add_key_w (pool, result, KEY_CAPSLOCK, 1.75);
    kbd_add_key (pool, result, KEY_A);
    kbd_add_key (pool, result, KEY_S);
    kbd_add_key (pool, result, KEY_D);
    kbd_add_key (pool, result, KEY_F);
    kbd_add_key (pool, result, KEY_G);
    kbd_add_key (pool, result, KEY_H);
    kbd_add_key (pool, result, KEY_J);
    kbd_add_key (pool, result, KEY_K);
    kbd_add_key (pool, result, KEY_L);
    kbd_add_key (pool, result, KEY_SEMICOLON);
    kbd_add_key (pool, result, KEY_APOSTROPHE);
    kbd_add_key_w (pool, result, KEY_ENTER, 2.25);
    kbd_add_key (pool, result, KEY_PAGEDOWN);

    kbd_new_row (pool, result);
    kbd_add_key_w (pool, result, KEY_LEFTSHIFT, 2.25);
    kbd_add_key (pool, result, KEY_Z);
    kbd_add_key (pool, result, KEY_X);
    kbd_add_key (pool, result, KEY_C);
    kbd_add_key (pool, result, KEY_V);
    kbd_add_key (pool, result, KEY_B);
    kbd_add_key (pool, result, KEY_N);
    kbd_add_key (pool, result, KEY_M);
    kbd_add_key (pool, result, KEY_COMMA);
    kbd_add_key (pool, result, KEY_DOT);
    kbd_add_key (pool, result, KEY_SLASH);
    kbd_add_key_w (pool, result, KEY_RIGHTSHIFT, 1.75);
    kbd_add_key (pool, result, KEY_UP);
    kbd_add_key (pool, result, KEY_END);

    kbd_new_row (pool, result);
    kbd_add_key_w (pool, result, KEY_LEFTCTRL, 1.5);
    kbd_add_key_w (pool, result, KEY_LEFTMETA, 1.5);
    kbd_add_key_w (pool, result, KEY_LEFTALT, 1.5);
    kbd_add_key_w (pool, result, KEY_SPACE, 5.5);
    kbd_add_key_w (pool, result, KEY_RIGHTALT, 1.5);
    kbd_add_key_w (pool, result, KEY_RIGHTCTRL, 1.5);
    kbd_add_key (pool, result, KEY_LEFT);
    kbd_add_key (pool, result, KEY_DOWN);
    kbd_add_key (pool, result, KEY_RIGHT);
    return result;
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

void keyboard_view_get_margins (GtkWidget *keyboard_view, double *left_margin, double *top_margin)
{
    double kbd_width, kbd_height;
    kbd_get_size (kbd, &kbd_width, &kbd_height);

    if (left_margin != NULL) {
        *left_margin = 0;
        int canvas_w = gtk_widget_get_allocated_width (keyboard_view);
        if (kbd_width < canvas_w) {
            *left_margin = floor((canvas_w - kbd_width)/2);
        }
    }

    if (top_margin != NULL) {
        *top_margin = 0;
        int canvas_h = gtk_widget_get_allocated_height (keyboard_view);
        if (kbd_height < canvas_h) {
            *top_margin = floor((canvas_h - kbd_height)/2);
        }
    }
}

gboolean render_keyboard (GtkWidget *widget, cairo_t *cr, gpointer data)
{
    cairo_set_source_rgba (cr, 1, 1, 1, 1);
    cairo_paint(cr);

    cairo_set_line_width (cr, 1);

    mem_pool_t pool = {0};
    double left_margin, top_margin;
    keyboard_view_get_margins (widget, &left_margin, &top_margin);

    if (keyboard_view_mode == KEYBOARD_VIEW_PREVIEW) {
        double y_pos = top_margin;
        struct row_t *curr_row = kbd->first_row;
        while (curr_row != NULL) {
            struct key_t *curr_key = curr_row->first_key;
            double x_pos = left_margin;
            double key_height = curr_row->height*kbd->default_key_size;
            while (curr_key != NULL) {
                double key_width = curr_key->width*kbd->default_key_size;

                // Get an apropriate representation of the key to use as label.
                // This code is likely to get a lot of special cases in the future.
                char buff[64];
                buff[0] = '\0';
                {
                    if (curr_key->kc == KEY_FN) {
                        strcpy (buff, "Fn");
                    }

                    xkb_keysym_t keysym;
                    if (buff[0] == '\0') {
                        int buff_len = 0;
                        keysym = xkb_state_key_get_one_sym(xkb_state, curr_key->kc + 8);
                        buff_len = xkb_keysym_to_utf8(keysym, buff, sizeof(buff-1));
                        buff[buff_len] = '\0';
                    }

                    if (buff[0] == '\0' ||
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

                        if (strcmp (buff, "Alt_L") == 0) {
                            strcpy (buff, "Alt");
                        }

                        if (strcmp (buff, "Alt_R") == 0) {
                            strcpy (buff, "AltGr");
                        }

                        if (strcmp (buff, "ISO_Level3_Shift") == 0) {
                            strcpy (buff, "AltGr");
                        }

                        if (strcmp (buff, "Control_L") == 0) {
                            strcpy (buff, "Ctrl");
                        }

                        if (strcmp (buff, "Control_R") == 0) {
                            strcpy (buff, "Ctrl");
                        }

                        if (strcmp (buff, "Shift_L") == 0) {
                            strcpy (buff, "Shift");
                        }

                        if (strcmp (buff, "Shift_R") == 0) {
                            strcpy (buff, "Shift");
                        }

                        if (strcmp (buff, "Caps_Lock") == 0) {
                            strcpy (buff, "CapsLock");
                        }

                        if (strcmp (buff, "Super_L") == 0) {
                            strcpy (buff, "⌘ ");
                        }

                        if (strcmp (buff, "Super_R") == 0) {
                            strcpy (buff, "⌘ ");
                        }

                        if (strcmp (buff, "Prior") == 0) {
                            strcpy (buff, "Page\nUp");
                        }

                        if (strcmp (buff, "Next") == 0) {
                            strcpy (buff, "Page\nDown");
                        }

                        if (strcmp (buff, "Num_Lock") == 0) {
                            strcpy (buff, "Num\nLock");
                        }

                        if (strcmp (buff, "Scroll_Lock") == 0) {
                            strcpy (buff, "Scroll\nLock");
                        }

                        if (strcmp (buff, "Escape") == 0) {
                            strcpy (buff, "Esc");
                        }

                        if (strcmp (buff, "Up") == 0) {
                            strcpy (buff, "↑");
                        }

                        if (strcmp (buff, "Down") == 0) {
                            strcpy (buff, "↓");
                        }

                        if (strcmp (buff, "Right") == 0) {
                            strcpy (buff, "→");
                        }

                        if (strcmp (buff, "Left") == 0) {
                            strcpy (buff, "←");
                        }

                        if (strcmp (buff, "Return") == 0) {
                            strcpy (buff, "↵ ");
                        }
                    }
                }

                if (curr_key->is_pressed || curr_key->kc == clicked_kc) {
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
    } else if (keyboard_view_mode == KEYBOARD_VIEW_EDIT) {

        double y_pos = top_margin;
        struct row_t *curr_row = kbd->first_row;
        while (curr_row != NULL) {
            struct key_t *curr_key = curr_row->first_key;
            double x_pos = left_margin;
            double key_height = curr_row->height*kbd->default_key_size;
            while (curr_key != NULL) {
                double key_width = curr_key->width*kbd->default_key_size;

                char buff[5];
                snprintf (buff, ARRAY_SIZE(buff), "%i", curr_key->kc);

                if (selected_key != NULL && curr_key->kc == selected_key->kc) {
                    cr_render_key (cr, x_pos, y_pos, key_width, key_height, "", RGB_HEX(0xe34442));
                } else if (curr_key->is_pressed) {
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
    }

    mem_pool_destroy (&pool);
    return FALSE;
}

gboolean key_press_handler (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    uint16_t kc = ((GdkEventKey*)event)->hardware_keycode;
    struct key_t *key = kbd->keys_by_kc[kc-8];
    switch (keyboard_view_mode) {
        case KEYBOARD_VIEW_PREVIEW:
            if (key != NULL) {
                key->is_pressed = true;
            }
            xkb_state_update_key(xkb_state, kc, XKB_KEY_DOWN);
            break;
        case KEYBOARD_VIEW_EDIT:
            if (edit_mode == KEYBOARD_VIEW_EDIT_KEYCODE_KEYPRESS) {
                if (key == NULL) {
                    // NOTE: We only update the keycode if it wasn't assigned
                    // before.
                    kbd->keys_by_kc[selected_key->kc] = NULL;
                    selected_key->kc = kc-8;
                    kbd->keys_by_kc[kc-8] = selected_key;
                }

                selected_key = NULL;
                edit_mode = KEYBOARD_VIEW_EDIT_DEFAULT;
                ungrab_keyboard_handler (NULL, NULL);
            }

            if (key != NULL) {
                key->is_pressed = true;
            }
            break;
        default:
            invalid_code_path;
    }

    gtk_widget_queue_draw (keyboard_view);
    return TRUE;
}

gboolean key_release_handler (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    uint16_t kc = ((GdkEventKey*)event)->hardware_keycode;
    struct key_t *key = kbd->keys_by_kc[kc-8];
    if (key != NULL) {
        key->is_pressed = false;
    }
    xkb_state_update_key(xkb_state, kc, XKB_KEY_UP);
    gtk_widget_queue_draw (keyboard_view);
    return TRUE;
}

struct key_t* keyboard_view_get_key (GtkWidget *keyboard_view, double x, double y, GdkRectangle *rect)
{
    double kbd_x, kbd_y;
    keyboard_view_get_margins (keyboard_view, &kbd_x, &kbd_y);

    struct row_t *curr_row = kbd->first_row;
    while (curr_row != NULL) {
        kbd_y += curr_row->height*kbd->default_key_size;
        if (kbd_y > y) {
            break;
        }

        curr_row = curr_row->next_row;
    }

    struct key_t *curr_key = NULL;
    if (curr_row != NULL) {
        curr_key = curr_row->first_key;
        while (curr_key != NULL) {
            kbd_x += curr_key->width*kbd->default_key_size;
            if (kbd_x > x) {
                break;
            }

            curr_key = curr_key->next_key;
        }
    }

    if (curr_key != NULL && rect != NULL) {
        rect->height = curr_row->height*kbd->default_key_size;
        rect->y = kbd_y - rect->height;

        rect->width = curr_key->width*kbd->default_key_size;
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

    struct key_t *key = keyboard_view_get_key (widget, button_event->x, button_event->y, NULL);
    if (key != NULL) {
        switch (keyboard_view_mode) {
            case KEYBOARD_VIEW_PREVIEW:
                clicked_kc = key->kc;
                xkb_state_update_key(xkb_state, key->kc+8, XKB_KEY_DOWN);
                break;
            case KEYBOARD_VIEW_EDIT:
                break;
            default:
                invalid_code_path;
        }
        gtk_widget_queue_draw (keyboard_view);
    }
    return TRUE;
}

gboolean keyboard_view_button_release (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    switch (keyboard_view_mode) {
        case KEYBOARD_VIEW_PREVIEW:
            xkb_state_update_key(xkb_state, clicked_kc+8, XKB_KEY_UP);
            clicked_kc = 0;
            break;
        case KEYBOARD_VIEW_EDIT:
            {
                // NOTE: We handle this on release because we are taking a grab
                // of all input. Doing so on a key press breaks GTK's grab
                // created before sending the event, which may cause trouble.
                GdkEventButton *button_event = (GdkEventButton*)event;
                struct key_t *key = keyboard_view_get_key (widget, button_event->x, button_event->y, NULL);
                if (edit_mode == KEYBOARD_VIEW_EDIT_DEFAULT) {
                    selected_key = key;
                    grab_keyboard_handler (NULL, NULL);
                    edit_mode = KEYBOARD_VIEW_EDIT_KEYCODE_KEYPRESS;

                } else if (edit_mode == KEYBOARD_VIEW_EDIT_KEYCODE_KEYPRESS) {
                    if (key == selected_key) {
                        ungrab_keyboard_handler (NULL, NULL);
                        selected_key = NULL;
                        edit_mode = KEYBOARD_VIEW_EDIT_DEFAULT;
                    } else {
                        // We will now edit the newly clicked key.
                        selected_key = key;
                    }
                }
            } break;
        default:
            invalid_code_path;
    }
    gtk_widget_queue_draw (keyboard_view);
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
    struct key_t *key = keyboard_view_get_key (widget, x, y, &rect);
    if (key != NULL) {
        gtk_tooltip_set_text (tooltip, keycode_names[key->kc]);

        gtk_tooltip_set_tip_area (tooltip, &rect);
        return TRUE;
    } else {
        return FALSE;
    }
}

GtkWidget* keyboard_view_new ()
{
    GtkWidget *new_keyboard_view = gtk_drawing_area_new ();
    gtk_widget_add_events (new_keyboard_view, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    gtk_widget_set_vexpand (new_keyboard_view, TRUE);
    gtk_widget_set_hexpand (new_keyboard_view, TRUE);
    g_signal_connect (G_OBJECT (new_keyboard_view), "draw", G_CALLBACK (render_keyboard), NULL);
    g_signal_connect (G_OBJECT (new_keyboard_view), "button-press-event", G_CALLBACK (keyboard_view_button_press), NULL);
    g_signal_connect (G_OBJECT (new_keyboard_view), "button-release-event", G_CALLBACK (keyboard_view_button_release), NULL);
    g_object_set_property_bool (G_OBJECT (new_keyboard_view), "has-tooltip", true);
    g_signal_connect (G_OBJECT (new_keyboard_view), "query-tooltip", G_CALLBACK (keyboard_view_tooltip_handler), NULL);
    gtk_widget_show (new_keyboard_view);
    return new_keyboard_view;
}

