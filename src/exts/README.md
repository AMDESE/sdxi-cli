# Vendor extensions

Vendor extensions live under `src/exts/<vendor>/` and register a vendor
namespace via `SDXI_EXT_REGISTER()` (without a trailing semicolon).

## Adding a new vendor namespace

1. Create `src/exts/<vendor>/` with at least:
   - `<vendor>.c` implementing a `struct sdxi_cli_cmd` root command
   - `meson.build` adding sources to `ext_sources`
2. Add `subdir('<vendor>')` to `src/exts/meson.build`.
3. Rebuild `sdxi`; `sdxi help` lists the new namespace.
