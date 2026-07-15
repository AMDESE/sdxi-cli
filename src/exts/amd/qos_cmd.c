/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * AMD QoS - command handlers. Each handler:
 *   1) parses CLI args
 *   2) opens the PF and validates supportability
 *   3) issues vendor-op telemetry/state updates via libsdxi
 *   4) prints the result via qos_print.c
 *
 * Each handler keeps a local `path` string snapshot. ctx.path is owned
 * by ctx, so we capture it before the cleanup boundary so error reports
 * issued after cleanup remain valid.
 */

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libsdxi.h>

#include "qos_internal.h"

/* ------------------------------------------------------------------ */
/* cap                                                                */
/* ------------------------------------------------------------------ */

int amd_qos_cmd_cap(const char *prog, int argc, char **argv)
{
	struct amd_qos_simple_args args;
	struct amd_qos_ctx ctx;
	struct sdxi_amd_qos_cap cap;
	const char *path;
	uint8_t qos_ver_major;
	uint8_t qos_ver_minor;
	int rc;
	int st;

	if (!amd_qos_parse_simple_cli_args(prog, AMD_QOS_CAP_USAGE, argc, argv,
					   &args, &rc, AMD_QOS_OPT_NONE))
		return rc;

	st = amd_qos_open_supported(args.dev_arg, args.json, &ctx);
	if (st)
		return 1;
	path = ctx.path;

	st = amd_qos_query_cap(&ctx, args.timeout_ms, &cap);
	amd_qos_ctx_cleanup(&ctx);
	if (st)
		return amd_qos_report_vendor_error(path, args.json, st,
						   "capability query");

	qos_ver_major = (uint8_t)((cap.qos_version >> 8) & 0xff);
	qos_ver_minor = (uint8_t)(cap.qos_version & 0xff);

	if (args.json) {
		printf("{\n");
		printf("  \"dev\": \"%s\",\n", path);
		printf("  \"qos_guid\": \"%s\",\n", AMD_QOS_GUID_STR);
		printf("  \"supported\": true,\n");
		printf("  \"qos_version\": \"%u.%u\",\n", qos_ver_major,
		       qos_ver_minor);
		printf("  \"feature_flags\": %" PRIu16 ",\n", cap.feature_flags);
		printf("  \"device_maxbw_cap\": %" PRIu32 ",\n",
		       cap.device_maxbw_cap_mb_s);
		printf("  \"max_pools\": %" PRIu32 ",\n",
		       (uint32_t)cap.max_pools);
		printf("  \"granularity_bw\": %" PRIu32 ",\n",
		       cap.granularity_bw_mb_s);
		printf("  \"timestamp_units\": %" PRIu32 ",\n",
		       (uint32_t)cap.timestamp_units);
		printf("  \"timestamp_units_str\": \"%s\",\n",
		       amd_qos_ts_units_str(cap.timestamp_units));
		printf("  \"timestamp_resolution\": %" PRIu32 "\n",
		       cap.timestamp_resolution);
		printf("}\n");
		return 0;
	}

	printf("Device:      %s\n", path);
	printf("QoS GUID:    %s\n", AMD_QOS_GUID_STR);
	printf("Supported:   yes\n");
	printf("QoS version: %u.%u\n", qos_ver_major, qos_ver_minor);
	printf("Flags:       0x%04" PRIx16 "\n", cap.feature_flags);
	printf("Max BW cap:  %" PRIu32 " MB/s\n", cap.device_maxbw_cap_mb_s);
	printf("Max pools:   %" PRIu32 "\n", (uint32_t)cap.max_pools);
	printf("BW gran:     %" PRIu32 " MB/s\n", cap.granularity_bw_mb_s);
	printf("TS units:    %s (%" PRIu32 ")\n",
	       amd_qos_ts_units_str(cap.timestamp_units),
	       (uint32_t)cap.timestamp_units);
	printf("TS res:      %" PRIu32 "\n", cap.timestamp_resolution);
	amd_qos_feature_flags_print_human(cap.feature_flags);
	return 0;
}

/* ------------------------------------------------------------------ */
/* status                                                             */
/* ------------------------------------------------------------------ */

