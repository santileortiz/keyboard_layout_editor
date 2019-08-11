/*
 * Copiright (C) 2019 Santiago León O.
 */

// This API is used to report exist status on code that can fail in several
// ways. Incidentally this is also a very simple logger.
//
// To make the status_t optional, all functions here accept NULL where a
// status_t pointer is required. All error checkings will report no errors in
// that case. This is just a convenience function to allow using functions that
// set a status_t without bothering the caller with defining a status_t
// structure.
//
// Some features that I can think of it could have in the future are:
//
//  - More logging/status levels (info, debug, warning, error). Maybe even have
//    user defined logging levels?.
//
//  - User specified error types. This implies an API for users to detect
//    the type of error, and take action depending on what happened. Do we want
//    to make the user define error types beforehand into the status_t context?
//    (so that we can show the user a list of defined error types), or do we
//    expect the user of this API documents all possible error type strings, I
//    like more the 2nd option. Do we want to use string interning so that error
//    matching is as fast as possible? this is probably too cumbersome and
//    performance here is not that critical.
//
// TODO: Once this looks mature enough we should upstream it into common.h

struct status_message_t {
    char *msg;
    struct status_message_t *next;
};

// NOTE: We expect this struct to be 0 initialized.
struct status_t {
    mem_pool_t pool;

    struct status_message_t *warnings;

    // We need this because we want to print them in the same order they were
    // added. This makes appending to the warnings linked list fast.
    struct status_message_t *warnings_last;

    // TODO: Do we want to be able to set multiple error types? currently this
    // isn't supported. If we want that, then change this into an enum? or an
    // int?.
    bool error;
    char *error_msg;
};

bool status_is_error (struct status_t *status)
{
    if (status == NULL) {
        return false;
    }

    return status->error;
}

bool status_has_warning (struct status_t *status)
{
    if (status == NULL) {
        return false;
    }

    return status->warnings != NULL;
}

// This macro simplifies the implementation of functions that receive formatting
// like printf. I made this macro because I always forget how va_list, va_start,
// va_copy, va_end and vsnprintf interact with each other.
//
// It will insert a block of code that declares a char* variable named OUT_VAR,
// allocated by calling the expression STR_ALLOCATION. From this expression, the
// variable 'size' will have the number of bytes to be allocated.
//
// The function from which this is called must be a varargs function. IN_ARGS
// must contain the name of the last argument before the variable argument list
// which is assumed to also be the format string.
// TODO: Upstream this into common.h
#define _PRINTF_FORMATTING(IN_ARG, STR_ALLOCATION, OUT_VAR)  \
char *OUT_VAR;                                               \
{                                                            \
    va_list args1, args2;                                    \
    va_start (args1, IN_ARG);                                \
    va_copy (args2, args1);                                  \
                                                             \
    size_t size = vsnprintf (NULL, 0, IN_ARG, args1) + 1;    \
    va_end (args1);                                          \
                                                             \
    OUT_VAR = STR_ALLOCATION;                                \
                                                             \
    vsnprintf (OUT_VAR, size, IN_ARG, args2);                \
    va_end (args2);                                          \
}

GCC_PRINTF_FORMAT(2, 3)
void status_error (struct status_t *status, char *format, ...)
{
    if (status == NULL) {
        return;
    }

    // We only set the first error, all subsequent calls do nothing.
    if (!status->error) {
        status->error = true;

        _PRINTF_FORMATTING (format, mem_pool_push_size (&status->pool, size), str);
        status->error_msg = str;
    }
}

GCC_PRINTF_FORMAT(2, 3)
void status_warning (struct status_t *status, char *msg, ...)
{
    if (status == NULL) {
        return;
    }

    struct status_message_t *new_msg = mem_pool_push_struct (&status->pool, struct status_message_t);

    _PRINTF_FORMATTING (msg, mem_pool_push_size (&status->pool, size), str);
    new_msg->msg = str;

    if (status->warnings == NULL) {
        status->warnings = new_msg;
    }
    status->warnings_last->next = new_msg;
    status->warnings_last = new_msg;
}

void str_cat_status (string_t *str, struct status_t *status)
{
    assert (status != NULL);

    struct status_message_t *curr_msg = status->warnings;
    while (curr_msg) {
        str_cat_printf (str, "[WARN] %s\n", curr_msg->msg);
        curr_msg = curr_msg->next;
    }

    if (status->error) {
        str_cat_printf (str, "[ERROR] %s\n", status->error_msg);
    }
}

void status_print (struct status_t *status)
{
    assert (status != NULL);

    struct status_message_t *curr_msg = status->warnings;
    while (curr_msg) {
        printf ("[WARN] %s\n", curr_msg->msg);
        curr_msg = curr_msg->next;
    }

    if (status->error) {
        printf ("[ERROR] %s\n", status->error_msg);
    }
}

