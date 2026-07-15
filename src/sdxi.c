/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * SDXI - Main function for sdxi-cli
 */

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>

#include <libsdxi.h>

#include "cli_cmd.h"
#include "exts/registry.h"

#define SDXI_VERSION "0.1.0"
#define SDXI_DESCRIPTION \
	"Command line tool for interacting with SDXI devices and drivers."

#define ARRAY_SIZE_OF(x) (sizeof(x) / sizeof((x)[0]))

static int sdxi_cmd_version(const char *prog, int argc, char **argv);
static int sdxi_cmd_help(const char *prog, int argc, char **argv);
static int sdxi_cmd_devices(const char *prog, int argc, char **argv);
static int sdxi_cmd_show(const char *prog, int argc, char **argv);
int sdxi_cmd_attrs(const char *prog, int argc, char **argv);
int sdxi_cmd_attr(const char *prog, int argc, char **argv);
int sdxi_cmd_set_attr(const char *prog, int argc, char **argv);

static const char * const help_aliases[] = { "-h", "--help", NULL };
static const char * const version_aliases[] = { "-v", "--version", NULL };

static const struct sdxi_cli_cmd base_cmds[] = {
	{ "devices", NULL, "List SDXI devices", "devices [--json]",
	  sdxi_cmd_devices, NULL },
	{ "show", NULL, "Show device info",
	  "show <device> [--json] [-v|--verbose]", sdxi_cmd_show, NULL },
	{ "attrs", NULL, "List device attributes",
	  "attrs <device> [--json]", sdxi_cmd_attrs, NULL },
	{ "attr", NULL, "Show device attribute",
	  "attr <device> <attr> [--json]", sdxi_cmd_attr, NULL },
	{ "set-attr", NULL, "Set device attribute",
	  "set-attr <device> <attr> <value>", sdxi_cmd_set_attr, NULL },
	{ "version", version_aliases, "Show program version", "version",
	  sdxi_cmd_version, NULL },
	{ "help", help_aliases, "Display this help", "help [command...]",
	  sdxi_cmd_help, NULL },
	{ NULL, NULL, NULL, NULL, NULL, NULL }
};

static const struct sdxi_cli_cmd *find_root_command(const char *name)
{
	const struct sdxi_cli_cmd *cmd;

	cmd = sdxi_cli_cmd_find(base_cmds, name);
	if (cmd)
		return cmd;

	return sdxi_ext_find_vendor(name);
}

static void print_vendor_list(size_t padding)
{
	for (size_t i = 0; i < sdxi_ext_vendor_count(); i++) {
		const struct sdxi_cli_cmd *cmd = sdxi_ext_vendor_at(i);

		if (!cmd || !cmd->name)
			continue;

		printf("  %-*s     %s\n", (int)padding, cmd->name,
		       cmd->desc ? cmd->desc : "");
	}
}

static void print_general_help(const char *prog)
{
	size_t padding = sdxi_cli_cmd_name_len_max(base_cmds);
	bool have_vendor = (sdxi_ext_vendor_count() > 0);

	for (size_t i = 0; i < sdxi_ext_vendor_count(); i++) {
		const struct sdxi_cli_cmd *cmd = sdxi_ext_vendor_at(i);
		size_t len;

		if (!cmd || !cmd->name)
			continue;

		len = strlen(cmd->name);
		if (len > padding)
			padding = len;
	}

	printf("%s %s\n", prog, SDXI_VERSION);
	printf("usage: %s <command> [<args>]\n\n", prog);
	printf("%s\n", SDXI_DESCRIPTION);
	printf("\n");
	printf("Available commands:\n");

	sdxi_cli_cmd_print_list(base_cmds, padding);

	if (have_vendor) {
		printf("\nVendor extensions:\n");
		print_vendor_list(padding);
	}

	printf("\nRun `%s help <command...>` for more details.\n", prog);
}

