#include <assert.h>

#include "../memctl/memctl_types.h"
#include "../memctl/platform.h"

bool i_command(void);
bool r_command(uint64_t address, size_t length, bool force, bool physical, size_t width, size_t access, bool dump);
bool w_command(kaddr_t address, kword_t value, bool force, bool physical, size_t width,
		size_t access);
bool wd_command(kaddr_t address, const void *data, size_t length, bool force, bool physical,
		size_t access);
bool rb_command(kaddr_t address, size_t length, bool force, bool physical, size_t access);
bool rs_command(kaddr_t address, size_t length, bool force, bool physical, size_t access);
bool ws_command(kaddr_t address, const char *string, bool force, bool physical, size_t access);
bool f_command(kaddr_t start, kaddr_t end, kword_t value, size_t width, bool physical, bool heap,
		size_t access, size_t alignment);

bool default_action(void);

struct state {
	// The current argv string. If part of the current argument string has already been
	// processed, arg might point into the middle of the argv string. NULL when no more
	// arguments are left.
	const char *arg;
	// The argument index. This is used to determine whether an argument may be part of the
	// compact format.
	int argidx;
	// The current argument being populated.
	struct argument *argument;
	// The current (or most recently processed) option, or NULL if options are not currently
	// being processed.
	const struct argspec *option;
	// When processing nameless options, the error message for a failed match will be
	// discarded. The keep_error flag signals that the match succeeded enough that the error
	// message should still be displayed and the command aborted.
	bool keep_error;
	// When processing an option starting with a dash, we sometimes can't tell if this is an
	// unrecognized option or an argument that starts with a dash. If option processing fails,
	// it will fall back to argument parsing. If that also fails, it will print the
	// unrecognized option error message.
	const char *bad_option;
	// The start index for iterating options or arguments.
	unsigned start;
	// The end index for iterating options or arguments.
	unsigned end;
	// The command that has been matched.
	const struct command *command;
	// The vector of arguments to populate.
	struct argument *arguments;
	// argc.
	int argc;
	// argv. Must be NULL-terminated.
	const char **argv;
};


/*
 * ARGUMENT
 *
 * Description:
 * 	Indicates that this is a required argument.
 */
#define ARGUMENT ((const char *)(1))

/*
 * OPTIONAL
 *
 * Description:
 * 	Indicates that this is an optional argument.
 */
#define OPTIONAL ((const char *)(2))

/*
 * argtype
 *
 * Description:
 * 	An enumeration for the types of arguments recognized by the command processing system.
 */
typedef enum argtype {
	ARG_NONE,
	ARG_INT,
	ARG_UINT,
	ARG_WIDTH,
	ARG_DATA,
	ARG_STRING,
	ARG_ARGV,
	ARG_SYMBOL,
	ARG_ADDRESS,
	ARG_RANGE,
	ARG_WORD,
	ARG_WORDS,
} argtype;

/*
 * struct argument
 *
 * Description:
 * 	An argument or option parsed from a command.
 */
struct argument {
	// The name of the option, or ARGUMENT or OPTIONAL if this is a positional argument.
	const char *option;
	// The name of the argument.
	const char *argument;
	// Whether this option or argument was supplied.
	bool present;
	// The argument type.
	argtype type;
	// The argument data. Use type to determine which field to access.
	union {
		intptr_t sint;
		uintptr_t uint;
		size_t width;
		struct argdata {
			void *data;
			size_t length;
		} data;
		const char *string;
		const char **argv;
		struct argsymbol {
			const char *kext;
			const char *symbol;
		} symbol;
		kaddr_t address;
		struct argrange {
			kaddr_t start;
			kaddr_t end;
			bool    default_start;
			bool    default_end;
		} range;
		struct argword {
			size_t width;
			kword_t value;
		} word;
		struct argwords {
			size_t count;
			struct argword *words;
		} words;
	};
};

/*
 * struct argspec
 *
 * Description:
 * 	An argument or option specification.
 */
struct argspec {
	// The name of the option, or ARGUMENT or OPTIONAL if this is a positional argument.
	const char *option;
	// The name of the argument.
	const char *argument;
	// The type of the argument.
	argtype type;
	// A description of this option or argument.
	const char *description;
};

/*
 * handler_fn
 *
 * Description:
 * 	The type of a command handler.
 *
 * Parameters:
 * 		arguments		An array of options and arguments, in the exact same layout
 * 					as argspecv in the corresponding command structure.
 *
 * Returns:
 * 	A bool indicating whether the command executed successfully. This value is returned by
 * 	command_run_argv.
 */
typedef bool (*handler_fn)(const struct argument *arguments);


/*
 * struct command
 *
 * Description:
 * 	A structure describing a command.
 */
struct command {
	// The command string.
	const char *command;
	// The parent command.
	const char *parent;
	// A handler for this command.
	handler_fn handler;
	// A description of this command.
	const char *short_description;
	// A longer description of this command.
	const char *long_description;
	// The number of elements in the argspecv array.
	size_t argspecc;
	// An array of argspec structures describing the options and arguments.
	struct argspec *argspecv;
};

/*
 * default_action_fn
 *
 * Description:
 * 	The type of a default action function, to be executed when no arguments are given at all.
 */
typedef bool (*default_action_fn)(void);

/*
 * struct cli
 *
 * Description:
 * 	A specification of this program's command line interface.
 */
struct cli {
	// The default action if no arguments are given.
	default_action_fn default_action;
	// The number of commands.
	size_t command_count;
	// An array of commands.
	struct command *commands;
};

/*
 * cli
 *
 * Description:
 * 	The cli structure for this program.
 */
extern struct cli cli;

/*
 * command_print_help
 *
 * Description:
 * 	Print a help message, either for the specified command or for this utility.
 *
 * Parameters
 * 		command			The command to print specific help for. If NULL, then a
 * 					generic help message is printed.
 */
bool command_print_help(const struct command *command);

/*
 * command_run_argv
 *
 * Description:
 * 	Parses the argument vector and runs the appropriate command.
 *
 * Parameters:
 * 		argc			The number of elements in argv.
 * 		argv			The argument vector. The first element should be the
 * 					command. Must be NULL terminated.
 *
 * Returns:
 * 	True if the command ran successfully.
 */
bool command_run_argv(int argc, const char *argv[]);
