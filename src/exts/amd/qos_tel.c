/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * AMD QoS - device open/close, capability + telemetry queries, decoders,
 * and a few small validators that are intrinsic to telemetry handling.
 */

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libsdxi.h>

#include "qos_internal.h"

/* ------------------------------------------------------------------ */
/* LE decoders                                                        */
/* ------------------------------------------------------------------ */

static uint32_t amd_qos_le32_dec(const void *p)
{
	const uint8_t *b = p;

	return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
	       ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static uint64_t amd_qos_le64_dec(const void *p)
{
	const uint8_t *b = p;

	return (uint64_t)amd_qos_le32_dec(&b[0]) |
	       ((uint64_t)amd_qos_le32_dec(&b[4]) << 32);
}

/* ------------------------------------------------------------------ */
/* Context lifecycle + supportability checks                          */
/* ------------------------------------------------------------------ */

void amd_qos_active_pools_cleanup(struct amd_qos_active_pools_tel *tel)
{
	if (!tel)
		return;

	free(tel->bitmap);
	tel->bitmap = NULL;
	tel->bitmap_bytes = 0;
}

void amd_qos_ctx_cleanup(struct amd_qos_ctx *ctx)
{
	if (!ctx)
		return;

	if (ctx->dev) {
		sdxi_dev_close(ctx->dev);
		ctx->dev = NULL;
	}
}

static bool amd_qos_has_admin_disc(sdxi_dev_t *dev)
{
	return sdxi_dev_has_uapi_cap(dev, SDXI_UAPI_INFO_CAP_ADMIN_DISC);
}

static bool amd_qos_has_vendor_op(sdxi_dev_t *dev)
{
	return sdxi_dev_has_uapi_cap(dev, SDXI_UAPI_INFO_CAP_VENDOR_OP);
}

static bool amd_qos_is_sdxi_1_1_or_newer(const struct sdxi_dev_caps *caps)
{
	if (!caps)
		return false;

	if (caps->ver_major > 1)
		return true;
	if (caps->ver_major < 1)
		return false;

	return caps->ver_minor >= 1;
}

bool amd_qos_timestamps_supported(const struct sdxi_amd_qos_cap *cap)
{
	return cap &&
	       (cap->feature_flags & SDXI_AMD_QOS_FEATURE_TEL_TIMESTAMPS);
}

int amd_qos_open_supported(const char *dev_arg, bool json,
			   struct amd_qos_ctx *ctx)
{
	int st;

	if (!ctx)
		return 1;

	memset(ctx, 0, sizeof(*ctx));

	st = amd_qos_dev_path_from_arg(dev_arg, ctx->path_buf,
				       sizeof(ctx->path_buf), &ctx->path);
	if (st) {
		fprintf(stderr, "Invalid device path: %s\n", sdxi_err_str(st));
		return 1;
	}

	st = sdxi_dev_open(ctx->path, &ctx->dev);
	if (st) {
		fprintf(stderr, "Failed to open %s: %s\n", ctx->path,
			sdxi_err_str(st));
		return 1;
	}

	st = sdxi_dev_get_caps(ctx->dev, &ctx->caps);
	if (st) {
		fprintf(stderr, "Failed to read device caps for %s: %s\n",
			ctx->path, sdxi_err_str(st));
		goto err;
	}

	if (ctx->caps.is_vf) {
		fprintf(stderr,
			"AMD QoS is PF admin-context only; select a PF device.\n");
		goto err;
	}

	if (!amd_qos_is_sdxi_1_1_or_newer(&ctx->caps)) {
		amd_qos_print_unsupported(ctx->path, json,
					 "requires_sdxi_1_1",
					 "requires SDXI 1.1+ discovery support");
		goto err;
	}

	if (!amd_qos_has_admin_disc(ctx->dev)) {
		amd_qos_print_unsupported(ctx->path, json,
					 "missing_admin_disc_cap",
					 "device does not advertise SDXI_ADMIN_DISC");
		goto err;
	}

	if (!amd_qos_has_vendor_op(ctx->dev)) {
		amd_qos_print_unsupported(ctx->path, json,
					 "missing_vendor_op_cap",
					 "device does not advertise SDXI_ADMIN_VENDOR");
		goto err;
	}

	return 0;

err:
	amd_qos_ctx_cleanup(ctx);
	return 1;
}

int amd_qos_report_vendor_error(const char *path, bool json, int st,
				const char *op_desc)
{
	if (st == -EACCES || st == -EPERM) {
		fprintf(stderr, "AMD QoS %s requires CAP_SYS_ADMIN.\n",
			op_desc);
		return 1;
	}
	if (st == -ENOTSUP) {
		amd_qos_print_unsupported(path, json,
					 "qos_vendor_handler_not_present",
					 "QoS vendor handler not present on this PF");
		return 1;
	}

	fprintf(stderr, "AMD QoS %s failed: %s (%d): %s\n",
		op_desc, sdxi_err_name(st), st, sdxi_err_str(st));
	return 1;
}

/* ------------------------------------------------------------------ */
/* Capability + telemetry queries                                     */
/* ------------------------------------------------------------------ */

int amd_qos_query_cap(struct amd_qos_ctx *ctx, uint32_t timeout_ms,
		      struct sdxi_amd_qos_cap *cap_out)
{
	if (!ctx || !ctx->dev || !cap_out)
		return -EINVAL;

	memset(cap_out, 0, sizeof(*cap_out));
	return sdxi_amd_qos_get_cap(ctx->dev, timeout_ms, cap_out, NULL);
}

static int amd_qos_decode_device_telemetry(struct amd_qos_device_tel *out,
					   const uint8_t buf[SDXI_AMD_QOS_TEL_FIXED_BYTES])
{
	uint32_t hdr;

	if (!out || !buf)
		return -EINVAL;

	hdr = amd_qos_le32_dec(&buf[0]);
	if ((hdr & 0x7u) != SDXI_AMD_QOS_TEL_QTYPE_DEVICE)
		return -EIO;

	memset(out, 0, sizeof(*out));
	out->device_maxbw_mb_s = amd_qos_le32_dec(&buf[4]);
	out->active_pools = amd_qos_le32_dec(&buf[8]) & 0xffffu;
	out->total_allocated_bw_mb_s = amd_qos_le32_dec(&buf[12]);
	out->configured_timestamp = amd_qos_le64_dec(&buf[32]);
	out->query_timestamp = amd_qos_le64_dec(&buf[40]);

	return 0;
}

int amd_qos_query_device_telemetry(struct amd_qos_ctx *ctx,
				   uint32_t timeout_ms,
				   struct amd_qos_device_tel *out)
{
	uint8_t buf[SDXI_AMD_QOS_TEL_FIXED_BYTES];
	struct sdxi_amd_qos_tel_target target;
	size_t buf_bytes = sizeof(buf);
	int st;

	if (!ctx || !ctx->dev || !out)
		return -EINVAL;

	memset(&target, 0, sizeof(target));
	memset(buf, 0, sizeof(buf));

	st = sdxi_amd_qos_tel(ctx->dev, SDXI_AMD_QOS_TEL_QTYPE_DEVICE,
			      &target, buf, &buf_bytes, timeout_ms, NULL);
	if (st)
		return st;
	if (buf_bytes != sizeof(buf))
		return -EIO;

	return amd_qos_decode_device_telemetry(out, buf);
}

bool amd_qos_pool_cfg_active(const struct amd_qos_pool_cfg *cfg)
{
	if (!cfg)
		return false;

	return cfg->pool_num == 0 || cfg->pool_minbw_mb_s != 0 ||
	       cfg->pool_maxbw_mb_s != 0;
}

static int amd_qos_decode_pool_cfg(struct amd_qos_pool_cfg *out,
				   const uint8_t buf[SDXI_AMD_QOS_TEL_FIXED_BYTES])
{
	uint32_t hdr;
	uint32_t counts;

	if (!out || !buf)
		return -EINVAL;

	hdr = amd_qos_le32_dec(&buf[0]);
	if ((hdr & 0x7u) != SDXI_AMD_QOS_TEL_QTYPE_POOL_CONFIG)
		return -EIO;

	memset(out, 0, sizeof(*out));
	out->pool_num = (uint16_t)(amd_qos_le32_dec(&buf[4]) & 0xffffu);
	out->pool_minbw_mb_s = amd_qos_le32_dec(&buf[8]);
	out->pool_maxbw_mb_s = amd_qos_le32_dec(&buf[12]);
	counts = amd_qos_le32_dec(&buf[16]);
	out->assigned_functions = (uint16_t)(counts & 0xffffu);
	out->assigned_contexts = (uint16_t)(counts >> 16);
	out->configured_timestamp = amd_qos_le64_dec(&buf[32]);
	out->query_timestamp = amd_qos_le64_dec(&buf[40]);

	return 0;
}

int amd_qos_query_pool_cfg(struct amd_qos_ctx *ctx, uint32_t timeout_ms,
			   uint16_t pool_num, struct amd_qos_pool_cfg *out)
{
	uint8_t buf[SDXI_AMD_QOS_TEL_FIXED_BYTES];
	struct sdxi_amd_qos_tel_target target;
	size_t buf_bytes = sizeof(buf);
	int st;

	if (!ctx || !ctx->dev || !out)
		return -EINVAL;

	memset(&target, 0, sizeof(target));
	memset(buf, 0, sizeof(buf));
	target.pool_num = pool_num;

	st = sdxi_amd_qos_tel(ctx->dev, SDXI_AMD_QOS_TEL_QTYPE_POOL_CONFIG,
			      &target, buf, &buf_bytes, timeout_ms, NULL);
	if (st)
		return st;
	if (buf_bytes != sizeof(buf))
		return -EIO;

	return amd_qos_decode_pool_cfg(out, buf);
}

int amd_qos_query_active_pools(struct amd_qos_ctx *ctx,
			       const struct sdxi_amd_qos_cap *cap,
			       uint32_t timeout_ms,
			       struct amd_qos_active_pools_tel *out)
{
	struct sdxi_amd_qos_tel_target target;
	size_t buf_bytes;
	uint8_t *buf = NULL;
	int st;

	if (!ctx || !ctx->dev || !cap || !out)
		return -EINVAL;
	if (cap->max_pools == 0)
		return -EIO;

	memset(out, 0, sizeof(*out));
	memset(&target, 0, sizeof(target));

	buf_bytes = sdxi_amd_qos_tel_active_pools_bytes(cap->max_pools);
	if (buf_bytes < SDXI_AMD_QOS_TEL_HDR_BYTES)
		return -EIO;

	buf = calloc(1, buf_bytes);
	if (!buf)
		return -ENOMEM;

	st = sdxi_amd_qos_tel(ctx->dev, SDXI_AMD_QOS_TEL_QTYPE_ACTIVE_POOLS,
			      &target, buf, &buf_bytes, timeout_ms, NULL);
	if (st)
		goto out;
	if (buf_bytes < SDXI_AMD_QOS_TEL_HDR_BYTES) {
		st = -EIO;
		goto out;
	}
	if ((amd_qos_le32_dec(&buf[0]) & 0x7u) !=
	    SDXI_AMD_QOS_TEL_QTYPE_ACTIVE_POOLS) {
		st = -EIO;
		goto out;
	}

	out->max_pools = cap->max_pools;
	out->active_pools = amd_qos_le32_dec(&buf[8]) & 0xffffu;
	out->query_timestamp = amd_qos_le64_dec(&buf[40]);
	out->bitmap_bytes = buf_bytes - SDXI_AMD_QOS_TEL_HDR_BYTES;
	if (out->bitmap_bytes != 0) {
		out->bitmap = malloc(out->bitmap_bytes);
		if (!out->bitmap) {
			st = -ENOMEM;
			goto out;
		}
		memcpy(out->bitmap, buf + SDXI_AMD_QOS_TEL_HDR_BYTES,
		       out->bitmap_bytes);
	}

	st = 0;

out:
	free(buf);
	if (st)
		amd_qos_active_pools_cleanup(out);
	return st;
}

bool amd_qos_active_pool_test(const struct amd_qos_active_pools_tel *tel,
			      uint16_t pool_num)
{
	uint32_t byte;
	uint8_t mask;

	if (!tel || !tel->bitmap)
		return false;
	if (pool_num >= tel->max_pools)
		return false;

	byte = pool_num / 8u;
	mask = (uint8_t)(1u << (pool_num % 8u));
	if (byte >= tel->bitmap_bytes)
		return false;

	return (tel->bitmap[byte] & mask) != 0;
}

static int amd_qos_decode_assignment(struct amd_qos_assignment *out,
				     const uint8_t buf[SDXI_AMD_QOS_TEL_FIXED_BYTES])
{
	uint32_t hdr;
	uint32_t cxt_dw;
	uint32_t target_dw;

	if (!out || !buf)
		return -EINVAL;

	hdr = amd_qos_le32_dec(&buf[0]);
	if ((hdr & 0x7u) != SDXI_AMD_QOS_TEL_QTYPE_POOL_ASSIGNMENT)
		return -EIO;

	cxt_dw = amd_qos_le32_dec(&buf[8]);
	target_dw = amd_qos_le32_dec(&buf[12]);

	memset(out, 0, sizeof(*out));
	out->assigned_pool = (uint16_t)(amd_qos_le32_dec(&buf[4]) & 0xffffu);
	out->cxt = (cxt_dw & 0x1u) != 0;
	out->vf_num = (uint16_t)(target_dw & 0x3ffu);
	out->vf = ((target_dw >> 10) & 0x1u) != 0;
	out->cxt_num = (uint16_t)(target_dw >> 16);
	out->assigned_timestamp = amd_qos_le64_dec(&buf[32]);
	out->query_timestamp = amd_qos_le64_dec(&buf[40]);

	return 0;
}

int amd_qos_query_assignment(struct amd_qos_ctx *ctx, uint32_t timeout_ms,
			     bool vf, uint16_t vf_num,
			     bool cxt, uint16_t cxt_num,
			     struct amd_qos_assignment *out)
{
	uint8_t buf[SDXI_AMD_QOS_TEL_FIXED_BYTES];
	struct sdxi_amd_qos_tel_target target;
	size_t buf_bytes = sizeof(buf);
	int st;

	if (!ctx || !ctx->dev || !out)
		return -EINVAL;

	memset(&target, 0, sizeof(target));
	memset(buf, 0, sizeof(buf));
	target.vf = vf;
	target.vf_num = vf_num;
	target.cxt = cxt;
	target.cxt_num = cxt_num;

	st = sdxi_amd_qos_tel(ctx->dev, SDXI_AMD_QOS_TEL_QTYPE_POOL_ASSIGNMENT,
			      &target, buf, &buf_bytes, timeout_ms, NULL);
	if (st)
		return st;
	if (buf_bytes != sizeof(buf))
		return -EIO;

	return amd_qos_decode_assignment(out, buf);
}

/* ------------------------------------------------------------------ */
/* Validators / pool BW helpers                                       */
/* ------------------------------------------------------------------ */

int amd_qos_validate_pool_range(const struct sdxi_amd_qos_cap *cap,
				uint16_t pool_num, bool allow_zero)
{
	if (!cap)
		return -EINVAL;
	if (!allow_zero && pool_num == 0)
		return -EINVAL;
	if (pool_num >= cap->max_pools)
		return -ERANGE;

	return 0;
}

int amd_qos_pool_round_bw(const char *label, const char *raw,
			  uint32_t granularity_bw, bool json,
			  uint32_t *requested_out, uint32_t *effective_out)
{
	uint32_t requested;
	int st;

	if (!raw || !requested_out || !effective_out)
		return -EINVAL;

	st = amd_qos_parse_bw_mb_s(raw, &requested);
	if (st)
		return st;

	*requested_out = requested;
	*effective_out = requested;
	if (granularity_bw != 0)
		*effective_out -= *effective_out % granularity_bw;

	if (!json && *requested_out != *effective_out) {
		fprintf(stderr,
			"Warning: requested %s %" PRIu32
			" MB/s rounded down to %" PRIu32
			" MB/s to match device granularity %" PRIu32 " MB/s.\n",
			label, *requested_out, *effective_out, granularity_bw);
	}

	return 0;
}

int amd_qos_pool_warn_cap(const struct sdxi_amd_qos_cap *cap,
			  uint32_t effective_minbw, uint32_t effective_maxbw)
{
	if (!cap)
		return 0;

	if (cap->device_maxbw_cap_mb_s != 0 &&
	    effective_minbw > cap->device_maxbw_cap_mb_s) {
		fprintf(stderr,
			"Warning: effective min bandwidth %" PRIu32
			" MB/s exceeds device_maxbw_cap %" PRIu32 " MB/s.\n",
			effective_minbw, cap->device_maxbw_cap_mb_s);
	}
	if (cap->device_maxbw_cap_mb_s != 0 &&
	    effective_maxbw > cap->device_maxbw_cap_mb_s) {
		fprintf(stderr,
			"Warning: effective max bandwidth %" PRIu32
			" MB/s exceeds device_maxbw_cap %" PRIu32 " MB/s.\n",
			effective_maxbw, cap->device_maxbw_cap_mb_s);
	}

	return 0;
}
