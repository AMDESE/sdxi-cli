/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * SDXI - Device attribute commands for sdxi-cli.
 *
 * The kernel UAPI exposes one device attribute per ioctl call via
 * SDXI_GET_DEV_ATTR / SDXI_SET_DEV_ATTR (selector + __u64 value), keyed by
 * enum sdxi_dev_attr (GSR, GRP_ID, MAX_BUFFER, MAX_AKEY_SZ, MAX_RKEY_SZ,
 * MAX_CXT). libsdxi mirrors that surface as enum sdxi_dev_attr_id and
 * sdxi_dev_{get,set}_attr(). This file implements the CLI commands on top of
 * that surface.
 *
 * The user-facing commands are `attrs`, `attr`, and `set-attr`.
 * `attrs` lists every selector on the device; `attr` reads one selector.
 */

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

#include <libsdxi.h>

#include "cli_cmd.h"

#define ARRAY_SIZE_OF(x) (sizeof(x) / sizeof((x)[0]))

/*
 * Static table of selectors libsdxi advertises today. The CLI iterates this
 * for whole-device listings and uses it to drive name<->id lookups in
 * argument parsing.
 * libsdxi already exposes name/lookup/writable helpers, but keeping a local
 * iteration order here gives the CLI a deterministic listing without having
 * to walk a sparse enum.
 */
static const enum sdxi_dev_attr_id attr_iter[] = {
	SDXI_DEV_ATTR_ID_GSR,
	SDXI_DEV_ATTR_ID_GRP_ID,
	SDXI_DEV_ATTR_ID_MAX_BUFFER,
	SDXI_DEV_ATTR_ID_MAX_AKEY_SZ,
	SDXI_DEV_ATTR_ID_MAX_RKEY_SZ,
	SDXI_DEV_ATTR_ID_MAX_CXT,
};

/*
 * MMIO_CTL0.fn_gsr is a 2-bit field; the SDXI base spec defines:
 *   0b00 GSRV_RESET, 0b01 GSRV_STOP_SF, 0b10 GSRV_STOP_HD, 0b11 GSRV_ACTIVE
 * Translate the raw value into the symbolic mnemonic the spec uses (RESET,
 * STOP_SF, STOP_HD, ACTIVE). Anything outside the 2-bit range is reported as
 * UNKNOWN so the caller can still see the raw encoding.
 */
static const char *gsr_state_name(uint64_t val)
{
	switch (val & 0x3ULL) {
	case 0: return "RESET";
	case 1: return "STOP_SF";
	case 2: return "STOP_HD";
	case 3: return "ACTIVE";
	}
	return "UNKNOWN";
}

static const char *bytes_fmt(uint64_t bytes, char *buf, size_t len)
{
	static const char *const units[] = {
		"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB",
	};
	size_t unit = 0;
	uint64_t val = bytes;

	if (!buf || len == 0)
		return "";

	while (val >= 1024 && (val % 1024) == 0 &&
	       unit + 1 < ARRAY_SIZE_OF(units)) {
		val /= 1024;
		unit++;
	}

	snprintf(buf, len, "%" PRIu64 " %s", val, units[unit]);
	return buf;
}

static int dev_path_from_arg(const char *device_arg, char *buf, size_t buflen,
			     const char **path_out)
{
	int ret;

	if (!device_arg || device_arg[0] == '\0' || !buf || buflen == 0 ||
	    !path_out)
		return -EINVAL;

	if (strncmp(device_arg, "/dev/", 5) == 0) {
		*path_out = device_arg;
		return 0;
	}

	ret = snprintf(buf, buflen, "/dev/%s", device_arg);
	if (ret < 0 || (size_t)ret >= buflen)
		return -ENAMETOOLONG;

	*path_out = buf;
	return 0;
}

/* SDXI MMIO_{CAP1,CTL2}.max_buffer encoding: bytes = 2^(val+21). */
static uint64_t enc_to_max_buffer_bytes(uint64_t enc)
{
	if (enc > 42)
		return 0;
	return 1ULL << (enc + 21);
}

/* SDXI MMIO_{CAP1,CTL2}.max_{akey,rkey}_sz encoding: entries = 2^(val+8). */
static uint64_t enc_to_key_entries(uint64_t enc)
{
	if (enc > 23)
		return 0;
	return 1ULL << (enc + 8);
}

