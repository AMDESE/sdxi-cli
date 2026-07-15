/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * AMD QoS internal definitions shared across qos_args.c, qos_tel.c,
 * qos_print.c, and qos_cmd.c. Not exported outside the AMD extension.
 */

#ifndef SDXI_CLI_AMD_QOS_INTERNAL_H
#define SDXI_CLI_AMD_QOS_INTERNAL_H

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <libsdxi.h>
#include <sdxi/vendor/amd_qos.h>

/* Shared identifiers/constants */
#define AMD_QOS_GUID_STR "f5b7610b-0dc6-4c03-ab79-65f1f4efcf4c"
#define AMD_QOS_GLOBAL_OPTS_HINT "Also accepts: [--timeout-ms <n>]"

/* Per-command usage strings (kept short so help is scannable). */
#define AMD_QOS_POOL_CREATE_USAGE \
	"amd qos pool-create <device> --pool <n> --minbw <bw> --maxbw <bw> [--json]"
#define AMD_QOS_POOL_UPDATE_USAGE \
	"amd qos pool-update <device> --pool <n> --minbw <bw> --maxbw <bw> [--json]"
#define AMD_QOS_POOL_DESTROY_USAGE \
	"amd qos pool-destroy <device> --pool <n> [--json]"
#define AMD_QOS_POOL_SHOW_USAGE \
	"amd qos pool-show <device> --pool <n> [--json]"
#define AMD_QOS_POOLS_USAGE \
	"amd qos pools <device> [--json]"
#define AMD_QOS_ACTIVE_POOLS_USAGE \
	"amd qos active-pools <device> [--json]"
#define AMD_QOS_ASSIGN_USAGE \
	"amd qos assign <device> --pool <n> [--vf <n>] [--cxt <id>[:<id>]] [--json]"
#define AMD_QOS_SHOW_ASSIGNMENT_USAGE \
	"amd qos show-assignment <device> [--vf <n>] [--cxt <id>] [--json]"
#define AMD_QOS_CAP_USAGE \
	"amd qos cap <device> [--json]"
#define AMD_QOS_STATUS_USAGE \
	"amd qos status <device> [--json]"
#define AMD_QOS_SET_MAXBW_USAGE \
	"amd qos set-maxbw <device> <bw> [--json]"

/* Shared runtime state */
struct amd_qos_ctx {
	char path_buf[PATH_MAX];
	const char *path;
	sdxi_dev_t *dev;
	struct sdxi_dev_caps caps;
};

/* Decoded telemetry payloads */
struct amd_qos_device_tel {
	uint32_t device_maxbw_mb_s;
	uint32_t active_pools;
	uint32_t total_allocated_bw_mb_s;
	uint64_t configured_timestamp;
	uint64_t query_timestamp;
};

struct amd_qos_pool_cfg {
	uint16_t pool_num;
	uint16_t assigned_functions;
	uint16_t assigned_contexts;
	uint32_t pool_minbw_mb_s;
	uint32_t pool_maxbw_mb_s;
	uint64_t configured_timestamp;
	uint64_t query_timestamp;
};

struct amd_qos_active_pools_tel {
	uint16_t max_pools;
	uint32_t active_pools;
	uint64_t query_timestamp;
	size_t bitmap_bytes;
	uint8_t *bitmap;
};

struct amd_qos_assignment {
	uint16_t assigned_pool;
	bool cxt;
	bool vf;
	uint16_t vf_num;
	uint16_t cxt_num;
	uint64_t assigned_timestamp;
	uint64_t query_timestamp;
};

/* Parsed CLI arg structures */

/*
 * Options understood by amd_qos_parse_simple_cli_args. Selecting
 * AMD_QOS_OPT_MAXBW enables a positional <bw> argument after <device>.
 */
enum amd_qos_simple_opt {
	AMD_QOS_OPT_NONE   = 0,
	AMD_QOS_OPT_MAXBW  = 1u << 0,
};

struct amd_qos_simple_args {
	const char *dev_arg;
	const char *maxbw_arg;
	bool json;
	uint32_t timeout_ms;
};

