/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * AMD QoS - argument parsing helpers and the three CLI parsers.
 */

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <libsdxi.h>

#include "qos_internal.h"

/* ------------------------------------------------------------------ */
/* Scalar parsers                                                     */
/* ------------------------------------------------------------------ */

int amd_qos_parse_u32(const char *s, uint32_t *out)
{
	char *end = NULL;
	unsigned long v;

	if (!s || !out || s[0] == '\0')
		return -EINVAL;

	errno = 0;
	v = strtoul(s, &end, 0);
	if (errno != 0)
		return -errno;
	if (end == s || *end != '\0')
		return -EINVAL;
	if (v > UINT32_MAX)
		return -ERANGE;

	*out = (uint32_t)v;
	return 0;
}

int amd_qos_parse_u16_str(const char *s, uint16_t *out)
{
	uint32_t v;
	int st;

	if (!s || !out)
		return -EINVAL;

	st = amd_qos_parse_u32(s, &v);
	if (st)
		return st;
	if (v > UINT16_MAX)
		return -ERANGE;

	*out = (uint16_t)v;
	return 0;
}

int amd_qos_parse_pool_num(const char *s, uint16_t *out)
{
	uint32_t v;
	int st;

	if (!s || !out)
		return -EINVAL;

	st = amd_qos_parse_u32(s, &v);
	if (st)
		return st;
	if (v > 0x0fffu)
		return -ERANGE;

	*out = (uint16_t)v;
	return 0;
}

/*
 * Parse a context selector of the form "<id>" or "<start>:<end>".
 * On success: *start_out == *end_out for a single id, or the inclusive
 * range bounds for a colon-delimited range.
 */
int amd_qos_parse_cxt_arg(const char *s, uint16_t *start_out,
			  uint16_t *end_out)
{
	const char *sep;
	char prefix[16];
	size_t prefix_len;
	int st;

	if (!s || !start_out || !end_out)
		return -EINVAL;

	sep = strchr(s, ':');
	if (!sep) {
		st = amd_qos_parse_u16_str(s, start_out);
		if (st)
			return st;
		*end_out = *start_out;
		return 0;
	}

	prefix_len = (size_t)(sep - s);
	if (prefix_len == 0 || prefix_len >= sizeof(prefix))
		return -EINVAL;

	memcpy(prefix, s, prefix_len);
	prefix[prefix_len] = '\0';

	st = amd_qos_parse_u16_str(prefix, start_out);
	if (st)
		return st;
	st = amd_qos_parse_u16_str(sep + 1, end_out);
	if (st)
		return st;
	if (*start_out > *end_out)
		return -EINVAL;

	return 0;
}

static bool amd_qos_has_suffix(const char *s, const char *suffix,
			       size_t *prefix_len)
{
	size_t slen;
	size_t ulen;

	if (!s || !suffix || !prefix_len)
		return false;

	slen = strlen(s);
	ulen = strlen(suffix);
	if (slen < ulen)
		return false;
	if (strcasecmp(s + slen - ulen, suffix) != 0)
		return false;

	*prefix_len = slen - ulen;
	return true;
}

