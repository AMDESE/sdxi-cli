# sdxi-cli

`sdxi-cli` is the command-line tool for the Linux SDXI software stack. It uses `libsdxi` for all device access and provides a small operator-facing interface for device discovery, capability inspection, device attributes, and selected vendor extensions.

## Contents

- [Supported Environment](#supported-environment)
- [Build Requirements](#build-requirements)
- [libsdxi Dependency](#libsdxi-dependency)
- [Build and Install](#build-and-install)
- [Command Overview](#command-overview)
- [AMD QoS](#amd-qos)
- [Packaging](#packaging)
- [License](#license)

## Supported Environment

`sdxi-cli` targets Linux systems with an SDXI kernel driver exposing character devices under `/dev`.

Device selectors may use either the full path or the short device name:

- `/dev/sdxi0` or `sdxi0`: physical function 0
- `/dev/sdxi0v0` or `sdxi0v0`: virtual function 0 on PF 0



## Build Requirements

From a git checkout, the build requires:

- C compiler such as `gcc` or `clang`
- `make`
- `meson` and `ninja`
- `git`
- `libsdxi` submodule build requirements (`autoconf`, `automake`, `libtool`, `pkg-config`)

The top-level `Makefile` initializes and builds the `libsdxi` submodule before building the CLI.

## libsdxi Dependency

`sdxi-cli` depends on `libsdxi`. In this repository, `libsdxi` is carried as a git submodule. The top-level `Makefile` initializes and builds it before building `sdxi-cli`:

```sh
make
```

If the submodule has not been initialized yet, `make` runs:

```sh
git -c protocol.file.allow=always submodule update --init --recursive
```

and then builds `libsdxi` with its autotools flow:

```sh
cd libsdxi
./autogen.sh
./configure --quiet
make
```

The CLI Meson build links against the static library produced at:

```text
libsdxi/src/.libs/libsdxi.a
```

Direct `meson setup builddir` is expected to work only after `libsdxi` has already been built in the submodule.

## Build and Install

Build:

```sh
make
```

Install:

```sh
sudo make install
```

Build a static binary:

```sh
make static
```

Clean generated build output:

```sh
make clean
```



## Command Overview

Show the command tree:

```sh
sdxi help
```

Core commands:

```sh
sdxi devices [--json]
sdxi show <device> [--json] [-v|--verbose]
sdxi attrs <device> [--json]
sdxi attr <device> <attr> [--json]
sdxi set-attr <device> <attr> <value>
sdxi version
```

Examples:

```sh
sdxi devices
sdxi show sdxi0 --verbose
sdxi attrs sdxi0
sdxi attr sdxi0 GSR --json
```

`set-attr` is a privileged device-control operation and normally requires `CAP_SYS_ADMIN`.

## AMD QoS

AMD QoS commands live under the vendor namespace:

```sh
sdxi amd qos <command> [args]
```

Common read-only commands:

```sh
sdxi amd qos cap sdxi0
sdxi amd qos status sdxi0
sdxi amd qos pools sdxi0
sdxi amd qos active-pools sdxi0
sdxi amd qos pool-show sdxi0 --pool 0
sdxi amd qos show-assignment sdxi0
```

Mutating QoS commands:

```sh
sdxi amd qos set-maxbw sdxi0 <bw>
sdxi amd qos pool-create sdxi0 --pool <n> --minbw <bw> --maxbw <bw>
sdxi amd qos pool-update sdxi0 --pool <n> --minbw <bw> --maxbw <bw>
sdxi amd qos pool-destroy sdxi0 --pool <n>
sdxi amd qos assign sdxi0 --pool <n> [--vf <n>] [--cxt <id>[:<id>]]
```

AMD QoS is PF-only and uses privileged admin/vendor operations. Most systems require `CAP_SYS_ADMIN` for these commands. Bandwidth values are integer MB/s or decimal GB/s, for example `50000`, `50000MB/s`, or `1.5GB/s`.

## Packaging

Local package helpers are available through the top-level `Makefile`:

```sh
make rpm
make srcrpm
make deb
make dist
```

Packaging builds are staged under `builddir/` and do not use the system-wide RPM build tree.

## License

`sdxi-cli` is distributed under the BSD 3-Clause License. See `LICENSE`.