int amd_qos_cmd_status(const char *prog, int argc, char **argv)
{
	struct amd_qos_simple_args args;
	struct amd_qos_ctx ctx;
	struct sdxi_amd_qos_cap cap;
	struct amd_qos_device_tel tel;
	const char *path;
	int rc;
	int st;

	if (!amd_qos_parse_simple_cli_args(prog, AMD_QOS_STATUS_USAGE,
					   argc, argv, &args, &rc,
					   AMD_QOS_OPT_NONE))
		return rc;

	st = amd_qos_open_supported(args.dev_arg, args.json, &ctx);
	if (st)
		return 1;
	path = ctx.path;

	st = amd_qos_query_cap(&ctx, args.timeout_ms, &cap);
	if (st) {
		amd_qos_ctx_cleanup(&ctx);
		return amd_qos_report_vendor_error(path, args.json, st,
						   "capability query");
	}

	st = amd_qos_query_device_telemetry(&ctx, args.timeout_ms, &tel);
	amd_qos_ctx_cleanup(&ctx);
	if (st)
		return amd_qos_report_vendor_error(path, args.json, st,
						   "device telemetry query");

	if (!amd_qos_timestamps_supported(&cap)) {
		tel.configured_timestamp = 0;
		tel.query_timestamp = 0;
	}

	amd_qos_print_device_telemetry(path, args.json, &cap, &tel);
	return 0;
}

/* ------------------------------------------------------------------ */
/* set-maxbw <bw>                                                     */
/* ------------------------------------------------------------------ */

int amd_qos_cmd_set_maxbw(const char *prog, int argc, char **argv)
{
	struct amd_qos_simple_args args;
	struct amd_qos_ctx ctx;
	struct sdxi_amd_qos_cap cap;
	struct amd_qos_device_tel pre_tel;
	struct amd_qos_device_tel tel;
	struct sdxi_admin_cpl cpl;
	const char *path;
	uint32_t requested_maxbw;
	uint32_t effective_maxbw;
	int rc;
	int st;

	if (!amd_qos_parse_simple_cli_args(prog, AMD_QOS_SET_MAXBW_USAGE,
					   argc, argv, &args, &rc,
					   AMD_QOS_OPT_MAXBW))
		return rc;

	st = amd_qos_parse_bw_mb_s(args.maxbw_arg, &requested_maxbw);
	if (st) {
		fprintf(stderr, "Invalid bandwidth `%s`: %s\n", args.maxbw_arg,
			amd_qos_bw_err_str(st));
		return 1;
	}

	st = amd_qos_open_supported(args.dev_arg, args.json, &ctx);
	if (st)
		return 1;
	path = ctx.path;

	st = amd_qos_query_cap(&ctx, args.timeout_ms, &cap);
	if (st) {
		amd_qos_ctx_cleanup(&ctx);
		return amd_qos_report_vendor_error(path, args.json, st,
						   "capability query");
	}

	effective_maxbw = requested_maxbw;
	if (cap.granularity_bw_mb_s != 0)
		effective_maxbw -= effective_maxbw % cap.granularity_bw_mb_s;

	if (!args.json && requested_maxbw != effective_maxbw) {
		fprintf(stderr,
			"Warning: requested max bandwidth %" PRIu32
			" MB/s rounded down to %" PRIu32
			" MB/s to match device granularity %" PRIu32 " MB/s.\n",
			requested_maxbw, effective_maxbw,
			cap.granularity_bw_mb_s);
	}
	if (!args.json && cap.device_maxbw_cap_mb_s != 0 &&
	    effective_maxbw > cap.device_maxbw_cap_mb_s) {
		fprintf(stderr,
			"Warning: requested device max bandwidth %" PRIu32
			" MB/s exceeds device_maxbw_cap %" PRIu32
			" MB/s; the device may cap or reject it.\n",
			effective_maxbw, cap.device_maxbw_cap_mb_s);
	}

	if (effective_maxbw < cap.device_maxbw_cap_mb_s) {
		st = amd_qos_query_device_telemetry(&ctx, args.timeout_ms,
						    &pre_tel);
		if (st) {
			amd_qos_ctx_cleanup(&ctx);
			return amd_qos_report_vendor_error(path, args.json, st,
							   "device telemetry preflight");
		}

		if (effective_maxbw < pre_tel.total_allocated_bw_mb_s) {
			amd_qos_ctx_cleanup(&ctx);
			return amd_qos_emit_underallocated_maxbw(
				path, args.json, requested_maxbw,
				effective_maxbw,
				pre_tel.total_allocated_bw_mb_s);
		}
	}

	memset(&cpl, 0, sizeof(cpl));
	st = sdxi_amd_qos_dev_update(ctx.dev, effective_maxbw, args.timeout_ms,
				     &cpl);
	if (st) {
		amd_qos_ctx_cleanup(&ctx);
		return amd_qos_report_vendor_error(path, args.json, st,
						   "device update");
	}

	st = amd_qos_query_device_telemetry(&ctx, args.timeout_ms, &tel);
	amd_qos_ctx_cleanup(&ctx);
	if (st) {
		fprintf(stderr,
			"AMD QoS device update completed, but post-update telemetry query failed: %s\n",
			sdxi_err_str(st));
		return 1;
	}

	if (!amd_qos_timestamps_supported(&cap)) {
		tel.configured_timestamp = 0;
		tel.query_timestamp = 0;
	}

	if (!args.json && tel.device_maxbw_mb_s != effective_maxbw) {
		fprintf(stderr,
			"Warning: device applied %" PRIu32
			" MB/s after update request of %" PRIu32 " MB/s.\n",
			tel.device_maxbw_mb_s, effective_maxbw);
	}

	amd_qos_print_device_set_result(path, args.json, &cap, &tel,
					requested_maxbw, effective_maxbw,
					cpl.cst_flags);
	return 0;
}

