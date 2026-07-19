/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * AMD QoS - human + JSON output, plus the three preflight rejection
 * emitters used by the create/update/destroy/assign paths.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <libsdxi.h>

#include "qos_internal.h"

/* ------------------------------------------------------------------ */
/* Small helpers                                                      */
/* ------------------------------------------------------------------ */

const char *amd_qos_ts_units_str(uint32_t units)
{
	switch (units) {
	case 0:
		return "ps";
	case 1:
		return "ns";
	case 2:
		return "us";
	case 3:
		return "ms";
	default:
		return "unknown";
	}
}

void amd_qos_feature_flags_print_human(uint16_t flags)
{
	bool printed = false;

	printf("Features:    ");

	if (flags & SDXI_AMD_QOS_FEATURE_DYNAMIC_POOL_RECONFIG) {
		printf("%sDynamic Pool Reconfiguration", printed ? ", " : "");
		printed = true;
	}
	if (flags & SDXI_AMD_QOS_FEATURE_DYNAMIC_POOL_REASSIGN) {
		printf("%sDynamic Pool Reassignment", printed ? ", " : "");
		printed = true;
	}
	if (flags & SDXI_AMD_QOS_FEATURE_TEL_TIMESTAMPS) {
		printf("%sTimestamp Telemetry Support", printed ? ", " : "");
		printed = true;
	}
	if (flags & SDXI_AMD_QOS_FEATURE_FLEXIBLE_POOL_SCOPE) {
		printf("%sFlexible Pool Scope", printed ? ", " : "");
		printed = true;
	}

	if (!printed)
		printf("(none)");

	printf("\n");
}

/* ------------------------------------------------------------------ */
/* "Not supported" envelope                                           */
/* ------------------------------------------------------------------ */

void amd_qos_print_unsupported(const char *path, bool json,
			       const char *reason_code,
			       const char *reason_msg)
{
	if (json) {
		printf("{\n");
		printf("  \"dev\": \"%s\",\n", path);
		printf("  \"qos_guid\": \"%s\",\n", AMD_QOS_GUID_STR);
		printf("  \"supported\": false,\n");
		printf("  \"reason\": \"%s\"\n", reason_code);
		printf("}\n");
		return;
	}

	printf("Device:      %s\n", path);
	printf("QoS GUID:    %s\n", AMD_QOS_GUID_STR);
	printf("Supported:   no\n");
	printf("Reason:      %s\n", reason_msg);
}

/* ------------------------------------------------------------------ */
/* Device telemetry / set-maxbw                                       */
/* ------------------------------------------------------------------ */

void amd_qos_print_device_telemetry(const char *path, bool json,
				    const struct sdxi_amd_qos_cap *cap,
				    const struct amd_qos_device_tel *tel)
{
	const bool ts_ok = amd_qos_timestamps_supported(cap);
	const uint64_t configured_ts = ts_ok ? tel->configured_timestamp : 0;
	const uint64_t query_ts = ts_ok ? tel->query_timestamp : 0;

	if (json) {
		printf("{\n");
		printf("  \"dev\": \"%s\",\n", path);
		printf("  \"qos_guid\": \"%s\",\n", AMD_QOS_GUID_STR);
		printf("  \"supported\": true,\n");
		printf("  \"device_maxbw\": %" PRIu32 ",\n",
		       tel->device_maxbw_mb_s);
		printf("  \"active_pools\": %" PRIu32 ",\n",
		       tel->active_pools);
		printf("  \"total_allocated_bw\": %" PRIu32 ",\n",
		       tel->total_allocated_bw_mb_s);
		printf("  \"timestamp_supported\": %s,\n",
		       ts_ok ? "true" : "false");
		printf("  \"timestamp_units\": %" PRIu32 ",\n",
		       (uint32_t)cap->timestamp_units);
		printf("  \"timestamp_units_str\": \"%s\",\n",
		       amd_qos_ts_units_str(cap->timestamp_units));
		printf("  \"timestamp_resolution\": %" PRIu32 ",\n",
		       cap->timestamp_resolution);
		printf("  \"configured_timestamp\": %" PRIu64 ",\n",
		       configured_ts);
		printf("  \"query_timestamp\": %" PRIu64 "\n", query_ts);
		printf("}\n");
		return;
	}

	printf("Device:           %s\n", path);
	printf("QoS GUID:         %s\n", AMD_QOS_GUID_STR);
	printf("Supported:        yes\n");
	printf("Device Max BW:    %" PRIu32 " MB/s\n", tel->device_maxbw_mb_s);
	printf("Active Pools:     %" PRIu32 "\n", tel->active_pools);
	printf("Total Alloc BW:   %" PRIu32 " MB/s\n",
	       tel->total_allocated_bw_mb_s);
	if (ts_ok) {
		printf("Configured TS:    %" PRIu64 " %s\n", configured_ts,
		       amd_qos_ts_units_str(cap->timestamp_units));
		printf("Query TS:         %" PRIu64 " %s\n", query_ts,
		       amd_qos_ts_units_str(cap->timestamp_units));
	} else {
		printf("Configured TS:    0\n");
		printf("Query TS:         0\n");
	}
}