struct amd_qos_pool_cli_args {
	const char *dev_arg;
	const char *minbw_arg;
	const char *maxbw_arg;
	bool json;
	bool have_pool;
	uint16_t pool_num;
	uint32_t timeout_ms;
};

struct amd_qos_assign_cli_args {
	const char *dev_arg;
	bool json;
	bool have_pool;
	bool vf;
	bool cxt;
	uint16_t pool_num;
	uint16_t vf_num;
	uint16_t cxt_start;
	uint16_t cxt_end;
	uint32_t timeout_ms;
};

/* qos_args.c: scalar value parsers + CLI parsers */
int amd_qos_parse_u32(const char *s, uint32_t *out);
int amd_qos_parse_u16_str(const char *s, uint16_t *out);
int amd_qos_parse_pool_num(const char *s, uint16_t *out);
int amd_qos_parse_cxt_arg(const char *s, uint16_t *start_out, uint16_t *end_out);
int amd_qos_parse_bw_mb_s(const char *s, uint32_t *out);
const char *amd_qos_bw_err_str(int err);
int amd_qos_dev_path_from_arg(const char *device_arg, char *buf, size_t buflen,
			      const char **path_out);

void amd_qos_print_usage(const char *prog, const char *usage);

bool amd_qos_parse_simple_cli_args(const char *prog, const char *usage,
				   int argc, char **argv,
				   struct amd_qos_simple_args *out,
				   int *rc_out, unsigned int opts);
bool amd_qos_parse_pool_cli_args(const char *prog, const char *usage,
				 int argc, char **argv,
				 struct amd_qos_pool_cli_args *out,
				 int *rc_out);
bool amd_qos_parse_assign_cli_args(const char *prog, const char *usage,
				   int argc, char **argv,
				   struct amd_qos_assign_cli_args *out,
				   int *rc_out, bool require_pool);

/* qos_tel.c: device open/close, capability + telemetry queries, validators */
void amd_qos_ctx_cleanup(struct amd_qos_ctx *ctx);
int  amd_qos_open_supported(const char *dev_arg, bool json,
			    struct amd_qos_ctx *ctx);
int  amd_qos_report_vendor_error(const char *path, bool json, int st,
				 const char *op_desc);

bool amd_qos_timestamps_supported(const struct sdxi_amd_qos_cap *cap);

int  amd_qos_query_cap(struct amd_qos_ctx *ctx, uint32_t timeout_ms,
		       struct sdxi_amd_qos_cap *cap_out);
int  amd_qos_query_device_telemetry(struct amd_qos_ctx *ctx,
				    uint32_t timeout_ms,
				    struct amd_qos_device_tel *out);
int  amd_qos_query_pool_cfg(struct amd_qos_ctx *ctx, uint32_t timeout_ms,
			    uint16_t pool_num, struct amd_qos_pool_cfg *out);
int  amd_qos_query_active_pools(struct amd_qos_ctx *ctx,
				const struct sdxi_amd_qos_cap *cap,
				uint32_t timeout_ms,
				struct amd_qos_active_pools_tel *out);
int  amd_qos_query_assignment(struct amd_qos_ctx *ctx, uint32_t timeout_ms,
			      bool vf, uint16_t vf_num,
			      bool cxt, uint16_t cxt_num,
			      struct amd_qos_assignment *out);

void amd_qos_active_pools_cleanup(struct amd_qos_active_pools_tel *tel);
bool amd_qos_active_pool_test(const struct amd_qos_active_pools_tel *tel,
			      uint16_t pool_num);
bool amd_qos_pool_cfg_active(const struct amd_qos_pool_cfg *cfg);

int  amd_qos_validate_pool_range(const struct sdxi_amd_qos_cap *cap,
				 uint16_t pool_num, bool allow_zero);
int  amd_qos_pool_round_bw(const char *label, const char *raw,
			   uint32_t granularity_bw, bool json,
			   uint32_t *requested_out, uint32_t *effective_out);
