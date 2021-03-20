#ifndef MEMCTL_CLI__ERROR_H_
#define MEMCTL_CLI__ERROR_H_

#include "memctl_error.h"


extern struct error_type message_error;
extern struct error_type usage_error;
extern struct error_type execve_error;
extern struct error_type kext_not_found_error;
extern struct error_type kext_symbol_not_found_error;

/*
 * error_message
 */
void error_message(const char *format, ...);

/*
 * usage_error
 */
struct usage_error {
	const char *command;
	const char *option;
	const char *message;
};

void error_usage(const char *command, const char *option, const char *format, ...);

/*
 * execve_error
 */
struct execve_error {
	const char *path;
	const char *reason;
};

void error_execve(const char *path, const char *error);

/*
 * kext_not_found_error
 */
struct kext_not_found_error {
	const char *bundle_id;
};

void error_kext_not_found(const char *bundle_id);

/*
 * kext_symbol_not_found_error
 */
struct kext_symbol_not_found_error {
	const char *bundle_id;
	const char *symbol;
};

void error_kext_symbol_not_found(const char *bundle_id, const char *symbol);

/*
 * print_errors
 *
 * Description:
 * 	Print out any error messages in the error stack and then clear the stack.
 */
void print_errors(void);

#endif