void amd_qos_print_device_set_result(const char *path, bool json,
				     const struct sdxi_amd_qos_cap *cap,
				     const struct amd_qos_device_tel *tel,
				     uint32_t requested_maxbw,
				     uint32_t effective_maxbw,
				     uint32_t cst_flags)
{
	const bool ts_ok = amd_qos_timestamps_supported(cap);
	const uint64_t configured_ts = ts_ok ? tel->configured_timestamp : 0;
	const uint64_t query_ts = ts_ok ? tel->query_timestamp : 0;

	if (json) {
		printf("{\n");
		printf("  \"dev\": \"%s\",\n", path);
		printf("  \"qos_guid\": \"%s\",\n", AMD_QOS_GUID_STR);
		printf("  \"supported\": true,\n");
		printf("  \"requested_maxbw\": %" PRIu32 ",\n",
		       requested_maxbw);
		printf("  \"effective_maxbw\": %" PRIu32 ",\n",
		       effective_maxbw);
		printf("  \"rounded_down\": %s,\n",
		       requested_maxbw != effective_maxbw ? "true" : "false");
		printf("  \"device_maxbw_cap\": %" PRIu32 ",\n",
		       cap->device_maxbw_cap_mb_s);
		printf("  \"granularity_bw\": %" PRIu32 ",\n",
		       cap->granularity_bw_mb_s);
		printf("  \"cst_flags\": %" PRIu32 ",\n", cst_flags);
		printf("  \"device_maxbw\": %" PRIu32 ",\n",
		       tel->device_maxbw_mb_s);
		printf("  \"active_pools\": %" PRIu32 ",\n",
		       tel->active_pools);
		printf("  \"total_allocated_bw\": %" PRIu32 ",\n",
		       tel->total_allocated_bw_mb_s);
		printf("  \"timestamp_supported\": %s,\n",
		       ts_ok ? "true" : "false");
		printf("  \"timestamp_units\": %" PRIu32 ",\n",
		       (uint32_t)cap->timestamp_units);
		printf("  \"timestamp_units_str\": \"%s\",\n",
		       amd_qos_ts_units_str(cap->timestamp_units));
		printf("  \"timestamp_resolution\": %" PRIu32 ",\n",
		       cap->timestamp_resolution);
		printf("  \"configured_timestamp\": %" PRIu64 ",\n",
		       configured_ts);
		printf("  \"query_timestamp\": %" PRIu64 "\n", query_ts);
		printf("}\n");
		return;
	}

	printf("Requested Max BW: %" PRIu32 " MB/s\n", requested_maxbw);
	printf("Effective Max BW: %" PRIu32 " MB/s\n", effective_maxbw);
	amd_qos_print_device_telemetry(path, false, cap, tel);
}

/* ------------------------------------------------------------------ */
/* Pool config / pools                                                */
/* ------------------------------------------------------------------ */