int  amd_qos_pool_warn_cap(const struct sdxi_amd_qos_cap *cap,
			   uint32_t effective_minbw, uint32_t effective_maxbw);

/* qos_print.c: human + JSON output, plus preflight rejection messages */
const char *amd_qos_ts_units_str(uint32_t units);
void amd_qos_feature_flags_print_human(uint16_t flags);

void amd_qos_print_unsupported(const char *path, bool json,
			       const char *reason_code,
			       const char *reason_msg);

void amd_qos_print_device_telemetry(const char *path, bool json,
				    const struct sdxi_amd_qos_cap *cap,
				    const struct amd_qos_device_tel *tel);
void amd_qos_print_device_set_result(const char *path, bool json,
				     const struct sdxi_amd_qos_cap *cap,
				     const struct amd_qos_device_tel *tel,
				     uint32_t requested_maxbw,
				     uint32_t effective_maxbw,
				     uint32_t cst_flags);
void amd_qos_print_pool_cfg(const char *path, bool json,
			    const struct sdxi_amd_qos_cap *cap,
			    const struct amd_qos_pool_cfg *cfg);
void amd_qos_print_pool_cfg_list(const char *path, bool json,
				 const struct sdxi_amd_qos_cap *cap,
				 const struct amd_qos_pool_cfg *cfgs,
				 uint16_t ncfgs);
void amd_qos_print_pool_update_result(const char *path, bool json,
				      const char *operation,
				      const struct sdxi_amd_qos_cap *cap,
				      const struct amd_qos_pool_cfg *cfg,
				      uint32_t requested_minbw,
				      uint32_t effective_minbw,
				      uint32_t requested_maxbw,
				      uint32_t effective_maxbw,
				      uint32_t cst_flags);
void amd_qos_print_active_pools(const char *path, bool json,
				const struct sdxi_amd_qos_cap *cap,
				const struct amd_qos_active_pools_tel *tel);
void amd_qos_print_assignment(const char *path, bool json,
			      const struct sdxi_amd_qos_cap *cap,
			      const struct amd_qos_assignment *asn);
void amd_qos_print_assign_result(const char *path, bool json,
				 const struct sdxi_amd_qos_cap *cap,
				 const struct amd_qos_assign_cli_args *args,
				 uint32_t cst_flags,
				 const struct amd_qos_assignment *first,
				 const struct amd_qos_assignment *last);

int amd_qos_emit_underallocated_maxbw(const char *path, bool json,
				      uint32_t requested_maxbw,
				      uint32_t effective_maxbw,
				      uint32_t total_allocated_bw);
int amd_qos_emit_pool_minbw_overflow(const char *path, bool json,
				     bool create, uint16_t pool_num,
				     uint32_t device_maxbw,
				     uint32_t current_total_minbw,
				     uint32_t current_pool_minbw,
				     uint32_t new_pool_minbw);
int amd_qos_emit_pool_existence(const char *path, bool json, bool create,
				uint16_t pool_num, bool is_active);

/* qos_cmd.c: per-command handlers; referenced from amd.c registration */
int amd_qos_cmd_cap(const char *prog, int argc, char **argv);
int amd_qos_cmd_status(const char *prog, int argc, char **argv);
int amd_qos_cmd_set_maxbw(const char *prog, int argc, char **argv);
int amd_qos_cmd_pool_show(const char *prog, int argc, char **argv);
int amd_qos_cmd_pools(const char *prog, int argc, char **argv);
int amd_qos_cmd_active_pools(const char *prog, int argc, char **argv);
int amd_qos_cmd_pool_create(const char *prog, int argc, char **argv);
int amd_qos_cmd_pool_update(const char *prog, int argc, char **argv);
int amd_qos_cmd_pool_destroy(const char *prog, int argc, char **argv);
int amd_qos_cmd_assign(const char *prog, int argc, char **argv);
int amd_qos_cmd_show_assignment(const char *prog, int argc, char **argv);

#endif /* SDXI_CLI_AMD_QOS_INTERNAL_H */