/* SDXI MMIO_{CAP1,CTL2}.max_cxt encoding: count = val + 1. */
static uint64_t raw_to_max_cxt_count(uint64_t raw)
{
	return raw + 1U;
}

struct attr_print_ctx {
	bool json;
	bool first;
};

/*
 * Emit one attribute either as a human-readable line or as a JSON object
 * element. `valid` is false for selectors the running kernel does not
 * implement (mapped from -EOPNOTSUPP -> -ENOTSUP by libsdxi).
 */
static void emit_attr(struct attr_print_ctx *ctx, enum sdxi_dev_attr_id attr,
		      const char *name, bool valid, uint64_t val, int err)
{
	const bool writable = sdxi_dev_attr_is_writable(attr);
	char decoded[48] = "";
	char num[32];

	if (valid) {
		switch (attr) {
		case SDXI_DEV_ATTR_ID_GSR:
			snprintf(decoded, sizeof(decoded),
				 "%s (enc=0x%02" PRIx64 ")",
				 gsr_state_name(val),
				 (uint64_t)(val & 0xffULL));
			break;
		case SDXI_DEV_ATTR_ID_GRP_ID:
			snprintf(decoded, sizeof(decoded), "0x%08" PRIx64,
				 (uint64_t)(val & 0xffffffffULL));
			break;
		case SDXI_DEV_ATTR_ID_MAX_BUFFER:
			bytes_fmt(enc_to_max_buffer_bytes(val), num,
				  sizeof(num));
			snprintf(decoded, sizeof(decoded), "%s (enc=%" PRIu64 ")",
				 num, val);
			break;
		case SDXI_DEV_ATTR_ID_MAX_AKEY_SZ:
			snprintf(decoded, sizeof(decoded),
				 "%" PRIu64 " entries (enc=%" PRIu64 ")",
				 enc_to_key_entries(val), val);
			break;
		case SDXI_DEV_ATTR_ID_MAX_RKEY_SZ:
			/*
			 * Unlike MAX_AKEY_SZ (which returns the 4-bit
			 * spec encoding from MMIO_CTL2.max_akey_sz), the
			 * kernel exposes MAX_RKEY_SZ as the actual entry
			 * count of the driver-allocated RKey table (or 0
			 * when MMIO_CAP1.rkey_cap is unset).
			 * See drivers/dma/sdxi/ioctl.c:
			 * sdxi_dev_attr_get_max_rkey_sz().
			 */
			if (val == 0)
				snprintf(decoded, sizeof(decoded),
					 "0 entries (rkey unsupported)");
			else
				snprintf(decoded, sizeof(decoded),
					 "%" PRIu64 " entries", val);
			break;
		case SDXI_DEV_ATTR_ID_MAX_CXT:
			snprintf(decoded, sizeof(decoded),
				 "%" PRIu64 " (raw=%" PRIu64 ")",
				 raw_to_max_cxt_count(val), val);
			break;
		default:
			snprintf(decoded, sizeof(decoded), "%" PRIu64, val);
			break;
		}
	}

	if (ctx->json) {
		if (!ctx->first)
			printf(",\n");
		ctx->first = false;
		printf("    {\n");
		printf("      \"name\": \"%s\",\n", name);
		printf("      \"id\": %d,\n", (int)attr);
		printf("      \"writable\": %s,\n",
		       writable ? "true" : "false");
		printf("      \"supported\": %s,\n",
		       valid ? "true" : "false");
		if (valid) {
			printf("      \"value\": %" PRIu64, val);
			if (attr == SDXI_DEV_ATTR_ID_GSR)
				printf(",\n      \"state\": \"%s\"\n",
				       gsr_state_name(val));
			else
				printf("\n");
		} else {
			printf("      \"error\": \"%s\"\n", sdxi_err_str(err));
		}
		printf("    }");
		return;
	}

	if (!valid) {
		printf("  %-12s  unsupported (%s)\n", name, sdxi_err_str(err));
		return;
	}

	printf("  %-12s  %s%s\n", name, decoded,
	       writable ? "" : "  [read-only]");
}