void amd_qos_print_pool_cfg(const char *path, bool json,
			    const struct sdxi_amd_qos_cap *cap,
			    const struct amd_qos_pool_cfg *cfg)
{
	const bool ts_ok = amd_qos_timestamps_supported(cap);
	const uint64_t configured_ts = ts_ok ? cfg->configured_timestamp : 0;
	const uint64_t query_ts = ts_ok ? cfg->query_timestamp : 0;
	const bool active = amd_qos_pool_cfg_active(cfg);

	if (json) {
		printf("{\n");
		printf("  \"dev\": \"%s\",\n", path);
		printf("  \"qos_guid\": \"%s\",\n", AMD_QOS_GUID_STR);
		printf("  \"supported\": true,\n");
		printf("  \"pool_num\": %" PRIu16 ",\n", cfg->pool_num);
		printf("  \"active\": %s,\n", active ? "true" : "false");
		printf("  \"pool_minbw\": %" PRIu32 ",\n", cfg->pool_minbw_mb_s);
		printf("  \"pool_maxbw\": %" PRIu32 ",\n", cfg->pool_maxbw_mb_s);
		printf("  \"assigned_functions\": %" PRIu16 ",\n",
		       cfg->assigned_functions);
		printf("  \"assigned_contexts\": %" PRIu16 ",\n",
		       cfg->assigned_contexts);
		printf("  \"timestamp_supported\": %s,\n",
		       ts_ok ? "true" : "false");
		printf("  \"timestamp_units\": %" PRIu32 ",\n",
		       (uint32_t)cap->timestamp_units);
		printf("  \"timestamp_units_str\": \"%s\",\n",
		       amd_qos_ts_units_str(cap->timestamp_units));
		printf("  \"timestamp_resolution\": %" PRIu32 ",\n",
		       cap->timestamp_resolution);
		printf("  \"configured_timestamp\": %" PRIu64 ",\n",
		       configured_ts);
		printf("  \"query_timestamp\": %" PRIu64 "\n", query_ts);
		printf("}\n");
		return;
	}

	printf("Device:             %s\n", path);
	printf("QoS GUID:           %s\n", AMD_QOS_GUID_STR);
	printf("Supported:          yes\n");
	printf("Pool:               %" PRIu16 "\n", cfg->pool_num);
	printf("Active:             %s\n", active ? "yes" : "no");
	printf("Pool Min BW:        %" PRIu32 " MB/s\n", cfg->pool_minbw_mb_s);
	printf("Pool Max BW:        %" PRIu32 " MB/s\n", cfg->pool_maxbw_mb_s);
	printf("Assigned Functions: %" PRIu16 "\n", cfg->assigned_functions);
	printf("Assigned Contexts:  %" PRIu16 "\n", cfg->assigned_contexts);
	if (ts_ok) {
		printf("Configured TS:      %" PRIu64 " %s\n", configured_ts,
		       amd_qos_ts_units_str(cap->timestamp_units));
		printf("Query TS:           %" PRIu64 " %s\n", query_ts,
		       amd_qos_ts_units_str(cap->timestamp_units));
	} else {
		printf("Configured TS:      0\n");
		printf("Query TS:           0\n");
	}
}

