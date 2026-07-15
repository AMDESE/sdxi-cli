/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * SDXI - Main function for sdxi-cli
 */

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <libgen.h>

#define SDXI_VERSION "0.1.0"
#define SDXI_DESCRIPTION \
	"Command line tool for interacting with SDXI devices and drivers."

typedef int (*cmd_handler)(const char *prog, int argc, char **argv);

struct cmd_entry {
	const char *name;
	const char *desc;
	const char *usage;
	cmd_handler func;
};

static int sdxi_cmd_version(const char *prog, int argc, char **argv);
static int sdxi_cmd_help(const char *prog, int argc, char **argv);

static const struct cmd_entry cmd_table[] = {
	{ "version", "Show program version",    "version",           sdxi_cmd_version },
	{ "help",    "Display this help",       "help [command]",    sdxi_cmd_help    },
	{ NULL, NULL, NULL, NULL }
};

static const struct cmd_entry *find_command(const char *name)
{
	const struct cmd_entry *entry;

	for (entry = cmd_table; entry->name; entry++) {
		if (strcmp(entry->name, name) == 0)
			return entry;
	}

	return NULL;
}

static void print_general_help(const char *prog)
{
	size_t padding = 0;
	const struct cmd_entry *entry;

	for (entry = cmd_table; entry->name; entry++) {
		size_t len = strlen(entry->name);
		if (len > padding)
			padding = len;
	}

	printf("%s %s\n", prog, SDXI_VERSION);
	printf("usage: %s <command> [<args>]\n\n", prog);
	printf("%s\n", SDXI_DESCRIPTION);
	printf("\n");
	printf("Available commands:\n");

	for (entry = cmd_table; entry->name; entry++)
		printf("  %-*s   %s\n", (int)padding, entry->name, entry->desc);

	printf("\nRun `%s help <command>` for more details.\n", prog);
}

static int sdxi_cmd_help(const char *prog, int argc, char **argv)
{
	const struct cmd_entry *entry;

	if (argc == 0) {
		print_general_help(prog);
		return 0;
	}

	entry = find_command(argv[0]);

	if (!entry) {
		fprintf(stderr, "Unknown command `%s`.\n\n", argv[0]);
		print_general_help(prog);
		return 1;
	}

	printf("%s - %s\n\n", entry->name, entry->desc);
	printf("Usage: %s %s\n", prog, entry->usage);

	return 0;
}

static int sdxi_cmd_version(const char *prog, int argc, char **argv)
{
	printf("%s version %s\n", prog, SDXI_VERSION);

	return 0;
}

static int invoke_cmd(const char *prog, const char *cmd, int argc, char **argv)
{
	const struct cmd_entry *entry = find_command(cmd);

	if (!entry) {
		fprintf(stderr, "Invalid command `%s`.\n\n", cmd);
		print_general_help(prog);
		return 1;
	}

	return entry->func(prog, argc, argv);
}

int main(int argc, char *argv[])
{
	const char *prog = basename(argv[0]);
	const char *cmd = NULL;
	int cmd_argc = (argc > 2) ? argc - 2 : 0;
	char **cmd_argv = (argc > 2) ? &argv[2] : NULL;

	if (argc < 2) {
		cmd = "help";
	} else if (strcmp(argv[1], "help") == 0 ||
		   strcmp(argv[1], "--help") == 0 ||
		   strcmp(argv[1], "-h") == 0) {
		cmd = "help";
	} else if (strcmp(argv[1], "version") == 0 ||
		   strcmp(argv[1], "--version") == 0) {
		cmd = "version";
	} else {
		fprintf(stderr, "Invalid command `%s`.\n\n", argv[1]);
		print_general_help(prog);
		return 1;
	}

	return invoke_cmd(prog, cmd, cmd_argc, cmd_argv);
}