int amd_qos_parse_bw_mb_s(const char *s, uint32_t *out)
{
	char numbuf[64];
	size_t num_len = 0;
	double value;
	double mb_s;
	char *end = NULL;
	bool gb_units = false;

	if (!s || !out || s[0] == '\0')
		return -EINVAL;

	if (amd_qos_has_suffix(s, "gb/s", &num_len) ||
	    amd_qos_has_suffix(s, "gbps", &num_len) ||
	    amd_qos_has_suffix(s, "gb", &num_len) ||
	    amd_qos_has_suffix(s, "g", &num_len)) {
		gb_units = true;
	} else if (amd_qos_has_suffix(s, "mb/s", &num_len) ||
		   amd_qos_has_suffix(s, "mbps", &num_len) ||
		   amd_qos_has_suffix(s, "mb", &num_len) ||
		   amd_qos_has_suffix(s, "m", &num_len)) {
		gb_units = false;
	} else {
		num_len = strlen(s);
	}

	if (num_len == 0 || num_len >= sizeof(numbuf))
		return -EINVAL;

	memcpy(numbuf, s, num_len);
	numbuf[num_len] = '\0';

	if (!gb_units && strpbrk(numbuf, ".eE") != NULL)
		return -EINVAL;

	errno = 0;
	value = strtod(numbuf, &end);
	if (errno != 0)
		return -errno;
	if (end == numbuf || *end != '\0' || !isfinite(value) ||
	    value < 0.0)
		return -EINVAL;

	mb_s = gb_units ? value * 1000.0 : value;
	if (mb_s > (double)UINT32_MAX)
		return -ERANGE;

	if (gb_units)
		mb_s += 0.5;

	*out = (uint32_t)mb_s;
	return 0;
}

const char *amd_qos_bw_err_str(int err)
{
	if (err == -ERANGE)
		return "value out of range";

	return "use integer MB/s or decimal GB/s";
}