/*
 * Parse an attribute value string. The value is interpreted in attribute-aware
 * units where useful (max_buffer accepts byte sizes with K/M/G/.. suffixes;
 * max_akey_sz / max_rkey_sz accept entry counts; max_cxt accepts a count). For
 * simple selectors the input is parsed as a 64-bit integer (decimal/0x...).
 */
static int parse_u64(const char *s, uint64_t *out)
{
	char *end = NULL;
	unsigned long long v;

	if (!s || !out || s[0] == '\0')
		return -EINVAL;

	errno = 0;
	v = strtoull(s, &end, 0);
	if (errno != 0)
		return -errno;
	if (end == s || *end != '\0')
		return -EINVAL;

	*out = (uint64_t)v;
	return 0;
}

static int parse_u64_bytes(const char *s, uint64_t *bytes_out)
{
	char *end = NULL;
	unsigned long long v;
	uint64_t mult = 1;

	if (!s || !bytes_out || s[0] == '\0')
		return -EINVAL;

	errno = 0;
	v = strtoull(s, &end, 0);
	if (errno != 0)
		return -errno;
	if (end == s)
		return -EINVAL;

	if (*end != '\0') {
		switch (*end) {
		case 'k': case 'K': mult = 1ULL << 10; break;
		case 'm': case 'M': mult = 1ULL << 20; break;
		case 'g': case 'G': mult = 1ULL << 30; break;
		case 't': case 'T': mult = 1ULL << 40; break;
		case 'p': case 'P': mult = 1ULL << 50; break;
		case 'e': case 'E': mult = 1ULL << 60; break;
		case 'b': case 'B': mult = 1; break;
		default: return -EINVAL;
		}

		end++;
		if (*end == 'i' || *end == 'I') {
			end++;
			if (*end == 'b' || *end == 'B')
				end++;
			else
				return -EINVAL;
		} else if (*end == 'b' || *end == 'B') {
			end++;
		}
		if (*end != '\0')
			return -EINVAL;
	}

	if (mult && (uint64_t)v > UINT64_MAX / mult)
		return -ERANGE;

	*bytes_out = (uint64_t)v * mult;
	return 0;
}

static int log2_u64_pow2(uint64_t v, unsigned int *log2_out)
{
	if (!log2_out || v == 0 || (v & (v - 1)) != 0)
		return -EINVAL;
	*log2_out = 63U - (unsigned int)__builtin_clzll(v);
	return 0;
}

/*
 * Convert a user-supplied value string into the raw __u64 the kernel expects
 * for `attr`. Supports the same friendly inputs the previous CLI accepted:
 *   max_buffer    : enc 0..15, or a power-of-two byte size >= 2 MiB
 *   max_akey_sz   : enc 0..15, or a power-of-two entry count >= 256
 *   max_rkey_sz   : same as max_akey_sz (read-only today; rejected earlier)
 *   max_cxt       : context count (>= 1); kernel value is count - 1
 *   gsr / grp_id  : plain integer
 */