static int sdxi_cmd_help(const char *prog, int argc, char **argv)
{
	const struct sdxi_cli_cmd *cmd;
	const struct sdxi_cli_cmd *next;
	int i;

	if (argc == 0) {
		print_general_help(prog);
		return 0;
	}

	cmd = find_root_command(argv[0]);

	if (!cmd) {
		fprintf(stderr, "Unknown command `%s`.\n\n", argv[0]);
		print_general_help(prog);
		return 1;
	}

	for (i = 1; i < argc; i++) {
		if (!cmd->subcmds) {
			fprintf(stderr, "Command `%s` has no subcommands.\n\n",
				cmd->name);
			sdxi_cli_cmd_print_help(prog, cmd);
			return 1;
		}

		next = sdxi_cli_cmd_find(cmd->subcmds, argv[i]);
		if (!next) {
			fprintf(stderr, "Unknown subcommand `%s`.\n\n", argv[i]);
			sdxi_cli_cmd_print_help(prog, cmd);
			return 1;
		}
		cmd = next;
	}

	sdxi_cli_cmd_print_help(prog, cmd);

	return 0;
}

static int sdxi_cmd_version(const char *prog, int argc, char **argv)
{
	printf("%s version %s\n", prog, SDXI_VERSION);

	return 0;
}

struct list_entry {
	const char *path;
	struct sdxi_dev_caps caps;
	bool has_caps;
	bool has_bdf;
	bool has_vendor;
	int caps_status;
	char pci_bdf[32];
	char vendor[32];
	int pf_id;
	int vf_id;
	bool is_vf_device;
	int orig_index;
};

static const char *yes_no(bool value)
{
	return value ? "yes" : "no";
}

static const char *limit_fmt(uint64_t val, char *buf, size_t len)
{
	unsigned int exp = 0;
	uint64_t tmp = val;

	if (!buf || len == 0)
		return "";

	if (val > 65536) {
		while ((tmp % 2) == 0 && tmp > 1) {
			tmp >>= 1;
			exp++;
		}
		if (tmp == 1) {
			snprintf(buf, len, "2^%u", exp);
			return buf;
		}
	}

	snprintf(buf, len, "%" PRIu64, val);
	return buf;
}

static const char *bytes_fmt(uint64_t bytes, char *buf, size_t len)
{
	static const char * const units[] = {
		"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB",
	};
	size_t unit = 0;
	uint64_t val = bytes;

	if (!buf || len == 0)
		return "";

	while (val >= 1024 && (val % 1024) == 0 && unit + 1 < ARRAY_SIZE_OF(units)) {
		val /= 1024;
		unit++;
	}

	snprintf(buf, len, "%" PRIu64 " %s", val, units[unit]);
	return buf;
}

static const char *func_type(const struct sdxi_dev_caps *caps)
{
	return caps->is_vf ? "VF" : "PF";
}

static int parse_device_ids(const char *dev_path, int *pf, int *vf, bool *is_vf)
{
	const char *name;
	int pf_id, vf_id, parsed;

	if (!dev_path)
		return -1;

	name = strrchr(dev_path, '/');
	name = name ? name + 1 : dev_path;

	parsed = sscanf(name, "sdxi%dv%d", &pf_id, &vf_id);
	if (parsed == 2) {
		if (pf)
			*pf = pf_id;
		if (vf)
			*vf = vf_id;
		if (is_vf)
			*is_vf = true;
		return 0;
	}

	parsed = sscanf(name, "sdxi%d", &pf_id);
	if (parsed == 1) {
		if (pf)
			*pf = pf_id;
		if (vf)
			*vf = -1;
		if (is_vf)
			*is_vf = false;
		return 0;
	}

	return -1;
}

