#include <stdio.h>
#include <stdlib.h>

#include "kernel_call.h"
#include "kernel_memory.h"
#include "kernel_patches.h"
#include "kext_load.h"
#include "ktrr_bypass.h"
#include "log.h"

#include "memctl_overwrite/histedit.h"
#include "memctl_overwrite/memctl/error.h"
#include "memctl_overwrite/memctl/utility.h"
#include "memctl_overwrite/memctl_modify/memCtlCommand.h"
#include "memctl_overwrite/libmemctl/memctl_error.h"
#include "memctl_overwrite/libmemctl/strparse.h"

#define BUF()	(buf == NULL ? NULL : buf + written)
#define SIZE()	(buf == NULL ? 0 : size - written)
#define WRITE(...)										\
	written += snprintf(BUF(), SIZE(), __VA_ARGS__);					\
	if (buf != NULL && written >= size) {							\
		goto fail;									\
	}


void error_usage(const char *command, const char *option, const char *format, ...);

#define ERROR_USAGE(fmt, ...)				\
	error_usage(NULL, NULL, fmt, ##__VA_ARGS__)

#define ERROR_COMMAND(s, fmt, ...)						\
	({ struct state *s0 = (s);						\
	   error_usage(s0->command->command, NULL, fmt, ##__VA_ARGS__); });
#define ERROR_OPTION(s, fmt, ...)						\
	({ struct state *s0 = (s);						\
	   const char *opt = (s0->option == NULL ? NULL : s0->option->option);	\
	   error_usage(s0->command->command, opt, fmt, ##__VA_ARGS__); })

extern struct error_type usage_error;

typedef bool (*parse_fn)(struct state *);

static bool parse_none(struct state *s);
static bool parse_int(struct state *s);
static bool parse_uint(struct state *s);
static bool parse_width(struct state *s);
static bool parse_data(struct state *s);
static bool parse_string(struct state *s);
static bool parse_argv(struct state *s);
static bool parse_symbol(struct state *s);
static bool parse_address(struct state *s);
//static bool parse_range(struct state *s);
static bool parse_word(struct state *s);
static bool parse_words(struct state *s);

struct savepoint {
	const char *arg;
	int argidx;
	int argc;
	const char **argv;
};

const char KERNEL_ID[] = "__kernel__";

static parse_fn parse_fns[] = {
	parse_none,
	parse_int,
	parse_uint,
	parse_width,
	parse_data,
	parse_string,
	parse_argv,
	parse_symbol,
	parse_address,
	//parse_range,
	parse_word,
	parse_words,
};

static bool
extract_symbol(struct argument *argument, const char *str, size_t len, bool force) {
	if (!force) {
		// We consider a string as a potential symbol if either it begins with "_" or
		// contains ":".
		if (str[0] != '_' && strnchr(str, len, ':') == NULL) {
			return false;
		}
	}
	char *sym = strndup(str, len);
	char *sep = strchr(sym, ':');
	if (sep == NULL) {
		argument->symbol.kext   = KERNEL_ID;
		argument->symbol.symbol = sym;
	} else if (sep == sym) {
		argument->symbol.kext   = NULL;
		argument->symbol.symbol = sym + 1;
	} else {
		*sep = 0;
		argument->symbol.kext   = sym;
		argument->symbol.symbol = sep + 1;
	}
	argument->type = ARG_SYMBOL;
	return true;
}

static bool
verify_width(struct state *s, size_t width) {
	if (width == 0 || width > sizeof(kword_t) || !ispow2(width)) {
		s->keep_error = true;
		ERROR_OPTION(s, "invalid width %zu", width);
		return false;
	}
	return true;
}

static bool advance(struct state *s) {
	if (s->arg == NULL) {
		assert(s->argc == 0);
		return true;
	}
	if (*s->arg == 0) {
		s->argv++;
		s->argc--;
		s->arg = s->argv[0];
		s->argidx++;
		assert(s->argc > 0 || s->arg == NULL);
		return true;
	}
	return false;
}

static bool
parse_string(struct state *s) {
	if (s->arg == NULL) {
		ERROR_OPTION(s, "missing string");
		return false;
	}
	s->argument->string = s->arg;
	s->argument->type = ARG_STRING;
	s->arg += strlen(s->arg);
	assert(*s->arg == 0);
	return true;
}

static bool
parse_argv(struct state *s) {
	assert(s->option == NULL); // TODO: Allow argv in options.
	s->argument->argv = s->argv;
	s->argument->type = ARG_ARGV;
	s->argv += s->argc;
	s->argc  = 0;
	s->arg   = NULL;
	return true;
}


static bool
parse_symbol(struct state *s) {
	if (s->arg == NULL) {
		ERROR_OPTION(s, "missing symbol");
		return false;
	}
	size_t len = strlen(s->arg);
	extract_symbol(s->argument, s->arg, len, true);
	s->arg += len;
	assert(*s->arg == 0);
	return true;
}


static bool
parse_int_internal(struct state *s, struct argument *argument, enum argtype argtype, size_t len) {
	const char *end;
	uintmax_t address;
	unsigned base = 10;
	uintmax_t *intptr;
	bool is_signed = false;
	const char *typename = "integer";
	if (argtype == ARG_ADDRESS) {
		base = 16;
		intptr = &address;
		typename = "address";
	} else if (argtype == ARG_UINT) {
		intptr = &argument->uint;
	} else {
		intptr = (uintmax_t *)&argument->sint;
		is_signed = true;
	}
	enum strtoint_result sr = strtoint(s->arg, len, true, is_signed, base, intptr, &end);
	if (sr == STRTOINT_OVERFLOW) {
		ERROR_OPTION(s, "integer overflow: '%.*s'", len, s->arg);
		return false;
	} else if (sr == STRTOINT_NODIGITS) {
fail:
		ERROR_OPTION(s, "invalid %s: '%.*s'", typename, len, s->arg);
		return false;
	}
	// If we didn't process the whole current argument, and if either this is not the first
	// argument or we are parsing an address, fail.
	if (sr == STRTOINT_BADDIGIT && (s->argidx != 0 || argtype == ARG_ADDRESS)) {
		goto fail;
	}
	if (argtype == ARG_ADDRESS) {
		argument->address = address;
	}
	argument->type = argtype;
	s->arg = end;
	return true;
}

static bool
parse_int(struct state *s) {
	if (s->arg == NULL) {
		ERROR_OPTION(s, "missing integer");
		return false;
	}
	return parse_int_internal(s, s->argument, ARG_INT, -1);
}

static bool
parse_uint(struct state *s) {
	if (s->arg == NULL) {
		ERROR_OPTION(s, "missing integer");
		return false;
	}
	return parse_int_internal(s, s->argument, ARG_UINT, -1);
}

static bool
parse_none(struct state *s) {
	// s->argument->type is already ARG_NONE.
	return true;
}


static bool
parse_width(struct state *s) {
	if (s->arg == NULL) {
		ERROR_OPTION(s, "missing width");
		return false;
	}
	if (!parse_int_internal(s, s->argument, ARG_UINT, -1)) {
		return false;
	}
	size_t width = s->argument->uint;
	if (!verify_width(s, width)) {
		return false;
	}
	s->argument->width = width;
	s->argument->type = ARG_WIDTH;
	return true;
}

static bool
parse_data(struct state *s) {
	if (s->arg == NULL) {
		ERROR_OPTION(s, "missing data");
		return false;
	}
	size_t size = 0;
	const char *end;
	enum strtodata_result sr = strtodata(s->arg, 16, NULL, &size, &end);
	if (sr == STRTODATA_BADBASE) {
		ERROR_OPTION(s, "bad base: '%s'", s->arg);
		return false;
	} else if (sr == STRTODATA_BADDIGIT) {
		ERROR_OPTION(s, "invalid digit '%c': '%s'", *end, s->arg);
		return false;
	} else if (sr == STRTODATA_NEEDDIGIT) {
		ERROR_OPTION(s, "incomplete final byte: '%s'", s->arg);
		return false;
	}
	uint8_t *data = malloc(size);
	sr = strtodata(s->arg, 16, data, &size, &end);
	assert(sr == STRTODATA_OK);
	s->argument->data.data = data;
	s->argument->data.length = size;
	s->argument->type = ARG_DATA;
	s->arg = end;
	assert(*s->arg == 0);
	return true;
}

static bool
parse_word_internal(struct state *s) {
	size_t len = -1;
	const char *sep = strchr(s->arg, ':');
	if (sep != NULL) {
		len = sep - s->arg;
	}
	bool ok = parse_int_internal(s, s->argument, ARG_UINT, len);
	if (!ok) {
		return false;
	}
	kword_t word = s->argument->sint;
	size_t width = sizeof(kword_t);
	if (sep != NULL) {
		assert(s->arg == sep);
		s->arg++;
		ok = parse_int_internal(s, s->argument, ARG_UINT, -1);
		if (!ok) {
			return false;
		}
		width = s->argument->uint;
		ok = verify_width(s, width);
		if (!ok) {
			return false;
		}
	}
	s->argument->word.value = word;
	s->argument->word.width = width;
	s->argument->type = ARG_WORD;
	return true;
}

static bool
parse_word(struct state *s) {
	if (s->arg == NULL) {
		ERROR_OPTION(s, "missing word");
		return false;
	}
	return parse_word_internal(s);
}

static bool
parse_words(struct state *s) {
	// TODO: This is a hack to get around implementing repeated arguments, which is somewhat
	// messy to implement under the design of this parser.
	assert(s->option == NULL);
	struct argword *words = NULL;
	size_t count = 0;
	while (s->arg != NULL) {
		if (!parse_word_internal(s)) {
			return false;
		}
		struct argword *new_words = realloc(words, (count + 1) * sizeof(*words));
		if (new_words == NULL) {
			error_out_of_memory();
			free(words);
			return false;
		}
		words = new_words;
		words[count] = s->argument->word;
		count++;
		advance(s);
	}
	s->argument->words.count = count;
	s->argument->words.words = words;
	s->argument->type = ARG_WORDS;
	s->argv += s->argc;
	s->argc  = 0;
	s->arg   = NULL;
	return true;
}


static bool
parse_address_internal(struct state *s, struct argument *argument, size_t len) {
	if (extract_symbol(argument, s->arg, len, false)) {
		kaddr_t address = 0;
		// bool found = resolve_symbol_address(&address, &argument->symbol);
		// free_symbol(argument);
		// if (!found) {
		// 	ERROR_OPTION(s, "could not resolve symbol '%.*s' to address", len, s->arg);
		// 	return false;
		// }
		argument->address = address;
		argument->type = ARG_ADDRESS;
		s->arg += len;
		return true;
	}
	return parse_int_internal(s, argument, ARG_ADDRESS, len);
}

static bool
parse_address(struct state *s) {
	if (s->arg == NULL) {
		ERROR_OPTION(s, "missing address");
		return false;
	}
	return parse_address_internal(s, s->argument, strlen(s->arg));
}

static void
state_save(const struct state *s, struct savepoint *save) {
	save->arg    = s->arg;
	save->argidx = s->argidx;
	save->argc   = s->argc;
	save->argv   = s->argv;
}

static void
state_restore(struct state *s, const struct savepoint *save) {
	s->arg    = save->arg;
	s->argidx = save->argidx;
	s->argc   = save->argc;
	s->argv   = save->argv;
}

static bool spec_is_option(const struct argspec *spec) {
	return !(spec->option == ARGUMENT || spec->option == OPTIONAL);
}	

static bool in_repl;
static char *prompt_string;

volatile sig_atomic_t interrupted;

static char * repl_prompt(EditLine *el) {
	return prompt_string;
}

static int repl_getc(EditLine *el, char *c) {
	for (;;) {
		errno = 0;
		int ch = fgetc(stdin);
		if (ch != EOF) {
			*c = ch;
			return 1;
		}
		if (interrupted || errno == 0) {
			return 0;
		}
		if (errno != EINTR) {
			return -1;
		}
		// Try again for EINTR.
	}
}

static void reinit_state(struct state *s) {
	assert(s->command != NULL);
	s->option = NULL;
	s->start  = s->end;
	s->end    = s->command->argspecc;
}


static size_t
write_argspec_usage_oneline(const struct argspec *argspec, char *buf, size_t size) {
	size_t written = 0;
	if (argspec->option == ARGUMENT) {
		WRITE("<%s>", argspec->argument);
	} else if (argspec->option == OPTIONAL) {
		WRITE("[%s]", argspec->argument);
	} else {
		if (argspec->type == ARG_NONE) {
			WRITE("[-%s]", argspec->option);
		} else if (argspec->option[0] != 0) {
			WRITE("[-%s %s]", argspec->option, argspec->argument);
		} else {
			assert(argspec->type != ARG_NONE);
			WRITE("[%s]", argspec->argument);
		}
	}
	return written;
fail:
	return 0;
}

static size_t
write_command_usage_oneline(const struct command *command, char *buf, size_t size,
		bool abbreviated) {
	size_t written = 0;
	WRITE("%s", command->command);
	for (size_t i = 0; i < command->argspecc; i++) {
		const struct argspec *s = &command->argspecv[i];
		// If we're doing an abbreviated usage, skip options.
		if (abbreviated && spec_is_option(s)) {
			continue;
		}
		// Insert a space unless it's an unnamed option.
		if (!(spec_is_option(s) && s->option[0] == 0)) {
			WRITE(" ");
		}
		written += write_argspec_usage_oneline(s, BUF(), SIZE());
	}
	return written;
fail:
	return 0;
}

static bool
parse_option(struct state *s) {
	s->option = NULL;
	bool have_dash = false;
	if (advance(s)) {
		if (s->arg == NULL) {
			// We've processed all elements of argv. Nothing left to do.
			return true;
		}
		if (s->arg[0] != '-') {
			// We tried to read the next option, but it doesn't start with a dash, so
			// it's not an option.
			return true;
		}
		if (s->arg[1] == 0) {
			// The next "option" is "-", that is, just a dash. This is not a valid
			// option, so let the arguments handle it.
			return true;
		}
		s->arg++;
		have_dash = true;
	}
	// Check if this matches any option.
	const struct argspec *specs = s->command->argspecv;
	for (size_t i = s->start; i < s->end; i++) {
		size_t len = strlen(specs[i].option);
		if (strncmp(specs[i].option, s->arg, len) == 0) {
			s->option = &specs[i];
			s->argument = &s->arguments[i];
			s->arg += len;
			break;
		}
	}
	if (s->option == NULL) {
		if (have_dash) {
			// We have an option or argument, but we can't tell which. Save this arg as
			// a bad option, back out the dash, and then return true to start
			// processing the arguments.
			s->bad_option = s->arg;
			s->arg--;
			return true;
		}
		ERROR_COMMAND(s, "unrecognized option '%s'", s->arg);
		return false;
	}
	// If this is an unnamed option, do not try matching it again.
	if (s->option->option[0] == 0) {
		s->start++;
	}
	// Disallow duplicate options.
	if (s->argument->present) {
		ERROR_COMMAND(s, "option '%s' given multiple times", s->option->option);
		return false;
	}
	s->argument->present = true;
	// Create a save point in case we have to undo parsing an unnamed option.
	struct savepoint save;
	if (s->option->option[0] == 0) {
		state_save(s, &save);
	}
	// If we're at the end of the current arg string and we expect an argument, advance to the
	// next arg string. Otherwise, stay in the same place so that we can detect the transition
	// to arguments.
	if (*s->arg == 0 && s->option->type != ARG_NONE) {
		advance(s);
	}
	// Try to parse the option's argument.
	assert(s->option->type < sizeof(parse_fns) / sizeof(parse_fns[0]));
	if (!(parse_fns[s->option->type])(s)) {
		// If this is an unnamed option, clear the errors. We'll skip trying to match this
		// option from now on, but continue parsing options on the next iteration.
		if (s->option->option[0] == 0 && error_last()->type == &usage_error
				&& !s->keep_error) {
			error_clear();
			state_restore(s, &save);
			s->argument->present = false;
			return true;
		}
		s->keep_error = false;
		return false;
	}
	return true;
}


static void
free_symbol(struct argument *argument) {
	assert(argument->type == ARG_SYMBOL);
	void *to_free;
	if (argument->symbol.kext == NULL) {
		to_free = (void *)(argument->symbol.symbol - 1);
	} else {
		to_free = (void *)argument->symbol.kext;
	}
	free(to_free);
	argument->type = ARG_NONE;
}

static void init_arguments(const struct command *command, struct argument *arguments) {
	memset(arguments, 0, command->argspecc * sizeof(*arguments));
	for (size_t i = 0; i < command->argspecc; i++) {
		arguments[i].option   = command->argspecv[i].option;
		arguments[i].argument = command->argspecv[i].argument;
		arguments[i].present  = false;
		arguments[i].type     = ARG_NONE;
	}
}

static size_t
option_count(const struct command *command) {
	size_t n = 0;
	while (n < command->argspecc && spec_is_option(&command->argspecv[n])) {
		n++;
	}
	return n;
}

static void init_state(struct state *s, const struct command *command, int argc, const char **argv,
		struct argument *arguments) {
	assert(argc > 0);
	s->arg        = argv[0] + strlen(command->command);
	s->argidx     = 0;
	s->argument   = NULL;
	s->option     = NULL;
	s->keep_error = false;
	s->bad_option = NULL;
	s->start      = 0;
	s->end        = option_count(command);
	s->command    = command;
	s->arguments  = arguments;
	s->argc       = argc;
	s->argv       = argv;
}

static void
break_and_print_indented_string(FILE *file, size_t indent, const char *string) {
	const size_t columns = 80;
	const size_t max_chars = columns - indent;
	size_t next_pos = 0;
	for (;;) {
		// Loop until:
		// 1. We reach the maximum line length, in which case we will print print_len
		//    characters and then start the next line at position next_pos;
		// 2. We encounter a NULL terminator, in which case we print the full line and
		//    abort;
		// 3. We encounter a newline, which means we are starting a new paragraph.
		bool new_paragraph = false;
		size_t begin     = next_pos;
		size_t pos       = begin;
		size_t print_len = 0;
		for (size_t n = 0;; n++, pos++) {
			if (n >= max_chars) {
				// If we haven't yet found a place to divide, just print the whole
				// block.
				if (print_len == 0) {
					print_len = n;
					next_pos = pos;
				}
				break;
			}
			if (string[pos] == 0) {
				print_len = n;
				next_pos  = 0;
				break;
			}
			if (string[pos] == ' ' || string[pos] == '\n') {
				print_len = n;
				next_pos = pos + 1;
			}
			if (string[pos] == '\n') {
				// Do a new paragraph.
				new_paragraph = true;
				break;
			}
		}
		// Now, print the print_len characters from the line starting at position begin.
		fprintf(file, "%*s%.*s\n", (int)indent, "", (int)print_len, string + begin);
		// Handle a new paragraph.
		if (new_paragraph) {
			fprintf(file, "\n");
		}
		// End the loop once we finish the string.
		if (next_pos == 0) {
			break;
		}
	}
}

static bool run_command(const struct command *command, struct argument *arguments) {
	return command->handler(arguments);
}

static bool parse_arguments(struct state *s) {
	for (size_t i = s->start; i < s->end; i++) {
		const struct argspec *spec = &s->command->argspecv[i];
		s->argument = &s->arguments[i];
		// Try to parse the argument.
		assert(spec->type < sizeof(parse_fns) / sizeof(parse_fns[0]));
		if (!(parse_fns[spec->type])(s)) {
			// If we previously had a bad option and it also failed argument parsing,
			// then print the bad option message.
			if (s->bad_option != NULL && error_last()->type == &usage_error) {
				error_clear();
				ERROR_COMMAND(s, "unrecognized option '%s'", s->arg);
				return false;
			}
			// If the issue was that no data was left and we've reached the optional
			// arguments, clear the error and stop processing.
			if (s->arg == NULL && spec->option == OPTIONAL &&
					error_last()->type == &usage_error) {
				error_clear();
				break;
			}
			return false;
		}
		s->argument->present = true;
		// We've processed any pending bad options successfully.
		s->bad_option = NULL;
		// We've finished parsing this argument, but we're still at the end of that arg
		// string. Advance to the next one.
		assert(s->arg == NULL || *s->arg == 0);
		advance(s);
	}
	// If there's any leftover data, emit an error.
	if (s->arg != NULL) {
		ERROR_COMMAND(s, "unexpected argument '%s'", s->arg);
		return false;
	}
	return true;
}

static bool parse_command(const struct command *command, int argc, const char **argv, struct argument *arguments) {
	init_arguments(command, arguments);	
	struct state s;
	init_state(&s, command, argc, argv, arguments);
	// Process all the options.
	do {
		if (!parse_option(&s)) {
		//	printf("[HIT_parse_option] => %s\n",*argv);
			return false;
		}
	} while (s.option != NULL);
	// Process all the arguments.
	reinit_state(&s);
	if (!parse_arguments(&s)) {
//		printf("[HIT_parse_arguments] => %s\n",*argv);
		return false;
	}
	return true;
}

static bool
help_command(const struct command *command) {
	bool success = false;
	// Print out the oneline usage and description.
	size_t length = write_command_usage_oneline(command, NULL, 0, false);
	char *buf = malloc(length + 1);
	if (buf == NULL) {
		error_out_of_memory();
		goto fail;
	}
	write_command_usage_oneline(command, buf, length + 1, false);
	fprintf(stderr, "\n%s\n\n", buf);
	free(buf);
	break_and_print_indented_string(stderr, 4, command->long_description);
	// Get the argspec length.
	const struct argspec *s = command->argspecv;
	const struct argspec *const send = s + command->argspecc;
	size_t argspec_length = 4;
	for (; s < send; s++) {
		length = write_argspec_usage_oneline(s, NULL, 0);
		if (length > argspec_length) {
			argspec_length = length;
		}
	}
	// Print out the options and arguments.
	argspec_length += 1;
	buf = malloc(argspec_length);
	if (buf == NULL) {
		error_out_of_memory();
		goto fail;
	}
	bool printed_options = false;
	bool printed_arguments = false;
	for (s = command->argspecv; s < send; s++) {
		// Print out the section header.
		if (spec_is_option(s) && !printed_options) {
			fprintf(stderr, "\nOptions:\n");
			printed_options = true;
		} else if (!spec_is_option(s) && !printed_arguments) {
			fprintf(stderr, "\nArguments:\n");
			printed_arguments = true;
		}
		// Print out details for this option or argument.
		write_argspec_usage_oneline(s, buf, argspec_length);
		fprintf(stderr, "    %-*s %s\n", (int)argspec_length, buf, s->description);
	}
	fprintf(stderr, "\n");
	free(buf);
	success = true;
fail:
	return success;
}

static bool help_all() {
	const struct command *c = cli.commands;
	const struct command *end = c + cli.command_count;
	// Get the length of the usage string.
	size_t usage_length = 4;
	for (; c < end; c++) {
		size_t length = write_command_usage_oneline(c, NULL, 0, true);
		if (length > usage_length) {
			usage_length = length;
		}
	}
	// Print the usage strings.
	usage_length += 1;
	char *buf = malloc(usage_length);
	if (buf == NULL) {
		error_out_of_memory();
		return false;
	}
	for (c = cli.commands; c < end; c++) {
		write_command_usage_oneline(c, buf, usage_length, true);
		fprintf(stderr, "%-*s %s\n", (int)usage_length, buf, c->short_description);
	}
	free(buf);
	return true;
}

static bool find_command(const char *str, const struct command **command) {
	if (strcmp(str, "?") == 0) {
		return help_all();
	}
	const struct command *c = cli.commands;
	const struct command *end = c + cli.command_count;
	const struct command *best = NULL;
	
	size_t match = 0;
	for (; c < end; c++) {
		size_t len = strlen(c->command);
		if (strncmp(c->command, str, len) == 0) {
			if (len > match) {
				best = c;
				match = len;
			}
		}
	}
	if (best == NULL) {
		ERROR_USAGE("unknown command '%s'", str);
		return false;
	}
	if (strcmp(str + match, "?") == 0) {
		return help_command(best);
	}
	// printf("[HIT find_command ] => %s \n", str);
	*command = best;
	return true;
}

static void
cleanup_argument(struct argument *argument) {
	switch (argument->type) {
		case ARG_DATA:
			free(argument->data.data);
			break;
		case ARG_SYMBOL:
			free_symbol(argument);
			break;
		case ARG_WORDS:
			free(argument->words.words);
			break;
		default:
			break;
	}
}

static void
cleanup_arguments(const struct command *command, struct argument *arguments) {
	for (size_t i = 0; i < command->argspecc; i++) {
		cleanup_argument(&arguments[i]);
	}
}


bool command_run_argv(int argc, const char *argv[]) {
	// assert(argv[argc] == NULL);
	if (argc < 1) {
		return command_run_argv(argc - 1, argv + 1);
	}
	const struct command *c = NULL;
	if (!find_command(argv[0], &c)) {
		return false;
	}
	if (c == NULL) {
		return true;
	}
	// printf("[HIT Command ] => %s \n", c->command);
	struct argument arguments[c->argspecc];
	bool success = false;
	if (!parse_command(c, argc, argv, arguments)) {
		goto fail;
		return false;
	}
	// printf("[HIT Command ] => %s \n", c->command);
	success = run_command(c, arguments);
fail:
	cleanup_arguments(c, arguments);
	return success;
}

bool memShow_cli(int argc, const char *argv[])
{
	bool success = false;
	EditLine *el = el_init(getprogname(), stdin, stdout, stderr);
	Tokenizer *tok = tok_init(NULL);
	History *hist = history_init();
	HistEvent ev;

	history(hist, &ev, H_SETSIZE, 256);
	el_set(el, EL_HIST, history, hist);
	el_set(el, EL_SIGNAL, 0);
	el_set(el, EL_PROMPT, repl_prompt);
	el_set(el, EL_EDITOR, "emacs");
	el_set(el, EL_GETCFN, repl_getc);

	in_repl = true;
	//asprintf(&prompt_string, "%s> ", getprogname());
	asprintf(&prompt_string, "%s> ", "se0g1");
	if(tok)
	{
		while(in_repl)
		{
			int n;
			interrupted = 0;
			const char *line = el_gets(el, &n);
			if (interrupted) {
				fprintf(stdout, "^C\n");
				interrupted = 0;
				continue;
			}
			if (line == NULL || n == 0)
			{
				printf("\n");
				break;
			}
			int argc;
			const char **argv;
			tok_str(tok, line, &argc, &argv);
			if (argc > 0) 
			{
				history(hist, &ev, H_ENTER, line);
				command_run_argv(argc, argv);
			}
			tok_reset(tok);
			//print_errors();
 		}
		 success = true;
		 free(prompt_string);
		 in_repl = false;
		 history_end(hist);
	}
	return 0;
}


int initialize(){
	int ret = 1;
	// Load the kernel symbol database.
	bool ok = kext_load_set_kernel_symbol_database("kernel_symbols");
	if (!ok) {
		ERROR("Could not load kernel symbol database");
		goto done_0;
	}
	// Try to get the kernel task port using task_for_pid().
	kernel_task_port = MACH_PORT_NULL;
	task_for_pid(mach_task_self(), 0, &kernel_task_port);
	if (kernel_task_port == MACH_PORT_NULL) {
		ERROR("Could not get kernel task port");
		goto done_0;
	}
	INFO("task_for_pid(0) = 0x%x", kernel_task_port);
	// Initialize our kernel function calling capability.
	ok = kernel_call_init();
	if (!ok) {
		ERROR("Could not initialize kernel_call subsystem");
		goto done_0;
	}
	// TODO: Check if we've already bypassed KTRR.
	// Ensure that we have a KTRR bypass.
	ok = have_ktrr_bypass();
	if (!ok) {
		ERROR("No KTRR bypass is available for this platform");
		goto done_1;
	}
	// Bypass KTRR and remap the kernel as read/write.
	ok = ktrr_bypass();
	if (!ok) {
		ERROR("NO KTRR bypass is available for this ktrr_bypass");
		goto done_0;
	}

	return true;
	// Apply kernel patches.
	// apply_kernel_patches();
	// Load the kernel extension.
done_1:
	// De-initialize our kernel function calling primitive.
	kernel_call_deinit();
	return 0;
done_0:
	return ret;
}

int main(int argc, const char *argv[]) {
	int init = initialize();

	if(init){
		memShow_cli(argc - 1, argv + 1);
	}
	else{
		return 0;
	}

	return true;
}