void amd_qos_print_pool_cfg_list(const char *path, bool json,
				 const struct sdxi_amd_qos_cap *cap,
				 const struct amd_qos_pool_cfg *cfgs,
				 uint16_t ncfgs)
{
	const bool ts_ok = amd_qos_timestamps_supported(cap);
	uint16_t i;

	if (json) {
		printf("{\n");
		printf("  \"dev\": \"%s\",\n", path);
		printf("  \"qos_guid\": \"%s\",\n", AMD_QOS_GUID_STR);
		printf("  \"supported\": true,\n");
		printf("  \"max_pools\": %" PRIu16 ",\n", cap->max_pools);
		printf("  \"timestamp_supported\": %s,\n",
		       ts_ok ? "true" : "false");
		printf("  \"timestamp_units\": %" PRIu32 ",\n",
		       (uint32_t)cap->timestamp_units);
		printf("  \"timestamp_units_str\": \"%s\",\n",
		       amd_qos_ts_units_str(cap->timestamp_units));
		printf("  \"timestamp_resolution\": %" PRIu32 ",\n",
		       cap->timestamp_resolution);
		printf("  \"pools\": [\n");
		for (i = 0; i < ncfgs; i++) {
			const bool active = amd_qos_pool_cfg_active(&cfgs[i]);
			const uint64_t configured_ts =
				ts_ok ? cfgs[i].configured_timestamp : 0;
			const uint64_t query_ts =
				ts_ok ? cfgs[i].query_timestamp : 0;

			printf("    {\n");
			printf("      \"pool_num\": %" PRIu16 ",\n",
			       cfgs[i].pool_num);
			printf("      \"active\": %s,\n",
			       active ? "true" : "false");
			printf("      \"pool_minbw\": %" PRIu32 ",\n",
			       cfgs[i].pool_minbw_mb_s);
			printf("      \"pool_maxbw\": %" PRIu32 ",\n",
			       cfgs[i].pool_maxbw_mb_s);
			printf("      \"assigned_functions\": %" PRIu16 ",\n",
			       cfgs[i].assigned_functions);
			printf("      \"assigned_contexts\": %" PRIu16 ",\n",
			       cfgs[i].assigned_contexts);
			printf("      \"configured_timestamp\": %" PRIu64 ",\n",
			       configured_ts);
			printf("      \"query_timestamp\": %" PRIu64 "\n",
			       query_ts);
			printf("    }%s\n", i + 1 == ncfgs ? "" : ",");
		}
		printf("  ]\n");
		printf("}\n");
		return;
	}

	printf("Device:   %s\n", path);
	printf("QoS GUID: %s\n", AMD_QOS_GUID_STR);
	printf("Pools:    %" PRIu16 "\n", ncfgs);
	printf("  %-4s %-6s %-12s %-12s %-10s %-10s\n",
	       "Pool", "Active", "Min BW", "Max BW", "Fn", "Cxt");
	printf("  -------------------------------------------------------------\n");
	for (i = 0; i < ncfgs; i++) {
		printf("  %-4" PRIu16 " %-6s %-12" PRIu32 " %-12" PRIu32
		       " %-10" PRIu16 " %-10" PRIu16 "\n",
		       cfgs[i].pool_num,
		       amd_qos_pool_cfg_active(&cfgs[i]) ? "yes" : "no",
		       cfgs[i].pool_minbw_mb_s,
		       cfgs[i].pool_maxbw_mb_s,
		       cfgs[i].assigned_functions,
		       cfgs[i].assigned_contexts);
	}
}