int amd_qos_dev_path_from_arg(const char *device_arg, char *buf, size_t buflen,
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

/* ------------------------------------------------------------------ */
/* Usage helper                                                       */
/* ------------------------------------------------------------------ */

void amd_qos_print_usage(const char *prog, const char *usage)
{
	printf("Usage: %s %s\n", prog, usage);
	printf("       (%s)\n", AMD_QOS_GLOBAL_OPTS_HINT);
}

/* ------------------------------------------------------------------ */
/* Shared option-token handling                                       */
/* ------------------------------------------------------------------ */

/*
 * Parse one of the universally-accepted option tokens:
 *   --json
 *   --timeout-ms <n>
 *   -h / --help
 * plus the trailing positional <device>.
 *
 * Returns one of:
 *   1  - matched and consumed; *idx_io advanced; caller continues
 *   0  - matched -h/--help; caller should print usage and return success
 *  -1  - matched an unknown/malformed token; caller should report the
 *        error appropriately
 *  -2  - matched but missing argument or duplicated; error already
 *        printed; caller should set *rc_out = 1 and return false
 */
static int amd_qos_consume_common(const char *prog, const char *usage,
				  int argc, char **argv, int *idx_io,
				  bool *json_io, uint32_t *timeout_io,
				  const char **dev_io)
{
	int i = *idx_io;
	const char *tok = argv[i];
	int st;

	if (strcmp(tok, "--json") == 0) {
		*json_io = true;
		*idx_io = i + 1;
		return 1;
	}
	if (strcmp(tok, "--timeout-ms") == 0) {
		if (i + 1 >= argc) {
			fprintf(stderr, "Missing argument for --timeout-ms\n");
			return -2;
		}
		st = amd_qos_parse_u32(argv[i + 1], timeout_io);
		if (st) {
			fprintf(stderr, "Invalid --timeout-ms: %s\n",
				sdxi_err_str(st));
			return -2;
		}
		*idx_io = i + 2;
		return 1;
	}
	if (strcmp(tok, "-h") == 0 || strcmp(tok, "--help") == 0) {
		amd_qos_print_usage(prog, usage);
		return 0;
	}
	if (tok[0] != '-' && !*dev_io) {
		*dev_io = tok;
		*idx_io = i + 1;
		return 1;
	}

	return -1;
}

/* ------------------------------------------------------------------ */
/* Simple parser: <device> [<bw>] [--json] [--timeout-ms <n>]        */
/* ------------------------------------------------------------------ */

bool amd_qos_parse_simple_cli_args(const char *prog, const char *usage,
				   int argc, char **argv,
				   struct amd_qos_simple_args *out,
				   int *rc_out, unsigned int opts)
{
	int i;

	if (!out || !rc_out)
		return false;

	memset(out, 0, sizeof(*out));
	out->timeout_ms = SDXI_ADMIN_TIMEOUT_DEFAULT;

	for (i = 0; i < argc; ) {
		int st = amd_qos_consume_common(prog, usage, argc, argv, &i,
						&out->json, &out->timeout_ms,
						&out->dev_arg);
		if (st == 1)
			continue;
		if (st == 0) {
			*rc_out = 0;
			return false;
		}
		if (st == -2) {
			*rc_out = 1;
			return false;
		}

		/* Command-specific positional argument. */
		if ((opts & AMD_QOS_OPT_MAXBW) && argv[i][0] != '-' &&
		    out->dev_arg && !out->maxbw_arg) {
			out->maxbw_arg = argv[i];
			i++;
			continue;
		}

		fprintf(stderr, "Unknown option or extra argument `%s`\n",
			argv[i]);
		amd_qos_print_usage(prog, usage);
		*rc_out = 1;
		return false;
	}

	if (!out->dev_arg) {
		fprintf(stderr,
			"Device name is required (e.g., sdxi0 or /dev/sdxi0)\n");
		amd_qos_print_usage(prog, usage);
		*rc_out = 1;
		return false;
	}
	if ((opts & AMD_QOS_OPT_MAXBW) && !out->maxbw_arg) {
		fprintf(stderr, "Bandwidth argument is required\n");
		amd_qos_print_usage(prog, usage);
		*rc_out = 1;
		return false;
	}

	return true;
}

/* ------------------------------------------------------------------ */
/* Pool parser                                                        */
/* ------------------------------------------------------------------ */

bool amd_qos_parse_pool_cli_args(const char *prog, const char *usage,
				 int argc, char **argv,
				 struct amd_qos_pool_cli_args *out,
				 int *rc_out)
{
	int i;

	if (!out || !rc_out)
		return false;

	memset(out, 0, sizeof(*out));
	out->timeout_ms = SDXI_ADMIN_TIMEOUT_DEFAULT;

	for (i = 0; i < argc; ) {
		int st = amd_qos_consume_common(prog, usage, argc, argv, &i,
						&out->json, &out->timeout_ms,
						&out->dev_arg);
		if (st == 1)
			continue;
		if (st == 0) {
			*rc_out = 0;
			return false;
		}
		if (st == -2) {
			*rc_out = 1;
			return false;
		}

		if (strcmp(argv[i], "--pool") == 0) {
			int prc;

			if (i + 1 >= argc) {
				fprintf(stderr, "Missing argument for --pool\n");
				*rc_out = 1;
				return false;
			}
			if (out->have_pool) {
				fprintf(stderr, "--pool specified more than once\n");
				*rc_out = 1;
				return false;
			}
			prc = amd_qos_parse_pool_num(argv[i + 1], &out->pool_num);
			if (prc) {
				fprintf(stderr, "Invalid --pool `%s`: %s\n",
					argv[i + 1], sdxi_err_str(prc));
				*rc_out = 1;
				return false;
			}
			out->have_pool = true;
			i += 2;
			continue;
		}
		if (strcmp(argv[i], "--minbw") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "Missing argument for --minbw\n");
				*rc_out = 1;
				return false;
			}
			if (out->minbw_arg) {
				fprintf(stderr, "--minbw specified more than once\n");
				*rc_out = 1;
				return false;
			}
			out->minbw_arg = argv[i + 1];
			i += 2;
			continue;
		}
		if (strcmp(argv[i], "--maxbw") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "Missing argument for --maxbw\n");
				*rc_out = 1;
				return false;
			}
			if (out->maxbw_arg) {
				fprintf(stderr, "--maxbw specified more than once\n");
				*rc_out = 1;
				return false;
			}
			out->maxbw_arg = argv[i + 1];
			i += 2;
			continue;
		}

		fprintf(stderr, "Unknown option or extra argument `%s`\n",
			argv[i]);
		amd_qos_print_usage(prog, usage);
		*rc_out = 1;
		return false;
	}

	if (!out->dev_arg) {
		fprintf(stderr,
			"Device name is required (e.g., sdxi0 or /dev/sdxi0)\n");
		amd_qos_print_usage(prog, usage);
		*rc_out = 1;
		return false;
	}

	return true;
}