/* ------------------------------------------------------------------ */
/* pool-show                                                          */
/* ------------------------------------------------------------------ */

int amd_qos_cmd_pool_show(const char *prog, int argc, char **argv)
{
	struct amd_qos_pool_cli_args args;
	struct amd_qos_ctx ctx;
	struct sdxi_amd_qos_cap cap;
	struct amd_qos_pool_cfg cfg;
	const char *path;
	int rc;
	int st;

	if (!amd_qos_parse_pool_cli_args(prog, AMD_QOS_POOL_SHOW_USAGE, argc,
					 argv, &args, &rc))
		return rc;
	if (!args.have_pool) {
		fprintf(stderr, "--pool is required\n");
		amd_qos_print_usage(prog, AMD_QOS_POOL_SHOW_USAGE);
		return 1;
	}
	if (args.minbw_arg || args.maxbw_arg) {
		fprintf(stderr, "pool-show does not accept --minbw or --maxbw\n");
		amd_qos_print_usage(prog, AMD_QOS_POOL_SHOW_USAGE);
		return 1;
	}

	st = amd_qos_open_supported(args.dev_arg, args.json, &ctx);
	if (st)
		return 1;
	path = ctx.path;

	st = amd_qos_query_cap(&ctx, args.timeout_ms, &cap);
	if (st) {
		amd_qos_ctx_cleanup(&ctx);
		return amd_qos_report_vendor_error(path, args.json, st,
						   "capability query");
	}

	st = amd_qos_validate_pool_range(&cap, args.pool_num, true);
	if (st) {
		amd_qos_ctx_cleanup(&ctx);
		fprintf(stderr,
			"Pool %" PRIu16 " is out of range (max_pools=%" PRIu16 ")\n",
			args.pool_num, cap.max_pools);
		return 1;
	}

	st = amd_qos_query_pool_cfg(&ctx, args.timeout_ms, args.pool_num, &cfg);
	amd_qos_ctx_cleanup(&ctx);
	if (st)
		return amd_qos_report_vendor_error(path, args.json, st,
						   "pool telemetry query");

	if (!amd_qos_timestamps_supported(&cap)) {
		cfg.configured_timestamp = 0;
		cfg.query_timestamp = 0;
	}

	amd_qos_print_pool_cfg(path, args.json, &cap, &cfg);
	return 0;
}

/* ------------------------------------------------------------------ */
/* pools / active-pools                                               */
/* ------------------------------------------------------------------ */

