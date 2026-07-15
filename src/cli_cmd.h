/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef SDXI_CLI_CMD_H
#define SDXI_CLI_CMD_H

#include <stdbool.h>
#include <stddef.h>

typedef int (*sdxi_cli_cmd_fn)(const char *prog, int argc, char **argv);

struct sdxi_cli_cmd {
	const char *name;
	const char * const *aliases;
	const char *desc;
	const char *usage;
	sdxi_cli_cmd_fn fn;
	const struct sdxi_cli_cmd *subcmds;
};

bool sdxi_cli_cmd_matches(const struct sdxi_cli_cmd *cmd, const char *name);
const struct sdxi_cli_cmd *sdxi_cli_cmd_find(const struct sdxi_cli_cmd *cmds,
					     const char *name);
size_t sdxi_cli_cmd_name_len_max(const struct sdxi_cli_cmd *cmds);
void sdxi_cli_cmd_print_list(const struct sdxi_cli_cmd *cmds, size_t padding);
void sdxi_cli_cmd_print_help(const char *prog, const struct sdxi_cli_cmd *cmd);
int sdxi_cli_cmd_invoke(const char *prog, const struct sdxi_cli_cmd *cmd,
			int argc, char **argv);

#endif /* SDXI_CLI_CMD_H */