static int dev_path_from_arg(const char *device_arg, char *buf, size_t buflen,
			     const char **path_out)
{
	int ret;

	if (!device_arg || device_arg[0] == '\0' || !buf || buflen == 0 || !path_out)
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

static int resolve_pci_bdf(const char *dev_path, char *bdf, size_t bdf_len)
{
	char sysfs_path[PATH_MAX];
	char resolved[PATH_MAX];
	const char *dev_name;
	const char *bdf_start;
	int ret;

	if (!dev_path || !bdf || bdf_len == 0)
		return -1;

	dev_name = strrchr(dev_path, '/');
	dev_name = dev_name ? dev_name + 1 : dev_path;

	ret = snprintf(sysfs_path, sizeof(sysfs_path), "/sys/class/sdxi/%s/device",
		       dev_name);
	if (ret < 0 || (size_t)ret >= sizeof(sysfs_path))
		return -1;

	if (!realpath(sysfs_path, resolved))
		return -1;

	bdf_start = strrchr(resolved, '/');
	if (!bdf_start || bdf_start[1] == '\0')
		return -1;

	ret = snprintf(bdf, bdf_len, "%s", bdf_start + 1);
	if (ret < 0 || (size_t)ret >= bdf_len)
		return -1;

	return 0;
}

static const struct {
	unsigned int id;
	const char *name;
} vendor_table[] = {
	{ 0x1022, "AMD" },
};
static const char *vendor_from_id(unsigned int vendor_id)
{
	for (size_t i = 0; i < ARRAY_SIZE_OF(vendor_table); i++) {
		if (vendor_table[i].id == vendor_id)
			return vendor_table[i].name;
	}

	return NULL;
}

static int resolve_pci_vendor(const char *dev_path, char *vendor, size_t vendor_len)
{
	char sysfs_path[PATH_MAX];
	char buf[32];
	const char *dev_name;
	unsigned long vid;
	char *end;
	int fd;
	ssize_t n;
	const char *name;

	if (!dev_path || !vendor || vendor_len == 0)
		return -1;

	dev_name = strrchr(dev_path, '/');
	dev_name = dev_name ? dev_name + 1 : dev_path;

	if (snprintf(sysfs_path, sizeof(sysfs_path),
		     "/sys/class/sdxi/%s/device/vendor", dev_name) >= (int)sizeof(sysfs_path))
		return -1;

	fd = open(sysfs_path, O_RDONLY);
	if (fd < 0)
		return -1;

	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return -1;

	buf[n] = '\0';
	vid = strtoul(buf, &end, 0);
	if (end == buf)
		return -1;

	name = vendor_from_id((unsigned int)vid);
	if (name)
		snprintf(vendor, vendor_len, "%s", name);
	else
		snprintf(vendor, vendor_len, "0x%04lx", vid & 0xFFFF);

	return 0;
}

static void print_list_table(const struct list_entry *entries, int count)
{
	int i;

	printf("%-16s %-16s %-6s %-10s %-12s\n",
	       "Node", "PCI BDF", "Func", "Version", "Vendor");
	printf("------------------------------------------------------\n");

	for (i = 0; i < count; i++) {
		const struct list_entry *ent = &entries[i];
		const char *bdf = ent->has_bdf ? ent->pci_bdf : "-";
		char ver[8];
		const char *vendor = ent->has_vendor ? ent->vendor : "-";

		if (!ent->has_caps) {
			printf("%-16s %-16s %-6s %-10s %-12s\n",
			       ent->path, bdf, "-", "-", vendor);
			continue;
		}

		snprintf(ver, sizeof(ver), "%u.%u", (unsigned int)ent->caps.ver_major,
			 (unsigned int)ent->caps.ver_minor);

		printf("%-16s %-16s %-6s %-10s %-12s\n",
		       ent->path, bdf, func_type(&ent->caps), ver, vendor);
	}
}

static void print_list_json(const struct list_entry *entries, int count)
{
	int i;

	printf("{\n  \"devices\": [\n");
	for (i = 0; i < count; i++) {
		const struct list_entry *ent = &entries[i];
		const bool last = (i == count - 1);

		printf("    {\n");
		printf("      \"path\": \"%s\",\n", ent->path);
		if (ent->has_bdf)
			printf("      \"pci_bdf\": \"%s\",\n", ent->pci_bdf);
		else
			printf("      \"pci_bdf\": null,\n");

		if (!ent->has_caps) {
			printf("      \"function\": null,\n");
			printf("      \"version\": null,\n");
			if (ent->has_vendor)
				printf("      \"vendor\": \"%s\",\n", ent->vendor);
			else
				printf("      \"vendor\": null,\n");
			printf("      \"error\": \"%s\"\n",
			       sdxi_err_str(ent->caps_status));
			printf("    }%s\n", last ? "" : ",");
			continue;
		}

		printf("      \"function\": \"%s\",\n", ent->caps.is_vf ? "vf" : "pf");
		printf("      \"version\": { \"major\": %" PRIu8 ", \"minor\": %" PRIu8 " },\n",
		       ent->caps.ver_major, ent->caps.ver_minor);
		if (ent->has_vendor)
			printf("      \"vendor\": \"%s\"\n", ent->vendor);
		else
			printf("      \"vendor\": null\n");
		printf("    }%s\n", last ? "" : ",");
	}
	printf("  ]\n}\n");
}

#define SDXI_MAX_DEVICES 64

static int list_entry_cmp(const void *a, const void *b)
{
	const struct list_entry *la = a;
	const struct list_entry *lb = b;

	if (la->pf_id != lb->pf_id)
		return (la->pf_id < lb->pf_id) ? -1 : 1;

	if (la->is_vf_device != lb->is_vf_device)
		return la->is_vf_device ? 1 : -1; /* PF before VF */

	if (la->vf_id != lb->vf_id)
		return (la->vf_id < lb->vf_id) ? -1 : 1;

	return (la->orig_index < lb->orig_index) ? -1 : 1;
}

static int sdxi_cmd_devices(const char *prog, int argc, char **argv)
{
	struct list_entry *entries = NULL;
	char **dev_list = NULL;
	size_t dev_count = 0;
	size_t used_count = 0;
	bool json = false;
	bool had_error = false;
	int i;
	int st;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--json") == 0) {
			json = true;
			continue;
		}

		fprintf(stderr, "Unknown option `%s`\n\n", argv[i]);
		printf("Usage: %s devices [--json]\n", prog);
		return 1;
	}

	st = sdxi_list_devs(&dev_list, &dev_count);
	if (st) {
		fprintf(stderr, "Failed to enumerate SDXI devices: %s\n",
			sdxi_err_str(st));
		return 1;
	}

	if (dev_count == 0) {
		if (json)
			printf("{ \"devices\": [] }\n");
		else
			printf("No SDXI devices found.\n");
		sdxi_free_dev_list(dev_list, dev_count);
		return 0;
	}

	used_count = dev_count;
	if (used_count > SDXI_MAX_DEVICES) {
		fprintf(stderr, "Warning: truncating device list (%zu -> %u)\n",
			used_count, SDXI_MAX_DEVICES);
		used_count = SDXI_MAX_DEVICES;
	}

	entries = calloc(used_count, sizeof(*entries));
	if (!entries) {
		fprintf(stderr, "Out of memory\n");
		sdxi_free_dev_list(dev_list, dev_count);
		return 1;
	}

	for (i = 0; i < (int)used_count; i++) {
		struct list_entry *ent = &entries[i];
		int pf_id = INT_MAX;
		int vf_id = INT_MAX;
		bool is_vf = false;
		sdxi_dev_t *dev = NULL;

		memset(ent, 0, sizeof(*ent));
		ent->path = dev_list[i];
		ent->orig_index = i;

		if (parse_device_ids(ent->path, &pf_id, &vf_id, &is_vf) == 0) {
			ent->pf_id = pf_id;
			ent->vf_id = vf_id;
			ent->is_vf_device = is_vf;
		} else {
			ent->pf_id = INT_MAX;
			ent->vf_id = INT_MAX;
			ent->is_vf_device = false;
		}

		ent->has_bdf = (resolve_pci_bdf(ent->path, ent->pci_bdf,
						sizeof(ent->pci_bdf)) == 0);
		ent->has_vendor = (resolve_pci_vendor(ent->path, ent->vendor,
						      sizeof(ent->vendor)) == 0);

		st = sdxi_dev_open(ent->path, &dev);
		if (st) {
			ent->caps_status = st;
			ent->has_caps = false;
			had_error = true;
			continue;
		}

		st = sdxi_dev_get_caps(dev, &ent->caps);
		sdxi_dev_close(dev);

		ent->caps_status = st;
		ent->has_caps = (st == 0);
		if (!ent->has_caps)
			had_error = true;
	}

	qsort(entries, used_count, sizeof(entries[0]), list_entry_cmp);

	if (json)
		print_list_json(entries, (int)used_count);
	else
		print_list_table(entries, (int)used_count);

	free(entries);
	sdxi_free_dev_list(dev_list, dev_count);

	return had_error ? 1 : 0;
}