static int amd_qos_cmd_pools_common(const char *prog, int argc, char **argv,
				    const char *usage, bool active_only)
{
	struct amd_qos_pool_cli_args args;
	struct amd_qos_ctx ctx;
	struct sdxi_amd_qos_cap cap;
	struct amd_qos_active_pools_tel active = { 0 };
	struct amd_qos_pool_cfg *cfgs = NULL;
	const char *path;
	uint16_t active_count = 0;
	uint16_t i;
	uint16_t j = 0;
	int rc;
	int st;

	if (!amd_qos_parse_pool_cli_args(prog, usage, argc,
					 argv, &args, &rc))
		return rc;
	if (args.have_pool || args.minbw_arg || args.maxbw_arg) {
		fprintf(stderr,
			"%s does not accept --pool, --minbw, or --maxbw\n",
			active_only ? "active-pools" : "pools");
		amd_qos_print_usage(prog, usage);
		return 1;
	}

	st = amd_qos_open_supported(args.dev_arg, args.json, &ctx);
	if (st)
		return 1;
	path = ctx.path;

	st = amd_qos_query_cap(&ctx, args.timeout_ms, &cap);
	if (st) {
		amd_qos_ctx_cleanup(&ctx);
		return amd_qos_report_vendor_error(path, args.json, st,
						   "capability query");
	}
	if (cap.max_pools == 0) {
		amd_qos_ctx_cleanup(&ctx);
		fprintf(stderr, "Device reports no QoS pools\n");
		return 1;
	}

	st = amd_qos_query_active_pools(&ctx, &cap, args.timeout_ms, &active);
	if (st) {
		amd_qos_ctx_cleanup(&ctx);
		return amd_qos_report_vendor_error(path, args.json, st,
						   "active pools query");
	}

	if (active_only) {
		amd_qos_print_active_pools(path, args.json, &cap, &active);
		amd_qos_active_pools_cleanup(&active);
		amd_qos_ctx_cleanup(&ctx);
		return 0;
	}

	for (i = 0; i < active.max_pools; i++) {
		if (amd_qos_active_pool_test(&active, i))
			active_count++;
	}

	if (active_count == 0) {
		amd_qos_print_pool_cfg_list(path, args.json, &cap, NULL, 0);
		amd_qos_active_pools_cleanup(&active);
		amd_qos_ctx_cleanup(&ctx);
		return 0;
	}

	cfgs = calloc(active_count, sizeof(*cfgs));
	if (!cfgs) {
		amd_qos_active_pools_cleanup(&active);
		amd_qos_ctx_cleanup(&ctx);
		fprintf(stderr, "Out of memory\n");
		return 1;
	}

	for (i = 0; i < active.max_pools; i++) {
		if (!amd_qos_active_pool_test(&active, i))
			continue;

		st = amd_qos_query_pool_cfg(&ctx, args.timeout_ms, i, &cfgs[j]);
		if (st) {
			amd_qos_active_pools_cleanup(&active);
			free(cfgs);
			amd_qos_ctx_cleanup(&ctx);
			return amd_qos_report_vendor_error(path, args.json, st,
							   "pool telemetry query");
		}

		if (!amd_qos_timestamps_supported(&cap)) {
			cfgs[j].configured_timestamp = 0;
			cfgs[j].query_timestamp = 0;
		}
		j++;
	}

	amd_qos_print_pool_cfg_list(path, args.json, &cap, cfgs, active_count);
	free(cfgs);
	amd_qos_active_pools_cleanup(&active);
	amd_qos_ctx_cleanup(&ctx);
	return 0;
}

int amd_qos_cmd_pools(const char *prog, int argc, char **argv)
{
	return amd_qos_cmd_pools_common(prog, argc, argv, AMD_QOS_POOLS_USAGE,
					false);
}

int amd_qos_cmd_active_pools(const char *prog, int argc, char **argv)
{
	return amd_qos_cmd_pools_common(prog, argc, argv,
					AMD_QOS_ACTIVE_POOLS_USAGE, true);
}

/* ------------------------------------------------------------------ */
/* pool-create / pool-update                                          */
/* ------------------------------------------------------------------ */