void amd_qos_print_pool_update_result(const char *path, bool json,
				      const char *operation,
				      const struct sdxi_amd_qos_cap *cap,
				      const struct amd_qos_pool_cfg *cfg,
				      uint32_t requested_minbw,
				      uint32_t effective_minbw,
				      uint32_t requested_maxbw,
				      uint32_t effective_maxbw,
				      uint32_t cst_flags)
{
	const bool ts_ok = amd_qos_timestamps_supported(cap);
	const uint64_t configured_ts = ts_ok ? cfg->configured_timestamp : 0;
	const uint64_t query_ts = ts_ok ? cfg->query_timestamp : 0;
	const bool active = amd_qos_pool_cfg_active(cfg);

	if (json) {
		printf("{\n");
		printf("  \"dev\": \"%s\",\n", path);
		printf("  \"qos_guid\": \"%s\",\n", AMD_QOS_GUID_STR);
		printf("  \"supported\": true,\n");
		printf("  \"operation\": \"%s\",\n", operation);
		printf("  \"pool_num\": %" PRIu16 ",\n", cfg->pool_num);
		printf("  \"requested_minbw\": %" PRIu32 ",\n",
		       requested_minbw);
		printf("  \"effective_minbw\": %" PRIu32 ",\n",
		       effective_minbw);
		printf("  \"requested_maxbw\": %" PRIu32 ",\n",
		       requested_maxbw);
		printf("  \"effective_maxbw\": %" PRIu32 ",\n",
		       effective_maxbw);
		printf("  \"rounded_down_minbw\": %s,\n",
		       requested_minbw != effective_minbw ? "true" : "false");
		printf("  \"rounded_down_maxbw\": %s,\n",
		       requested_maxbw != effective_maxbw ? "true" : "false");
		printf("  \"cst_flags\": %" PRIu32 ",\n", cst_flags);
		printf("  \"active\": %s,\n", active ? "true" : "false");
		printf("  \"pool_minbw\": %" PRIu32 ",\n", cfg->pool_minbw_mb_s);
		printf("  \"pool_maxbw\": %" PRIu32 ",\n", cfg->pool_maxbw_mb_s);
		printf("  \"assigned_functions\": %" PRIu16 ",\n",
		       cfg->assigned_functions);
		printf("  \"assigned_contexts\": %" PRIu16 ",\n",
		       cfg->assigned_contexts);
		printf("  \"timestamp_supported\": %s,\n",
		       ts_ok ? "true" : "false");
		printf("  \"timestamp_units\": %" PRIu32 ",\n",
		       (uint32_t)cap->timestamp_units);
		printf("  \"timestamp_units_str\": \"%s\",\n",
		       amd_qos_ts_units_str(cap->timestamp_units));
		printf("  \"timestamp_resolution\": %" PRIu32 ",\n",
		       cap->timestamp_resolution);
		printf("  \"configured_timestamp\": %" PRIu64 ",\n",
		       configured_ts);
		printf("  \"query_timestamp\": %" PRIu64 "\n", query_ts);
		printf("}\n");
		return;
	}

	printf("Operation:          %s\n", operation);
	printf("Requested Min BW:   %" PRIu32 " MB/s\n", requested_minbw);
	printf("Effective Min BW:   %" PRIu32 " MB/s\n", effective_minbw);
	printf("Requested Max BW:   %" PRIu32 " MB/s\n", requested_maxbw);
	printf("Effective Max BW:   %" PRIu32 " MB/s\n", effective_maxbw);
	amd_qos_print_pool_cfg(path, false, cap, cfg);
}

/* ------------------------------------------------------------------ */
/* Active pools                                                       */
/* ------------------------------------------------------------------ */

void amd_qos_print_active_pools(const char *path, bool json,
				const struct sdxi_amd_qos_cap *cap,
				const struct amd_qos_active_pools_tel *tel)
{
	const bool ts_ok = amd_qos_timestamps_supported(cap);
	const uint64_t query_ts = ts_ok ? tel->query_timestamp : 0;
	bool first = true;
	uint16_t pool;

	if (json) {
		printf("{\n");
		printf("  \"dev\": \"%s\",\n", path);
		printf("  \"qos_guid\": \"%s\",\n", AMD_QOS_GUID_STR);
		printf("  \"supported\": true,\n");
		printf("  \"active_pools\": %" PRIu32 ",\n", tel->active_pools);
		printf("  \"timestamp_supported\": %s,\n",
		       ts_ok ? "true" : "false");
		printf("  \"timestamp_units\": %" PRIu32 ",\n",
		       (uint32_t)cap->timestamp_units);
		printf("  \"timestamp_units_str\": \"%s\",\n",
		       amd_qos_ts_units_str(cap->timestamp_units));
		printf("  \"timestamp_resolution\": %" PRIu32 ",\n",
		       cap->timestamp_resolution);
		printf("  \"query_timestamp\": %" PRIu64 ",\n", query_ts);
		printf("  \"active_pool_ids\": [");
		for (pool = 0; pool < tel->max_pools; pool++) {
			if (!amd_qos_active_pool_test(tel, pool))
				continue;
			printf("%s%" PRIu16, first ? "" : ", ", pool);
			first = false;
		}
		printf("]\n");
		printf("}\n");
		return;
	}

	printf("Device:       %s\n", path);
	printf("QoS GUID:     %s\n", AMD_QOS_GUID_STR);
	printf("Supported:    yes\n");
	printf("Active Pools: %" PRIu32 "\n", tel->active_pools);
	printf("Pools:        ");
	for (pool = 0; pool < tel->max_pools; pool++) {
		if (!amd_qos_active_pool_test(tel, pool))
			continue;
		printf("%s%" PRIu16, first ? "" : ", ", pool);
		first = false;
	}
	if (first)
		printf("(none)");
	printf("\n");
	if (ts_ok)
		printf("Query TS:     %" PRIu64 " %s\n", query_ts,
		       amd_qos_ts_units_str(cap->timestamp_units));
}