static void print_info_table(const char *path, const struct sdxi_dev_caps *caps,
			     const char *bdf, const char *vendor, bool verbose)
{
	const char *func = caps->is_vf ? "VF" : "PF";
	char buf[32];
	char bytes[32];

	printf("Device:      %s (%s)\n", path, func);
	if (bdf)
		printf("PCI BDF:     %s\n", bdf);
	if (vendor)
		printf("Vendor:      %s\n", vendor);
	printf("SDXI Ver:    %u.%u\n", caps->ver_major, caps->ver_minor);
	printf("SFunc:       %" PRIu16 "\n", caps->sfunc);
	if (!verbose)
		return;

	printf("\n");
	printf("Capabilities:\n");
	printf("  cs_cap:               %" PRIu8 "\n", caps->cs_cap);
	printf("  rm_required:          %s\n", yes_no(caps->rm_required));
	printf("  mmio_64bit:           %s\n", yes_no(caps->mmio_64bit));
	printf("  rkey_supported:       %s\n", yes_no(caps->rkey_supported));
	printf("  op_group_caps:        0x%08" PRIx32 "\n", caps->op_group_caps);
	printf("  doorbell_stride:      %" PRIu32 " bytes\n", caps->db_stride_bytes);
	printf("\n");
	printf("Limits:\n");
	printf("  max_contexts:         %s\n", limit_fmt(caps->max_cxt_num, buf, sizeof(buf)));
	printf("  max_ds_ring_entries:  %s\n", limit_fmt(caps->max_ds_ring_entries, buf, sizeof(buf)));
	printf("  max_akey_entries:     %s\n", limit_fmt(caps->max_akey_entries, buf, sizeof(buf)));
	printf("  max_rkey_entries:     %s\n", limit_fmt(caps->max_rkey_entries, buf, sizeof(buf)));
	printf("  max_errlog_entries:   %s\n", limit_fmt(caps->max_errlog_entries, buf, sizeof(buf)));
	printf("  max_buffer_bytes:     %s\n", bytes_fmt(caps->max_buffer_bytes, bytes, sizeof(bytes)));
}