static int amd_qos_cmd_pool_create_update_common(const char *prog, int argc,
						 char **argv, bool create)
{
	const char *usage = create ? AMD_QOS_POOL_CREATE_USAGE :
		AMD_QOS_POOL_UPDATE_USAGE;
	const char *operation = create ? "create" : "update";
	struct amd_qos_pool_cli_args args;
	struct amd_qos_ctx ctx;
	struct sdxi_amd_qos_cap cap;
	struct amd_qos_device_tel dev_tel;
	struct amd_qos_active_pools_tel active = { 0 };
	struct amd_qos_pool_cfg cfg;
	struct sdxi_admin_cpl cpl;
	const char *path;
	uint32_t requested_minbw;
	uint32_t effective_minbw;
	uint32_t requested_maxbw;
	uint32_t effective_maxbw;
	uint32_t current_pool_minbw = 0;
	uint8_t pool_op = create ? SDXI_AMD_QOS_POOL_OP_CREATE :
		SDXI_AMD_QOS_POOL_OP_UPDATE;
	bool target_active;
	int rc;
	int st;

	if (!amd_qos_parse_pool_cli_args(prog, usage, argc, argv, &args, &rc))
		return rc;
	if (!args.have_pool || !args.minbw_arg || !args.maxbw_arg) {
		fprintf(stderr, "--pool, --minbw, and --maxbw are required\n");
		amd_qos_print_usage(prog, usage);
		return 1;
	}

	st = amd_qos_open_supported(args.dev_arg, args.json, &ctx);
	if (st)
		return 1;
	path = ctx.path;

	st = amd_qos_query_cap(&ctx, args.timeout_ms, &cap);
	if (st) {
		amd_qos_ctx_cleanup(&ctx);
		return amd_qos_report_vendor_error(path, args.json, st,
						   "capability query");
	}

	st = amd_qos_validate_pool_range(&cap, args.pool_num, !create);
	if (st) {
		amd_qos_ctx_cleanup(&ctx);
		if (args.pool_num == 0 && create)
			fprintf(stderr, "Pool 0 cannot be created\n");
		else
			fprintf(stderr,
				"Pool %" PRIu16 " is out of range (max_pools=%" PRIu16 ")\n",
				args.pool_num, cap.max_pools);
		return 1;
	}

	st = amd_qos_pool_round_bw("minbw", args.minbw_arg,
				   cap.granularity_bw_mb_s, args.json,
				   &requested_minbw, &effective_minbw);
	if (st) {
		amd_qos_ctx_cleanup(&ctx);
		fprintf(stderr, "Invalid --minbw `%s`: %s\n", args.minbw_arg,
			amd_qos_bw_err_str(st));
		return 1;
	}
	st = amd_qos_pool_round_bw("maxbw", args.maxbw_arg,
				   cap.granularity_bw_mb_s, args.json,
				   &requested_maxbw, &effective_maxbw);
	if (st) {
		amd_qos_ctx_cleanup(&ctx);
		fprintf(stderr, "Invalid --maxbw `%s`: %s\n", args.maxbw_arg,
			amd_qos_bw_err_str(st));
		return 1;
	}

	if (effective_minbw > effective_maxbw) {
		amd_qos_ctx_cleanup(&ctx);
		fprintf(stderr,
			"Effective min bandwidth %" PRIu32
			" MB/s exceeds effective max bandwidth %" PRIu32 " MB/s.\n",
			effective_minbw, effective_maxbw);
		return 1;
	}

	if (!args.json)
		(void)amd_qos_pool_warn_cap(&cap, effective_minbw,
					    effective_maxbw);

	st = amd_qos_query_device_telemetry(&ctx, args.timeout_ms, &dev_tel);
	if (st) {
		amd_qos_ctx_cleanup(&ctx);
		return amd_qos_report_vendor_error(path, args.json, st,
						   "device telemetry preflight");
	}

	st = amd_qos_query_active_pools(&ctx, &cap, args.timeout_ms, &active);
	if (st) {
		amd_qos_ctx_cleanup(&ctx);
		return amd_qos_report_vendor_error(path, args.json, st,
						   "active pools query");
	}

	target_active = amd_qos_active_pool_test(&active, args.pool_num);
	st = amd_qos_emit_pool_existence(path, args.json, create,
					 args.pool_num, target_active);
	if (st) {
		amd_qos_active_pools_cleanup(&active);
		amd_qos_ctx_cleanup(&ctx);
		return st;
	}

	if (!create) {
		st = amd_qos_query_pool_cfg(&ctx, args.timeout_ms, args.pool_num,
					    &cfg);
		if (st) {
			amd_qos_active_pools_cleanup(&active);
			amd_qos_ctx_cleanup(&ctx);
			return amd_qos_report_vendor_error(path, args.json, st,
							   "pool telemetry query");
		}
		current_pool_minbw = cfg.pool_minbw_mb_s;
	}

	st = amd_qos_emit_pool_minbw_overflow(path, args.json, create,
					      args.pool_num,
					      dev_tel.device_maxbw_mb_s,
					      dev_tel.total_allocated_bw_mb_s,
					      current_pool_minbw,
					      effective_minbw);
	if (st) {
		amd_qos_active_pools_cleanup(&active);
		amd_qos_ctx_cleanup(&ctx);
		return st;
	}

	amd_qos_active_pools_cleanup(&active);

	if (!create &&
	    !(cap.feature_flags & SDXI_AMD_QOS_FEATURE_DYNAMIC_POOL_RECONFIG) &&
	    !args.json) {
		fprintf(stderr,
			"Warning: Dynamic Pool Reconfiguration is not advertised; stop affected contexts before updating.\n");
	}

	memset(&cpl, 0, sizeof(cpl));
	st = sdxi_amd_qos_pool_update(ctx.dev, args.pool_num, pool_op,
				      effective_maxbw, effective_minbw,
				      args.timeout_ms, &cpl);
	if (st) {
		amd_qos_ctx_cleanup(&ctx);
		return amd_qos_report_vendor_error(path, args.json, st,
						   "pool update");
	}

	st = amd_qos_query_pool_cfg(&ctx, args.timeout_ms, args.pool_num, &cfg);
	amd_qos_ctx_cleanup(&ctx);
	if (st)
		return amd_qos_report_vendor_error(path, args.json, st,
						   "pool telemetry query");

	if (!amd_qos_timestamps_supported(&cap)) {
		cfg.configured_timestamp = 0;
		cfg.query_timestamp = 0;
	}

	if (!args.json &&
	    (cfg.pool_minbw_mb_s != effective_minbw ||
	     cfg.pool_maxbw_mb_s != effective_maxbw)) {
		fprintf(stderr,
			"Warning: device applied min/max %" PRIu32 "/%" PRIu32
			" MB/s after %s request %" PRIu32 "/%" PRIu32 " MB/s.\n",
			cfg.pool_minbw_mb_s, cfg.pool_maxbw_mb_s, operation,
			effective_minbw, effective_maxbw);
	}

	amd_qos_print_pool_update_result(path, args.json, operation, &cap,
					 &cfg, requested_minbw, effective_minbw,
					 requested_maxbw, effective_maxbw,
					 cpl.cst_flags);
	return 0;
}