/* ------------------------------------------------------------------ */
/* Assignment                                                         */
/* ------------------------------------------------------------------ */

void amd_qos_print_assignment(const char *path, bool json,
			      const struct sdxi_amd_qos_cap *cap,
			      const struct amd_qos_assignment *asn)
{
	const bool ts_ok = amd_qos_timestamps_supported(cap);
	const uint64_t assigned_ts = ts_ok ? asn->assigned_timestamp : 0;
	const uint64_t query_ts = ts_ok ? asn->query_timestamp : 0;

	if (json) {
		printf("{\n");
		printf("  \"dev\": \"%s\",\n", path);
		printf("  \"qos_guid\": \"%s\",\n", AMD_QOS_GUID_STR);
		printf("  \"supported\": true,\n");
		printf("  \"assigned_pool\": %" PRIu16 ",\n", asn->assigned_pool);
		printf("  \"function\": \"%s\",\n", asn->vf ? "vf" : "pf");
		printf("  \"vf_num\": %" PRIu16 ",\n", asn->vf_num);
		printf("  \"context\": %s,\n", asn->cxt ? "true" : "false");
		printf("  \"cxt_num\": %" PRIu16 ",\n", asn->cxt_num);
		printf("  \"timestamp_supported\": %s,\n",
		       ts_ok ? "true" : "false");
		printf("  \"timestamp_units\": %" PRIu32 ",\n",
		       (uint32_t)cap->timestamp_units);
		printf("  \"timestamp_units_str\": \"%s\",\n",
		       amd_qos_ts_units_str(cap->timestamp_units));
		printf("  \"timestamp_resolution\": %" PRIu32 ",\n",
		       cap->timestamp_resolution);
		printf("  \"assigned_timestamp\": %" PRIu64 ",\n", assigned_ts);
		printf("  \"query_timestamp\": %" PRIu64 "\n", query_ts);
		printf("}\n");
		return;
	}

	printf("Device:            %s\n", path);
	printf("QoS GUID:          %s\n", AMD_QOS_GUID_STR);
	printf("Supported:         yes\n");
	printf("Assigned Pool:     %" PRIu16 "\n", asn->assigned_pool);
	printf("Target Function:   %s", asn->vf ? "VF" : "PF");
	if (asn->vf)
		printf(" %" PRIu16, asn->vf_num);
	printf("\n");
	printf("Target Scope:      %s\n", asn->cxt ? "context" : "function");
	if (asn->cxt)
		printf("Context:           %" PRIu16 "\n", asn->cxt_num);
	if (ts_ok) {
		printf("Assigned TS:       %" PRIu64 " %s\n", assigned_ts,
		       amd_qos_ts_units_str(cap->timestamp_units));
		printf("Query TS:          %" PRIu64 " %s\n", query_ts,
		       amd_qos_ts_units_str(cap->timestamp_units));
	} else {
		printf("Assigned TS:       0\n");
		printf("Query TS:          0\n");
	}
}