static int parse_attr_value(enum sdxi_dev_attr_id attr, const char *s,
			    uint64_t *raw_out, char *errbuf, size_t errlen)
{
	uint64_t v;
	int st;

	if (!s || !raw_out)
		return -EINVAL;

	switch (attr) {
	case SDXI_DEV_ATTR_ID_GSR:
		st = parse_u64(s, &v);
		if (st || v > UINT8_MAX) {
			snprintf(errbuf, errlen, "expected an 8-bit unsigned integer");
			return -EINVAL;
		}
		*raw_out = v;
		return 0;

	case SDXI_DEV_ATTR_ID_GRP_ID:
		st = parse_u64(s, &v);
		if (st || v > UINT32_MAX) {
			snprintf(errbuf, errlen, "expected a 32-bit unsigned integer");
			return -EINVAL;
		}
		*raw_out = v;
		return 0;

	case SDXI_DEV_ATTR_ID_MAX_BUFFER: {
		uint64_t bytes;
		unsigned int log2;

		if (parse_u64(s, &v) == 0 && v <= 15) {
			*raw_out = v;
			return 0;
		}
		st = parse_u64_bytes(s, &bytes);
		if (st) {
			snprintf(errbuf, errlen, "invalid byte size '%s'", s);
			return st;
		}
		if (log2_u64_pow2(bytes, &log2) || log2 < 21) {
			snprintf(errbuf, errlen,
				 "max_buffer must be a power of two and at least 2 MiB");
			return -EINVAL;
		}
		if (log2 - 21 > 15) {
			snprintf(errbuf, errlen,
				 "max_buffer encoding must be 15 or less");
			return -ERANGE;
		}
		*raw_out = log2 - 21;
		return 0;
	}

	case SDXI_DEV_ATTR_ID_MAX_AKEY_SZ: {
		uint64_t entries;
		unsigned int log2;

		if (parse_u64(s, &v) == 0 && v <= 15) {
			*raw_out = v;
			return 0;
		}
		st = parse_u64(s, &entries);
		if (st) {
			snprintf(errbuf, errlen, "invalid entry count '%s'", s);
			return st;
		}
		if (log2_u64_pow2(entries, &log2) || log2 < 8) {
			snprintf(errbuf, errlen,
				 "key size must be a power of two and at least 256 entries");
			return -EINVAL;
		}
		if (log2 - 8 > 15) {
			snprintf(errbuf, errlen,
				 "key size encoding must be 15 or less");
			return -ERANGE;
		}
		*raw_out = log2 - 8;
		return 0;
	}

	case SDXI_DEV_ATTR_ID_MAX_RKEY_SZ:
		/*
		 * Read-only in the current kernel; the writability check in
		 * the SET path rejects this before we reach the parser. If
		 * the driver ever drops the read-only flag, the kernel value
		 * is an entry count (not an encoding), so accept a plain
		 * integer here.
		 */
		st = parse_u64(s, &v);
		if (st) {
			snprintf(errbuf, errlen, "invalid entry count '%s'", s);
			return st;
		}
		*raw_out = v;
		return 0;

	case SDXI_DEV_ATTR_ID_MAX_CXT: {
		st = parse_u64(s, &v);
		if (st) {
			snprintf(errbuf, errlen, "invalid context count '%s'", s);
			return st;
		}
		if (v == 0) {
			snprintf(errbuf, errlen, "max_cxt must be at least 1");
			return -EINVAL;
		}
		if (v > 65536) {
			snprintf(errbuf, errlen, "max_cxt must be 65536 or less");
			return -ERANGE;
		}
		*raw_out = v - 1;
		return 0;
	}

	default:
		snprintf(errbuf, errlen, "unsupported selector");
		return -ENOTSUP;
	}
}

/*
 * Map a libsdxi/CLI errno to a human-readable message for set-attr failures.
 * libsdxi already maps kernel -EOPNOTSUPP to -ENOTSUP for both selector-not-
 * implemented and read-only attributes; surface that consistently here.
 */
static const char *set_attr_err_str(enum sdxi_dev_attr_id attr, int err)
{
	if (err == -ENOTSUP) {
		if (!sdxi_dev_attr_is_writable(attr))
			return "attribute is read-only";
		return "attribute not supported by this kernel";
	}
	return sdxi_err_str(err);
}

/* ----- shared open helper ----- */

static int open_dev_for_arg(const char *device_arg, sdxi_dev_t **dev_out,
			    const char **path_out, char *path_buf,
			    size_t path_buflen)
{
	int st;

	st = dev_path_from_arg(device_arg, path_buf, path_buflen, path_out);
	if (st) {
		fprintf(stderr, "Invalid device path: %s.\n", sdxi_err_str(st));
		return st;
	}

	st = sdxi_dev_open(*path_out, dev_out);
	if (st) {
		fprintf(stderr, "Failed to open %s: %s.\n", *path_out,
			sdxi_err_str(st));
		return st;
	}

	return 0;
}

/* ----- full attribute listing ----- */