int amd_qos_cmd_pool_create(const char *prog, int argc, char **argv)
{
	return amd_qos_cmd_pool_create_update_common(prog, argc, argv, true);
}

int amd_qos_cmd_pool_update(const char *prog, int argc, char **argv)
{
	return amd_qos_cmd_pool_create_update_common(prog, argc, argv, false);
}

/* ------------------------------------------------------------------ */
/* pool-destroy                                                       */
/* ------------------------------------------------------------------ */

int amd_qos_cmd_pool_destroy(const char *prog, int argc, char **argv)
{
	struct amd_qos_pool_cli_args args;
	struct amd_qos_ctx ctx;
	struct sdxi_amd_qos_cap cap;
	struct amd_qos_active_pools_tel active = { 0 };
	struct sdxi_admin_cpl cpl;
	const char *path;
	int rc;
	int st;

	if (!amd_qos_parse_pool_cli_args(prog, AMD_QOS_POOL_DESTROY_USAGE, argc,
					 argv, &args, &rc))
		return rc;
	if (!args.have_pool) {
		fprintf(stderr, "--pool is required\n");
		amd_qos_print_usage(prog, AMD_QOS_POOL_DESTROY_USAGE);
		return 1;
	}
	if (args.minbw_arg || args.maxbw_arg) {
		fprintf(stderr,
			"pool-destroy does not accept --minbw or --maxbw\n");
		amd_qos_print_usage(prog, AMD_QOS_POOL_DESTROY_USAGE);
		return 1;
	}

	st = amd_qos_open_supported(args.dev_arg, args.json, &ctx);
	if (st)
		return 1;
	path = ctx.path;

	st = amd_qos_query_cap(&ctx, args.timeout_ms, &cap);
	if (st) {
		amd_qos_ctx_cleanup(&ctx);
		return amd_qos_report_vendor_error(path, args.json, st,
						   "capability query");
	}

	st = amd_qos_validate_pool_range(&cap, args.pool_num, false);
	if (st) {
		amd_qos_ctx_cleanup(&ctx);
		if (args.pool_num == 0)
			fprintf(stderr, "Pool 0 cannot be destroyed\n");
		else
			fprintf(stderr,
				"Pool %" PRIu16 " is out of range (max_pools=%" PRIu16 ")\n",
				args.pool_num, cap.max_pools);
		return 1;
	}

	memset(&cpl, 0, sizeof(cpl));
	st = sdxi_amd_qos_pool_update(ctx.dev, args.pool_num,
				      SDXI_AMD_QOS_POOL_OP_DESTROY, 0, 0,
				      args.timeout_ms, &cpl);
	if (st) {
		amd_qos_ctx_cleanup(&ctx);
		return amd_qos_report_vendor_error(path, args.json, st,
						   "pool update");
	}

	st = amd_qos_query_active_pools(&ctx, &cap, args.timeout_ms, &active);
	amd_qos_ctx_cleanup(&ctx);
	if (st)
		return amd_qos_report_vendor_error(path, args.json, st,
						   "active pools query");

	if (amd_qos_active_pool_test(&active, args.pool_num)) {
		amd_qos_active_pools_cleanup(&active);
		fprintf(stderr,
			"Pool %" PRIu16 " still appears active after destroy.\n",
			args.pool_num);
		return 1;
	}

	if (args.json) {
		bool first = true;
		uint16_t pool;

		printf("{\n");
		printf("  \"dev\": \"%s\",\n", path);
		printf("  \"qos_guid\": \"%s\",\n", AMD_QOS_GUID_STR);
		printf("  \"supported\": true,\n");
		printf("  \"operation\": \"destroy\",\n");
		printf("  \"pool_num\": %" PRIu16 ",\n", args.pool_num);
		printf("  \"cst_flags\": %" PRIu32 ",\n", cpl.cst_flags);
		printf("  \"active_pools\": %" PRIu32 ",\n",
		       active.active_pools);
		printf("  \"active_pool_ids\": [");
		for (pool = 0; pool < active.max_pools; pool++) {
			if (!amd_qos_active_pool_test(&active, pool))
				continue;
			printf("%s%" PRIu16, first ? "" : ", ", pool);
			first = false;
		}
		printf("]\n");
		printf("}\n");
	} else {
		printf("Operation:    destroy\n");
		printf("Destroyed:    pool %" PRIu16 "\n", args.pool_num);
		printf("CST Flags:    0x%08" PRIx32 "\n", cpl.cst_flags);
		amd_qos_print_active_pools(path, false, &cap, &active);
	}

	amd_qos_active_pools_cleanup(&active);
	return 0;
}