static void print_info_json(const char *path, const struct sdxi_dev_caps *caps,
			    const char *bdf, const char *vendor)
{
	printf("{\n");
	printf("  \"path\": \"%s\",\n", path);
	if (bdf)
		printf("  \"pci_bdf\": \"%s\",\n", bdf);
	else
		printf("  \"pci_bdf\": null,\n");
	printf("  \"function\": \"%s\",\n", caps->is_vf ? "vf" : "pf");
	printf("  \"sfunc\": %" PRIu16 ",\n", caps->sfunc);
	printf("  \"version\": { \"major\": %" PRIu8 ", \"minor\": %" PRIu8 " },\n",
	       caps->ver_major, caps->ver_minor);
	if (vendor)
		printf("  \"vendor\": \"%s\",\n", vendor);
	else
		printf("  \"vendor\": null,\n");
	printf("  \"caps\": {\n");
	printf("    \"cs_cap\": %" PRIu8 ",\n", caps->cs_cap);
	printf("    \"rm_required\": %s,\n", caps->rm_required ? "true" : "false");
	printf("    \"mmio_64bit\": %s,\n", caps->mmio_64bit ? "true" : "false");
	printf("    \"rkey_supported\": %s,\n", caps->rkey_supported ? "true" : "false");
	printf("    \"op_group_caps\": %" PRIu32 ",\n", caps->op_group_caps);
	printf("    \"doorbell_stride_bytes\": %" PRIu32 "\n", caps->db_stride_bytes);
	printf("  },\n");
	printf("  \"limits\": {\n");
	printf("    \"max_contexts\": %" PRIu32 ",\n", caps->max_cxt_num);
	printf("    \"max_ds_ring_entries\": %" PRIu64 ",\n", caps->max_ds_ring_entries);
	printf("    \"max_akey_entries\": %" PRIu32 ",\n", caps->max_akey_entries);
	printf("    \"max_rkey_entries\": %" PRIu32 ",\n", caps->max_rkey_entries);
	printf("    \"max_errlog_entries\": %" PRIu64 ",\n", caps->max_errlog_entries);
	printf("    \"max_buffer_bytes\": %" PRIu64 "\n", caps->max_buffer_bytes);
	printf("  }\n");
	printf("}\n");
}