static int do_attr_list(const char *prog, const char *device_arg, bool json)
{
	char path_buf[PATH_MAX];
	const char *path = NULL;
	sdxi_dev_t *dev = NULL;
	struct attr_print_ctx ctx = { .json = json, .first = true };
	int st;
	size_t i;

	(void)prog;

	st = open_dev_for_arg(device_arg, &dev, &path, path_buf,
			      sizeof(path_buf));
	if (st)
		return 1;

	if (json) {
		printf("{\n");
		printf("  \"path\": \"%s\",\n", path);
		printf("  \"attrs\": [\n");
	} else {
		printf("Device: %s\n", path);
		printf("Attributes:\n");
	}

	for (i = 0; i < ARRAY_SIZE_OF(attr_iter); i++) {
		enum sdxi_dev_attr_id attr = attr_iter[i];
		const char *name = sdxi_dev_attr_name(attr);
		uint64_t val = 0;
		int gst;

		if (!name)
			continue;

		gst = sdxi_dev_get_attr(dev, attr, &val);
		emit_attr(&ctx, attr, name, gst == 0, val, gst);
	}

	if (json) {
		printf("\n  ]\n}\n");
	}

	sdxi_dev_close(dev);
	return 0;
}

/* ----- single attribute read ----- */

static int do_attr_get(const char *prog, const char *device_arg,
		       const char *attr_name, bool json)
{
	char path_buf[PATH_MAX];
	const char *path = NULL;
	sdxi_dev_t *dev = NULL;
	struct attr_print_ctx ctx = { .json = json, .first = true };
	enum sdxi_dev_attr_id attr;
	uint64_t val = 0;
	int st;

	(void)prog;

	st = sdxi_dev_attr_by_name(attr_name, &attr);
	if (st) {
		fprintf(stderr, "Unknown attribute: '%s'.\n", attr_name);
		return 1;
	}

	st = open_dev_for_arg(device_arg, &dev, &path, path_buf,
			      sizeof(path_buf));
	if (st)
		return 1;

	st = sdxi_dev_get_attr(dev, attr, &val);
	if (json) {
		printf("{\n");
		printf("  \"path\": \"%s\",\n", path);
		printf("  \"attrs\": [\n");
		emit_attr(&ctx, attr, sdxi_dev_attr_name(attr), st == 0, val,
			  st);
		printf("\n  ]\n}\n");
	} else {
		printf("Device: %s\n", path);
		printf("Attributes:\n");
		emit_attr(&ctx, attr, sdxi_dev_attr_name(attr), st == 0, val,
			  st);
	}

	sdxi_dev_close(dev);
	if (st && st != -ENOTSUP)
		return 1;
	return 0;
}

/* ----- single attribute update ----- */

static int do_attr_set(const char *prog, const char *device_arg,
		       const char *attr_name, const char *value_str)
{
	char path_buf[PATH_MAX];
	const char *path = NULL;
	sdxi_dev_t *dev = NULL;
	enum sdxi_dev_attr_id attr;
	uint64_t raw = 0;
	char errbuf[128] = "";
	int st;

	(void)prog;

	st = sdxi_dev_attr_by_name(attr_name, &attr);
	if (st) {
		fprintf(stderr, "Unknown attribute: '%s'.\n", attr_name);
		return 1;
	}

	if (!sdxi_dev_attr_is_writable(attr)) {
		fprintf(stderr, "Attribute '%s' is read-only.\n",
			sdxi_dev_attr_name(attr));
		return 1;
	}

	st = parse_attr_value(attr, value_str, &raw, errbuf, sizeof(errbuf));
	if (st) {
		fprintf(stderr, "Invalid value for %s: %s.\n",
			sdxi_dev_attr_name(attr),
			errbuf[0] ? errbuf : sdxi_err_str(st));
		return 1;
	}

	st = open_dev_for_arg(device_arg, &dev, &path, path_buf,
			      sizeof(path_buf));
	if (st)
		return 1;

	st = sdxi_dev_set_attr(dev, attr, &raw);
	sdxi_dev_close(dev);
	if (st) {
		fprintf(stderr, "Failed to set %s on %s: %s (%s).\n",
			sdxi_dev_attr_name(attr), path, sdxi_err_name(st),
			set_attr_err_str(attr, st));
		if (st == -EPERM || st == -EACCES)
			fprintf(stderr,
				"Hint: setting device attributes usually requires CAP_SYS_ADMIN.\n");
		return 1;
	}

	/*
	 * The kernel echoes back the value actually applied (it may clamp).
	 * Print both the symbolic decoding and the raw value so users can see
	 * any clamping without issuing a follow-up `attr`.
	 */
	{
		struct attr_print_ctx ctx = { .json = false, .first = true };
		printf("Set %s on %s. Applied value:\n",
		       sdxi_dev_attr_name(attr), path);
		emit_attr(&ctx, attr, sdxi_dev_attr_name(attr), true, raw, 0);
	}

	return 0;
}