/* ------------------------------------------------------------------ */
/* show-assignment                                                    */
/* ------------------------------------------------------------------ */

int amd_qos_cmd_show_assignment(const char *prog, int argc, char **argv)
{
	struct amd_qos_assign_cli_args args;
	struct amd_qos_ctx ctx;
	struct sdxi_amd_qos_cap cap;
	struct amd_qos_assignment asn;
	const char *path;
	int rc;
	int st;

	if (!amd_qos_parse_assign_cli_args(prog, AMD_QOS_SHOW_ASSIGNMENT_USAGE,
					    argc, argv, &args, &rc, false))
		return rc;
	if (args.cxt && args.cxt_start != args.cxt_end) {
		fprintf(stderr,
			"show-assignment requires a single --cxt id (no range)\n");
		amd_qos_print_usage(prog, AMD_QOS_SHOW_ASSIGNMENT_USAGE);
		return 1;
	}

	st = amd_qos_open_supported(args.dev_arg, args.json, &ctx);
	if (st)
		return 1;
	path = ctx.path;

	st = amd_qos_query_cap(&ctx, args.timeout_ms, &cap);
	if (st) {
		amd_qos_ctx_cleanup(&ctx);
		return amd_qos_report_vendor_error(path, args.json, st,
						   "capability query");
	}
	if (args.cxt &&
	    !(cap.feature_flags & SDXI_AMD_QOS_FEATURE_FLEXIBLE_POOL_SCOPE)) {
		amd_qos_ctx_cleanup(&ctx);
		fprintf(stderr,
			"Context-level assignment queries require Flexible Pool Scope support.\n");
		return 1;
	}

	st = amd_qos_query_assignment(&ctx, args.timeout_ms, args.vf,
				      args.vf_num, args.cxt,
				      args.cxt ? args.cxt_start : 0, &asn);
	amd_qos_ctx_cleanup(&ctx);
	if (st)
		return amd_qos_report_vendor_error(path, args.json, st,
						   "assignment query");

	if (!amd_qos_timestamps_supported(&cap)) {
		asn.assigned_timestamp = 0;
		asn.query_timestamp = 0;
	}

	amd_qos_print_assignment(path, args.json, &cap, &asn);
	return 0;
}

/* ------------------------------------------------------------------ */
/* assign                                                             */
/* ------------------------------------------------------------------ */

