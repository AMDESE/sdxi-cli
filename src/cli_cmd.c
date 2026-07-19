/* SPDX-License-Identifier: BSD-3-Clause */
#include "cli_cmd.h"

#include <stdio.h>
#include <string.h>

bool sdxi_cli_cmd_matches(const struct sdxi_cli_cmd *cmd, const char *name)
{
	const char * const *alias;

	if (!cmd || !cmd->name || !name)
		return false;

	if (strcmp(cmd->name, name) == 0)
		return true;

	if (!cmd->aliases)
		return false;

	for (alias = cmd->aliases; *alias; alias++) {
		if (strcmp(*alias, name) == 0)
			return true;
	}

	return false;
}

const struct sdxi_cli_cmd *sdxi_cli_cmd_find(const struct sdxi_cli_cmd *cmds,
					     const char *name)
{
	if (!cmds || !name)
		return NULL;

	for (const struct sdxi_cli_cmd *cmd = cmds; cmd->name; cmd++) {
		if (sdxi_cli_cmd_matches(cmd, name))
			return cmd;
	}

	return NULL;
}

size_t sdxi_cli_cmd_name_len_max(const struct sdxi_cli_cmd *cmds)
{
	size_t padding = 0;

	if (!cmds)
		return 0;

	for (const struct sdxi_cli_cmd *cmd = cmds; cmd->name; cmd++) {
		size_t len = strlen(cmd->name);
		if (len > padding)
			padding = len;
	}

	return padding;
}

void sdxi_cli_cmd_print_list(const struct sdxi_cli_cmd *cmds, size_t padding)
{
	if (!cmds)
		return;

	for (const struct sdxi_cli_cmd *cmd = cmds; cmd->name; cmd++) {
		printf("  %-*s     %s\n", (int)padding, cmd->name,
		       cmd->desc ? cmd->desc : "");
	}
}

void sdxi_cli_cmd_print_help(const char *prog, const struct sdxi_cli_cmd *cmd)
{
	size_t padding;

	if (!cmd || !cmd->name)
		return;

	printf("%s - %s\n\n", cmd->name, cmd->desc ? cmd->desc : "");
	printf("Usage: %s %s\n", prog, cmd->usage ? cmd->usage : cmd->name);

	if (!cmd->subcmds)
		return;

	padding = sdxi_cli_cmd_name_len_max(cmd->subcmds);

	printf("\nSubcommands:\n");
	sdxi_cli_cmd_print_list(cmd->subcmds, padding);
}

static bool is_help_token(const char *s)
{
	return s && (strcmp(s, "-h") == 0 || strcmp(s, "--help") == 0 ||
		     strcmp(s, "help") == 0);
}

int sdxi_cli_cmd_invoke(const char *prog, const struct sdxi_cli_cmd *cmd,
			int argc, char **argv)
{
	const struct sdxi_cli_cmd *sub;

	if (!cmd)
		return 1;

	if (argc > 0 && is_help_token(argv[0])) {
		sdxi_cli_cmd_print_help(prog, cmd);
		return 0;
	}

	if (cmd->subcmds) {
		if (argc == 0) {
			sdxi_cli_cmd_print_help(prog, cmd);
			return 0;
		}

		sub = sdxi_cli_cmd_find(cmd->subcmds, argv[0]);
		if (!sub) {
			fprintf(stderr, "Unknown subcommand: '%s'.\n\n", argv[0]);
			sdxi_cli_cmd_print_help(prog, cmd);
			return 1;
		}

		return sdxi_cli_cmd_invoke(prog, sub, argc - 1, argv + 1);
	}

	if (!cmd->fn) {
		sdxi_cli_cmd_print_help(prog, cmd);
		return 1;
	}

	return cmd->fn(prog, argc, argv);
}
