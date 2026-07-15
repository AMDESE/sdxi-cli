// SPDX-License-Identifier: BSD-3-Clause
#include "registry.h"

#include <stdbool.h>

#define SDXI_EXT_MAX_VENDORS 32u

static const struct sdxi_cli_cmd *vendor_roots[SDXI_EXT_MAX_VENDORS];
static size_t vendor_roots_count;

static bool vendor_exists(const char *name)
{
	for (size_t i = 0; i < vendor_roots_count; i++) {
		if (sdxi_cli_cmd_matches(vendor_roots[i], name))
			return true;
	}
	return false;
}

void sdxi_ext_register(const struct sdxi_cli_cmd *vendor_root)
{
	if (!vendor_root || !vendor_root->name)
		return;

	if (vendor_exists(vendor_root->name))
		return;

	if (vendor_roots_count >= SDXI_EXT_MAX_VENDORS)
		return;

	vendor_roots[vendor_roots_count++] = vendor_root;
}

size_t sdxi_ext_vendor_count(void)
{
	return vendor_roots_count;
}

const struct sdxi_cli_cmd *sdxi_ext_vendor_at(size_t idx)
{
	if (idx >= vendor_roots_count)
		return NULL;
	return vendor_roots[idx];
}

const struct sdxi_cli_cmd *sdxi_ext_find_vendor(const char *name)
{
	if (!name)
		return NULL;

	for (size_t i = 0; i < vendor_roots_count; i++) {
		if (sdxi_cli_cmd_matches(vendor_roots[i], name))
			return vendor_roots[i];
	}

	return NULL;
}
