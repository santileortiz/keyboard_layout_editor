/*
 * Copiright (C) 2019 Santiago León O.
 */

struct scanner_t {
    char *pos;
    bool is_eof;
    bool eof_is_error;

    bool error;
    char *error_message;

    int line_number;
};

// Calling scanning functions will never set the error flag, it's the
// responsability of the caller to call scanner_set_error() if a match should
// have happened but didn't. To allow this the return value of scanning
// functions is a boolean that is true if the match was successful and false
// otherwise.
//
// After an error is set, the return value for scanning functions  will always
// be false. This allows easy termination of the execution without adding gotos
// everywhere in the code.
//
// NOTE: The error message is not duplicated or stored by the scanner, it just
// stores a pointer to it.
void scanner_set_error (struct scanner_t *scnr, char *error_message)
{
    // Only set this the first time the function is called. Knowing the first
    // error message is more useful than the last.
    if (!scnr->error) {
        scnr->error = true;
        scnr->error_message = error_message;
    }
}

// Sometimes there are blocks of code where reaching EOF is an error, setting
// eof_is_error to true will make reaching EOF be an error. This is convenience
// functionality so we don't need to check for EOF every time we call a scanning
// function inside a block like this.
void scanner_eof_set (struct scanner_t *scnr)
{
    scnr->is_eof = true;
    if (scnr->eof_is_error) {
        scanner_set_error (scnr, "Unexpected end of file.");
    }
}

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

    // Don't accept leading spaces.
    // NOTE: We don't accept floats not starting with a digit like .5, INF or
    // NAN. But we do accept hexadecimal floats like 0x1.Cp2
    if (!isdigit (*scnr->pos)) {
        return false;
    }

    char *end;
    float res = strtof (scnr->pos, &end);
    if (res != 0 || scnr->pos != end) {
        *value = res;
        scnr->pos = end;

        if (*scnr->pos == '\0') {
            scanner_eof_set (scnr);
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

    // Don't accept leading spaces.
    if (!isdigit (*scnr->pos)) {
        return false;
    }

    char *end;
    int res = strtol (scnr->pos, &end, 10);
    if (res != 0 || scnr->pos != end) {
        *value = res;
        scnr->pos = end;

        if (*scnr->pos == '\0') {
            scanner_eof_set (scnr);
        }
        return true;
    }

    return false;
}

// NOTE: The definition of a space demends on the locale. In the POSIX locale
// it means space, \n, \f, \r, \t and \v.
// TODO: We should have a scanner_is_space that uses an internal definition of
// what a space is.
void scanner_consume_spaces (struct scanner_t *scnr)
{
    while (isspace(*scnr->pos)) {
        if (*scnr->pos == '\n') {
            scnr->line_number++;
        }
        scnr->pos++;
    }

    if (*scnr->pos == '\0') {
        scanner_eof_set (scnr);
    }
}

bool scanner_char (struct scanner_t *scnr, char c)
{
    if (scnr->error)
        return false;

    if (*scnr->pos == c) {
        scnr->pos++;

        if (*scnr->pos == '\0') {
            scanner_eof_set (scnr);
        }

        return true;
    }

    return false;
}

bool scanner_char_any (struct scanner_t *scnr, char *char_list)
{
    assert (char_list != NULL);

    if (scnr->error)
        return false;

    while (*char_list != '\0' && *scnr->pos != *char_list) {
        char_list++;
    }

    if (*scnr->pos == '\n') {
        scnr->line_number++;
    }

    if (*char_list != '\0') {
        scnr->pos++;
        return true;
    } else {
        return false;
    }

}

// Consume all characters until c is found. c will be consummed too.
bool scanner_to_char (struct scanner_t *scnr, char c)
{
    if (scnr->error)
        return false;

    while (*scnr->pos != '\0' && *scnr->pos != c) {
        if (*scnr->pos == '\n') {
            scnr->line_number++;
        }
        scnr->pos++;
    }

    if (*scnr->pos == '\0') {
        scanner_eof_set (scnr);
        return false;
    } else {
        scnr->pos++;
        return true;
    }
}

// NOTE: A 'str' containing \n will mess up the line count
bool scanner_str (struct scanner_t *scnr, char *str)
{
    assert (str != NULL);
    if (scnr->error)
        return false;

    size_t len = strlen(str);
    if (strncmp(scnr->pos, str, len) == 0) {
        scnr->pos += len;

        if (*scnr->pos == '\0') {
            scanner_eof_set (scnr);
        }

        return true;
    }

    return false;
}

// NOTE: A 'str' containing \n will mess up the line count
bool scanner_strcase (struct scanner_t *scnr, char *str)
{
    assert (str != NULL);
    if (scnr->error)
        return false;

    size_t len = strlen(str);
    if (strncasecmp(scnr->pos, str, len) == 0) {
        scnr->pos += len;

        if (*scnr->pos == '\0') {
            scanner_eof_set (scnr);
        }

        return true;
    }

    return false;
}

