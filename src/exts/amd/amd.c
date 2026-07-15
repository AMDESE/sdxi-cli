/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * AMD vendor extensions for sdxi-cli.
 *
 * This file owns only the command-tree registration. The handlers,
 * parsers, telemetry decoders, and printers live in:
 *   - qos_cmd.c
 *   - qos_args.c
 *   - qos_tel.c
 *   - qos_print.c
 * and are declared in qos_internal.h.
 */

#include "exts/registry.h"
#include "qos_internal.h"

static const struct sdxi_cli_cmd amd_qos_cmds[] = {
	{ "cap", NULL, "Show QoS capabilities",
	  AMD_QOS_CAP_USAGE,
	  amd_qos_cmd_cap, NULL },
	{ "status", NULL, "Show QoS device telemetry",
	  AMD_QOS_STATUS_USAGE,
	  amd_qos_cmd_status, NULL },
	{ "set-maxbw", NULL, "Set device max bandwidth",
	  AMD_QOS_SET_MAXBW_USAGE,
	  amd_qos_cmd_set_maxbw, NULL },
	{ "pools", NULL, "List configured pools",
	  AMD_QOS_POOLS_USAGE,
	  amd_qos_cmd_pools, NULL },
	{ "active-pools", NULL, "Show configured-active pool set",
	  AMD_QOS_ACTIVE_POOLS_USAGE,
	  amd_qos_cmd_active_pools, NULL },
	{ "pool-show", NULL, "Show pool configuration",
	  AMD_QOS_POOL_SHOW_USAGE,
	  amd_qos_cmd_pool_show, NULL },
	{ "pool-create", NULL, "Create resource pool",
	  AMD_QOS_POOL_CREATE_USAGE,
	  amd_qos_cmd_pool_create, NULL },
	{ "pool-update", NULL, "Update resource pool",
	  AMD_QOS_POOL_UPDATE_USAGE,
	  amd_qos_cmd_pool_update, NULL },
	{ "pool-destroy", NULL, "Destroy resource pool",
	  AMD_QOS_POOL_DESTROY_USAGE,
	  amd_qos_cmd_pool_destroy, NULL },
	{ "assign", NULL, "Assign function/context to a pool",
	  AMD_QOS_ASSIGN_USAGE,
	  amd_qos_cmd_assign, NULL },
	{ "show-assignment", NULL, "Show assignment",
	  AMD_QOS_SHOW_ASSIGNMENT_USAGE,
	  amd_qos_cmd_show_assignment, NULL },
	{ NULL, NULL, NULL, NULL, NULL, NULL }
};

static const struct sdxi_cli_cmd amd_cmds[] = {
	{ "qos", NULL, "Quality-of-Service extensions (ASQOSE)",
	  "amd qos <command> [<args>]", NULL, amd_qos_cmds },
	{ NULL, NULL, NULL, NULL, NULL, NULL }
};

static const struct sdxi_cli_cmd amd_cmd = {
	"amd", NULL, "AMD vendor extensions", "amd <command> [<args>]", NULL,
	amd_cmds
};

SDXI_EXT_REGISTER(amd_cmd)