/* ----- arg dispatch ----- */

static void usage_attrs(const char *prog)
{
	printf("Usage: %s attrs <device> [--json]\n", prog);
	printf("Attrs: gsr | grp_id | max_buffer | max_akey_sz | max_rkey_sz | max_cxt\n");
}

static void usage_attr(const char *prog)
{
	printf("Usage: %s attr <device> <attr> [--json]\n", prog);
	printf("Attrs: gsr | grp_id | max_buffer | max_akey_sz | max_rkey_sz | max_cxt\n");
}

static void usage_set_attr(const char *prog)
{
	printf("Usage: %s set-attr <device> <attr> <value>\n", prog);
	printf("Attrs: gsr | grp_id | max_buffer | max_akey_sz | max_cxt\n");
}

/*
 * Public entry: `sdxi attrs <device> [--json]`.
 */
int sdxi_cmd_attrs(const char *prog, int argc, char **argv)
{
	const char *device_arg = NULL;
	bool json = false;
	int i;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--json") == 0) {
			json = true;
			continue;
		}
		if (strcmp(argv[i], "-h") == 0 ||
		    strcmp(argv[i], "--help") == 0) {
			usage_attrs(prog);
			return 0;
		}
		if (!device_arg) {
			device_arg = argv[i];
			continue;
		}

		fprintf(stderr, "Unknown option or extra argument: '%s'.\n",
			argv[i]);
		usage_attrs(prog);
		return 1;
	}

	if (!device_arg) {
		fprintf(stderr,
			"Missing required argument: <device> (for example, sdxi0 or /dev/sdxi0).\n");
		usage_attrs(prog);
		return 1;
	}

	return do_attr_list(prog, device_arg, json);
}

/*
 * Public entry: `sdxi attr <device> <attr> [--json]`.
 */
int sdxi_cmd_attr(const char *prog, int argc, char **argv)
{
	const char *device_arg = NULL;
	const char *attr_name = NULL;
	bool json = false;
	int i;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--json") == 0) {
			json = true;
			continue;
		}
		if (strcmp(argv[i], "-h") == 0 ||
		    strcmp(argv[i], "--help") == 0) {
			usage_attr(prog);
			return 0;
		}
		if (!device_arg) {
			device_arg = argv[i];
			continue;
		}
		if (!attr_name) {
			attr_name = argv[i];
			continue;
		}

		fprintf(stderr, "Unknown option or extra argument: '%s'.\n",
			argv[i]);
		usage_attr(prog);
		return 1;
	}

	if (!device_arg || !attr_name) {
		fprintf(stderr, "Missing required arguments: <device> and <attr>.\n");
		usage_attr(prog);
		return 1;
	}

	return do_attr_get(prog, device_arg, attr_name, json);
}

/*
 * Public entry: `sdxi set-attr <device> <attr> <value>`.
 */
int sdxi_cmd_set_attr(const char *prog, int argc, char **argv)
{
	const char *device_arg = NULL;
	const char *attr_name = NULL;
	const char *value_str = NULL;
	int i;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0 ||
		    strcmp(argv[i], "--help") == 0) {
			usage_set_attr(prog);
			return 0;
		}
		if (!device_arg) {
			device_arg = argv[i];
			continue;
		}
		if (!attr_name) {
			attr_name = argv[i];
			continue;
		}
		if (!value_str) {
			value_str = argv[i];
			continue;
		}

		fprintf(stderr, "Extra argument: '%s'.\n", argv[i]);
		usage_set_attr(prog);
		return 1;
	}

	if (!device_arg || !attr_name || !value_str) {
		fprintf(stderr, "Missing required arguments: <device>, <attr>, and <value>.\n");
		usage_set_attr(prog);
		return 1;
	}

	return do_attr_set(prog, device_arg, attr_name, value_str);
}