/* ------------------------------------------------------------------ */
/* Assign parser                                                      */
/* ------------------------------------------------------------------ */

bool amd_qos_parse_assign_cli_args(const char *prog, const char *usage,
				   int argc, char **argv,
				   struct amd_qos_assign_cli_args *out,
				   int *rc_out, bool require_pool)
{
	int i;

	if (!out || !rc_out)
		return false;

	memset(out, 0, sizeof(*out));
	out->timeout_ms = SDXI_ADMIN_TIMEOUT_DEFAULT;

	for (i = 0; i < argc; ) {
		int st = amd_qos_consume_common(prog, usage, argc, argv, &i,
						&out->json, &out->timeout_ms,
						&out->dev_arg);
		if (st == 1)
			continue;
		if (st == 0) {
			*rc_out = 0;
			return false;
		}
		if (st == -2) {
			*rc_out = 1;
			return false;
		}

		if (strcmp(argv[i], "--pool") == 0) {
			int prc;

			if (i + 1 >= argc) {
				fprintf(stderr, "Missing argument for --pool\n");
				*rc_out = 1;
				return false;
			}
			if (out->have_pool) {
				fprintf(stderr, "--pool specified more than once\n");
				*rc_out = 1;
				return false;
			}
			prc = amd_qos_parse_pool_num(argv[i + 1], &out->pool_num);
			if (prc) {
				fprintf(stderr, "Invalid --pool `%s`: %s\n",
					argv[i + 1], sdxi_err_str(prc));
				*rc_out = 1;
				return false;
			}
			out->have_pool = true;
			i += 2;
			continue;
		}
		if (strcmp(argv[i], "--vf") == 0) {
			int prc;

			if (i + 1 >= argc) {
				fprintf(stderr, "Missing argument for --vf\n");
				*rc_out = 1;
				return false;
			}
			if (out->vf) {
				fprintf(stderr, "--vf specified more than once\n");
				*rc_out = 1;
				return false;
			}
			prc = amd_qos_parse_u16_str(argv[i + 1], &out->vf_num);
			if (prc) {
				fprintf(stderr, "Invalid --vf `%s`: %s\n",
					argv[i + 1], sdxi_err_str(prc));
				*rc_out = 1;
				return false;
			}
			out->vf = true;
			i += 2;
			continue;
		}
		if (strcmp(argv[i], "--cxt") == 0) {
			int prc;

			if (i + 1 >= argc) {
				fprintf(stderr, "Missing argument for --cxt\n");
				*rc_out = 1;
				return false;
			}
			if (out->cxt) {
				fprintf(stderr, "--cxt specified more than once\n");
				*rc_out = 1;
				return false;
			}
			prc = amd_qos_parse_cxt_arg(argv[i + 1], &out->cxt_start,
						    &out->cxt_end);
			if (prc) {
				fprintf(stderr, "Invalid --cxt `%s`\n",
					argv[i + 1]);
				*rc_out = 1;
				return false;
			}
			out->cxt = true;
			i += 2;
			continue;
		}

		fprintf(stderr, "Unknown option or extra argument `%s`\n",
			argv[i]);
		amd_qos_print_usage(prog, usage);
		*rc_out = 1;
		return false;
	}

	if (!out->dev_arg) {
		fprintf(stderr,
			"Device name is required (e.g., sdxi0 or /dev/sdxi0)\n");
		amd_qos_print_usage(prog, usage);
		*rc_out = 1;
		return false;
	}
	if (require_pool && !out->have_pool) {
		fprintf(stderr, "--pool is required\n");
		amd_qos_print_usage(prog, usage);
		*rc_out = 1;
		return false;
	}

	return true;
}