int amd_qos_cmd_assign(const char *prog, int argc, char **argv)
{
	struct amd_qos_assign_cli_args args;
	struct amd_qos_ctx ctx;
	struct sdxi_amd_qos_cap cap;
	struct amd_qos_active_pools_tel active = { 0 };
	struct sdxi_admin_cpl cpl;
	struct amd_qos_assignment first;
	struct amd_qos_assignment last;
	struct amd_qos_assignment *last_ptr = NULL;
	const char *path;
	int rc;
	int st;

	if (!amd_qos_parse_assign_cli_args(prog, AMD_QOS_ASSIGN_USAGE, argc,
					    argv, &args, &rc, true))
		return rc;

	st = amd_qos_open_supported(args.dev_arg, args.json, &ctx);
	if (st)
		return 1;
	path = ctx.path;

	st = amd_qos_query_cap(&ctx, args.timeout_ms, &cap);
	if (st) {
		amd_qos_ctx_cleanup(&ctx);
		return amd_qos_report_vendor_error(path, args.json, st,
						   "capability query");
	}

	st = amd_qos_validate_pool_range(&cap, args.pool_num, true);
	if (st) {
		amd_qos_ctx_cleanup(&ctx);
		fprintf(stderr,
			"Pool %" PRIu16 " is out of range (max_pools=%" PRIu16 ")\n",
			args.pool_num, cap.max_pools);
		return 1;
	}

	if (args.cxt &&
	    !(cap.feature_flags & SDXI_AMD_QOS_FEATURE_FLEXIBLE_POOL_SCOPE)) {
		amd_qos_ctx_cleanup(&ctx);
		fprintf(stderr,
			"Context-level assignment requires Flexible Pool Scope support.\n");
		return 1;
	}

	st = amd_qos_query_active_pools(&ctx, &cap, args.timeout_ms, &active);
	if (st) {
		amd_qos_ctx_cleanup(&ctx);
		return amd_qos_report_vendor_error(path, args.json, st,
						   "active pools query");
	}

	st = amd_qos_emit_pool_existence(path, args.json, false,
					 args.pool_num,
					 amd_qos_active_pool_test(&active,
								  args.pool_num));
	amd_qos_active_pools_cleanup(&active);
	if (st) {
		amd_qos_ctx_cleanup(&ctx);
		return st;
	}

	if (!(cap.feature_flags & SDXI_AMD_QOS_FEATURE_DYNAMIC_POOL_REASSIGN) &&
	    !args.json) {
		fprintf(stderr,
			"Warning: Dynamic Pool Reassignment is not advertised; stop affected contexts or functions before reassigning.\n");
	}

	memset(&cpl, 0, sizeof(cpl));
	st = sdxi_amd_qos_fn_update(ctx.dev, args.pool_num, args.cxt, args.vf,
				    args.vf_num,
				    args.cxt ? args.cxt_start : 0,
				    args.cxt ? args.cxt_end : 0,
				    args.timeout_ms, &cpl);
	if (st) {
		amd_qos_ctx_cleanup(&ctx);
		return amd_qos_report_vendor_error(path, args.json, st,
						   "assignment update");
	}

	st = amd_qos_query_assignment(&ctx, args.timeout_ms, args.vf,
				      args.vf_num, args.cxt,
				      args.cxt ? args.cxt_start : 0, &first);
	if (st) {
		amd_qos_ctx_cleanup(&ctx);
		return amd_qos_report_vendor_error(path, args.json, st,
						   "assignment query");
	}

	if (args.cxt && args.cxt_end != args.cxt_start) {
		st = amd_qos_query_assignment(&ctx, args.timeout_ms, args.vf,
					      args.vf_num, true,
					      args.cxt_end, &last);
		if (st) {
			amd_qos_ctx_cleanup(&ctx);
			return amd_qos_report_vendor_error(path, args.json, st,
							   "assignment query");
		}
		last_ptr = &last;
	}

	amd_qos_ctx_cleanup(&ctx);

	if (!amd_qos_timestamps_supported(&cap)) {
		first.assigned_timestamp = 0;
		first.query_timestamp = 0;
		if (last_ptr) {
			last_ptr->assigned_timestamp = 0;
			last_ptr->query_timestamp = 0;
		}
	}

	amd_qos_print_assign_result(path, args.json, &cap, &args,
				    cpl.cst_flags, &first, last_ptr);
	return 0;
}