static int sdxi_cmd_show(const char *prog, int argc, char **argv)
{
	struct sdxi_dev_caps caps;
	const char *device_arg = NULL;
	bool json = false;
	bool verbose = false;
	bool has_bdf = false;
	bool has_vendor = false;
	char bdf[32];
	char vendor[32];
	char path_buf[PATH_MAX];
	const char *path;
	int i;
	int st;
	sdxi_dev_t *dev = NULL;

	for (i = 0; i < argc; ) {
		if (strcmp(argv[i], "--json") == 0) {
			json = true;
			i++;
			continue;
		}
		if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
			verbose = true;
			i++;
			continue;
		}

		if (!device_arg) {
			device_arg = argv[i];
			i++;
			continue;
		}

		fprintf(stderr, "Unknown option or extra argument `%s`\n", argv[i]);
		printf("Usage: %s show <device> [--json] [-v|--verbose]\n", prog);
		return 1;
	}

	if (!device_arg || device_arg[0] == '\0') {
		fprintf(stderr, "Device name is required (e.g., sdxi0 or /dev/sdxi0)\n");
		printf("Usage: %s show <device> [--json] [-v|--verbose]\n", prog);
		return 1;
	}

	st = dev_path_from_arg(device_arg, path_buf, sizeof(path_buf), &path);
	if (st) {
		fprintf(stderr, "Invalid device path: %s\n", sdxi_err_str(st));
		return 1;
	}

	st = sdxi_dev_open(path, &dev);
	if (st) {
		fprintf(stderr, "Failed to open %s: %s\n", path, sdxi_err_str(st));
		return 1;
	}

	st = sdxi_dev_get_caps(dev, &caps);
	sdxi_dev_close(dev);
	if (st) {
		fprintf(stderr, "Failed to get device info for %s: %s\n",
			path, sdxi_err_str(st));
		return 1;
	}

	has_bdf = (resolve_pci_bdf(path, bdf, sizeof(bdf)) == 0);
	has_vendor = (resolve_pci_vendor(path, vendor, sizeof(vendor)) == 0);

	if (json)
		print_info_json(path, &caps, has_bdf ? bdf : NULL,
				has_vendor ? vendor : NULL);
	else
		print_info_table(path, &caps, has_bdf ? bdf : NULL,
				 has_vendor ? vendor : NULL, verbose);

	return 0;
}

static int invoke_cmd(const char *prog, const char *cmd, int argc, char **argv)
{
	const struct sdxi_cli_cmd *entry = find_root_command(cmd);

	if (!entry) {
		fprintf(stderr, "Invalid command `%s`.\n\n", cmd);
		print_general_help(prog);
		return 1;
	}

	return sdxi_cli_cmd_invoke(prog, entry, argc, argv);
}

int main(int argc, char *argv[])
{
	const char *prog = basename(argv[0]);
	const char *cmd = (argc >= 2) ? argv[1] : "help";
	int cmd_argc = (argc > 2) ? argc - 2 : 0;
	char **cmd_argv = (argc > 2) ? &argv[2] : NULL;

	return invoke_cmd(prog, cmd, cmd_argc, cmd_argv);
}