void amd_qos_print_assign_result(const char *path, bool json,
				 const struct sdxi_amd_qos_cap *cap,
				 const struct amd_qos_assign_cli_args *args,
				 uint32_t cst_flags,
				 const struct amd_qos_assignment *first,
				 const struct amd_qos_assignment *last)
{
	const bool ts_ok = amd_qos_timestamps_supported(cap);

	if (json) {
		printf("{\n");
		printf("  \"dev\": \"%s\",\n", path);
		printf("  \"qos_guid\": \"%s\",\n", AMD_QOS_GUID_STR);
		printf("  \"supported\": true,\n");
		printf("  \"operation\": \"assign\",\n");
		printf("  \"requested_pool\": %" PRIu16 ",\n", args->pool_num);
		printf("  \"function\": \"%s\",\n", args->vf ? "vf" : "pf");
		printf("  \"vf_num\": %" PRIu16 ",\n", args->vf_num);
		printf("  \"context\": %s,\n", args->cxt ? "true" : "false");
		printf("  \"cxt_start\": %" PRIu16 ",\n", args->cxt_start);
		printf("  \"cxt_end\": %" PRIu16 ",\n", args->cxt_end);
		printf("  \"cst_flags\": %" PRIu32 ",\n", cst_flags);
		printf("  \"timestamp_supported\": %s,\n",
		       ts_ok ? "true" : "false");
		printf("  \"samples\": [\n");
		printf("    {\n");
		printf("      \"assigned_pool\": %" PRIu16 ",\n", first->assigned_pool);
		printf("      \"function\": \"%s\",\n", first->vf ? "vf" : "pf");
		printf("      \"vf_num\": %" PRIu16 ",\n", first->vf_num);
		printf("      \"context\": %s,\n", first->cxt ? "true" : "false");
		printf("      \"cxt_num\": %" PRIu16 ",\n", first->cxt_num);
		printf("      \"assigned_timestamp\": %" PRIu64 ",\n",
		       ts_ok ? first->assigned_timestamp : 0);
		printf("      \"query_timestamp\": %" PRIu64 "\n",
		       ts_ok ? first->query_timestamp : 0);
		printf("    }%s\n", (last && last != first) ? "," : "");
		if (last && last != first) {
			printf("    {\n");
			printf("      \"assigned_pool\": %" PRIu16 ",\n", last->assigned_pool);
			printf("      \"function\": \"%s\",\n", last->vf ? "vf" : "pf");
			printf("      \"vf_num\": %" PRIu16 ",\n", last->vf_num);
			printf("      \"context\": %s,\n", last->cxt ? "true" : "false");
			printf("      \"cxt_num\": %" PRIu16 ",\n", last->cxt_num);
			printf("      \"assigned_timestamp\": %" PRIu64 ",\n",
			       ts_ok ? last->assigned_timestamp : 0);
			printf("      \"query_timestamp\": %" PRIu64 "\n",
			       ts_ok ? last->query_timestamp : 0);
			printf("    }\n");
		}
		printf("  ]\n");
		printf("}\n");
		return;
	}

	printf("Operation:         assign\n");
	printf("Requested Pool:    %" PRIu16 "\n", args->pool_num);
	printf("Target Function:   %s", args->vf ? "VF" : "PF");
	if (args->vf)
		printf(" %" PRIu16, args->vf_num);
	printf("\n");
	if (!args->cxt) {
		amd_qos_print_assignment(path, false, cap, first);
		return;
	}

	if (args->cxt_start == args->cxt_end) {
		amd_qos_print_assignment(path, false, cap, first);
		return;
	}

	printf("Target Scope:      context range %" PRIu16 ":%" PRIu16 "\n",
	       args->cxt_start, args->cxt_end);
	printf("Sample Start:      cxt %" PRIu16 " -> pool %" PRIu16 "\n",
	       first->cxt_num, first->assigned_pool);
	if (last && last != first) {
		printf("Sample End:        cxt %" PRIu16 " -> pool %" PRIu16 "\n",
		       last->cxt_num, last->assigned_pool);
	}
}

/* ------------------------------------------------------------------ */
/* Preflight rejection emitters                                       */
/* ------------------------------------------------------------------ */

