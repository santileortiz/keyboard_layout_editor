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

GCC_PRINTF_FORMAT(2, 3)
void status_error (struct status_t *status, char *format, ...)
{
    if (status == NULL) {
        return;
    }

    // We only set the first error, all subsequent calls do nothing.
    if (!status->error) {
        status->error = true;

        va_list args1, args2;
        va_start (args1, format);
        va_copy (args2, args1);

        size_t size = vsnprintf (NULL, 0, format, args1) + 1;
        va_end (args1);

        char *str = mem_pool_push_size (&status->pool, size);

        vsnprintf (str, size, format, args2);
        va_end (args2);

        status->error_msg = str;
    }
}

// TODO: Allow printf formatted strings here
void status_warning (struct status_t *status, char *msg)
{
    if (status == NULL) {
        return;
    }

    struct status_message_t *new_msg = mem_pool_push_struct (&status->pool, struct status_message_t);
    new_msg->msg = pom_strdup (&status->pool, msg);

    if (status->warnings == NULL) {
        status->warnings = new_msg;
    }
    status->warnings_last->next = new_msg;
    status->warnings_last = new_msg;
}

