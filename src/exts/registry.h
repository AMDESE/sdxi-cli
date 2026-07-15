/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef SDXI_CLI_EXT_REGISTRY_H
#define SDXI_CLI_EXT_REGISTRY_H

#include <stddef.h>

#include "cli_cmd.h"

void sdxi_ext_register(const struct sdxi_cli_cmd *vendor_root);
size_t sdxi_ext_vendor_count(void);
const struct sdxi_cli_cmd *sdxi_ext_vendor_at(size_t idx);
const struct sdxi_cli_cmd *sdxi_ext_find_vendor(const char *name);

#define SDXI_EXT_REGISTER(cmd_var)						\
	static void __attribute__((constructor, used))				\
	sdxi_ext_ctor_##cmd_var(void)						\
	{									\
		sdxi_ext_register(&(cmd_var));					\
	}

#endif /* SDXI_CLI_EXT_REGISTRY_H */