int amd_qos_emit_underallocated_maxbw(const char *path, bool json,
				      uint32_t requested_maxbw,
				      uint32_t effective_maxbw,
				      uint32_t total_allocated_bw)
{
	if (json) {
		printf("{\n");
		printf("  \"dev\": \"%s\",\n", path);
		printf("  \"qos_guid\": \"%s\",\n", AMD_QOS_GUID_STR);
		printf("  \"supported\": true,\n");
		printf("  \"error\": \"requested device_maxbw would undercut active pool guarantees\",\n");
		printf("  \"requested_maxbw\": %" PRIu32 ",\n",
		       requested_maxbw);
		printf("  \"effective_maxbw\": %" PRIu32 ",\n",
		       effective_maxbw);
		printf("  \"total_allocated_bw\": %" PRIu32 "\n",
		       total_allocated_bw);
		printf("}\n");
		return 1;
	}

	fprintf(stderr,
		"Requested device_maxbw %" PRIu32 " MB/s becomes %" PRIu32
		" MB/s after rounding, below total_allocated_bw %" PRIu32 " MB/s.\n",
		requested_maxbw, effective_maxbw, total_allocated_bw);
	fprintf(stderr,
		"Reduce pool minimum bandwidths before lowering device_maxbw.\n");
	return 1;
}

int amd_qos_emit_pool_minbw_overflow(const char *path, bool json,
				     bool create, uint16_t pool_num,
				     uint32_t device_maxbw,
				     uint32_t current_total_minbw,
				     uint32_t current_pool_minbw,
				     uint32_t new_pool_minbw)
{
	uint64_t projected_total = current_total_minbw;

	if (!create)
		projected_total -= current_pool_minbw;
	projected_total += new_pool_minbw;

	if (projected_total <= device_maxbw)
		return 0;

	if (json) {
		printf("{\n");
		printf("  \"dev\": \"%s\",\n", path);
		printf("  \"qos_guid\": \"%s\",\n", AMD_QOS_GUID_STR);
		printf("  \"supported\": true,\n");
		printf("  \"error\": \"pool minimum bandwidths would exceed current device_maxbw\",\n");
		printf("  \"pool_num\": %" PRIu16 ",\n", pool_num);
		printf("  \"device_maxbw\": %" PRIu32 ",\n", device_maxbw);
		printf("  \"current_total_minbw\": %" PRIu32 ",\n",
		       current_total_minbw);
		printf("  \"current_pool_minbw\": %" PRIu32 ",\n",
		       current_pool_minbw);
		printf("  \"requested_pool_minbw\": %" PRIu32 ",\n",
		       new_pool_minbw);
		printf("  \"projected_total_minbw\": %" PRIu64 "\n",
		       projected_total);
		printf("}\n");
		return 1;
	}

	fprintf(stderr,
		"Pool minima would become %" PRIu64 " MB/s, above device_maxbw %" PRIu32 " MB/s.\n",
		projected_total, device_maxbw);
	fprintf(stderr,
		"The sum of all pool minimum bandwidths must fit within current device_maxbw.\n");
	fprintf(stderr,
		"Reduce another pool's minimum bandwidth, or raise device_maxbw first if it is below the device capability.\n");
	return 1;
}

int amd_qos_emit_pool_existence(const char *path, bool json, bool create,
				uint16_t pool_num, bool is_active)
{
	if (create && is_active) {
		if (json) {
			printf("{\n");
			printf("  \"dev\": \"%s\",\n", path);
			printf("  \"qos_guid\": \"%s\",\n", AMD_QOS_GUID_STR);
			printf("  \"supported\": true,\n");
			printf("  \"error\": \"pool already exists\",\n");
			printf("  \"pool_num\": %" PRIu16 "\n", pool_num);
			printf("}\n");
			return 1;
		}

		fprintf(stderr, "Pool %" PRIu16 " already exists.\n", pool_num);
		return 1;
	}

	if (!create && !is_active) {
		if (json) {
			printf("{\n");
			printf("  \"dev\": \"%s\",\n", path);
			printf("  \"qos_guid\": \"%s\",\n", AMD_QOS_GUID_STR);
			printf("  \"supported\": true,\n");
			printf("  \"error\": \"pool is not active\",\n");
			printf("  \"pool_num\": %" PRIu16 "\n", pool_num);
			printf("}\n");
			return 1;
		}

		fprintf(stderr, "Pool %" PRIu16 " is not active.\n", pool_num);
		return 1;
	}

	return 0;
}
