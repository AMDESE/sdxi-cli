.PHONY: all clean install uninstall dist static help submodule rpm srcrpm deb

BUILDDIR = builddir
BUILDDIR_TARGET = $(BUILDDIR)/build.ninja
PREFIX ?= /usr/local
DESTDIR ?=
VERSION = 0.1.0
PROJECT = sdxi
SUBMODULE = libsdxi
SUBMODULE_UPDATE = git -c protocol.file.allow=always submodule update --init --recursive
LIBSDXI_LIB = $(SUBMODULE)/src/.libs/libsdxi.a

# RPM packaging configuration. Source tarball, spec template, and rpmbuild
# topdir all live under $(BUILDDIR)/rpm so the system-wide ~/rpmbuild is
# untouched.
RPM_PKG = sdxi-cli
RPM_TOPDIR = $(BUILDDIR)/rpm
RPM_SOURCE_TARBALL = $(RPM_TOPDIR)/SOURCES/$(RPM_PKG)-$(VERSION).tar.gz
RPM_SPEC_IN = packaging/$(RPM_PKG).spec.in
RPM_SPEC = $(RPM_TOPDIR)/SPECS/$(RPM_PKG).spec

# Debian packaging configuration. The build is staged under
# $(BUILDDIR)/deb/build/$(DEB_PKG)-$(VERSION) and the resulting .deb plus
# associated artifacts are moved to $(BUILDDIR)/deb/.
DEB_PKG = sdxi-cli
DEB_TOPDIR = $(BUILDDIR)/deb
DEB_BUILDDIR = $(DEB_TOPDIR)/build
DEB_STAGE = $(DEB_BUILDDIR)/$(DEB_PKG)-$(VERSION)
DEB_TEMPLATE = packaging/debian

all: $(BUILDDIR_TARGET)
	@meson compile -C $(BUILDDIR)

$(BUILDDIR_TARGET): $(LIBSDXI_LIB)
	@if [ -d $(BUILDDIR) ]; then \
		meson setup $(BUILDDIR) --reconfigure || \
			(rm -rf $(BUILDDIR) && meson setup $(BUILDDIR)); \
	else \
		meson setup $(BUILDDIR); \
	fi

$(SUBMODULE)/.git:
	@$(SUBMODULE_UPDATE)

$(LIBSDXI_LIB): $(SUBMODULE)/.git
	@cd $(SUBMODULE) && ./autogen.sh
	@cd $(SUBMODULE) && ./configure --quiet --with-pic
	@$(MAKE) -C $(SUBMODULE)

clean:
	@if [ -d $(BUILDDIR) ]; then \
		meson compile -C $(BUILDDIR) --clean || true; \
		rm -rf $(BUILDDIR); \
	fi
	@rm -rf $(PROJECT)-$(VERSION).tar.gz

install: all
	@meson install -C $(BUILDDIR) --destdir=$(DESTDIR)

uninstall:
	@if [ -d $(BUILDDIR) ]; then \
		meson compile -C $(BUILDDIR) --clean || true; \
		if [ -f $(PREFIX)/bin/$(PROJECT) ]; then \
			rm -f $(PREFIX)/bin/$(PROJECT); \
			echo "Uninstalled $(PREFIX)/bin/$(PROJECT)"; \
		else \
			echo "Binary not found at $(PREFIX)/bin/$(PROJECT)"; \
		fi; \
	else \
		echo "Build directory not found. Attempting to remove binary..."; \
		if [ -f $(PREFIX)/bin/$(PROJECT) ]; then \
			rm -f $(PREFIX)/bin/$(PROJECT); \
			echo "Uninstalled $(PREFIX)/bin/$(PROJECT)"; \
		else \
			echo "Binary not found at $(PREFIX)/bin/$(PROJECT)"; \
		fi; \
	fi

dist: $(BUILDDIR_TARGET)
	@meson dist -C $(BUILDDIR) --no-tests --formats=gztar --allow-dirty
	@echo "Distribution tarball created in $(BUILDDIR)/meson-dist/"

static:
	@rm -rf $(BUILDDIR)-static
	@LDFLAGS="-static" meson setup $(BUILDDIR)-static -Ddefault_library=static -Dprefer_static=true
	@meson compile -C $(BUILDDIR)-static
	@echo "Static binary built: $(BUILDDIR)-static/$(PROJECT)"

# ----------------------------------------------------------------------
# RPM packaging
#
# `make rpm` produces an installable binary RPM (and the matching SRPM)
# under $(RPM_TOPDIR)/RPMS and $(RPM_TOPDIR)/SRPMS. The build is fully
# local: rpmbuild is invoked with --define "_topdir <abs path>" so it
# never writes to ~/rpmbuild.
# ----------------------------------------------------------------------

# Build a self-contained source tarball that includes the libsdxi
# submodule. We avoid `git archive` because it can't include submodule
# contents in a single archive without an external helper.
$(RPM_SOURCE_TARBALL): Makefile $(SUBMODULE)/.git $(RPM_SPEC_IN)
	@command -v rpmbuild >/dev/null 2>&1 || { \
		echo "rpmbuild not found. Install rpm tooling first."; \
		exit 1; }
	@mkdir -p $(RPM_TOPDIR)/BUILD $(RPM_TOPDIR)/BUILDROOT \
	          $(RPM_TOPDIR)/RPMS $(RPM_TOPDIR)/SOURCES \
	          $(RPM_TOPDIR)/SPECS $(RPM_TOPDIR)/SRPMS
	@echo "Staging $(RPM_PKG)-$(VERSION) source tree..."
	@stage_dir=$$(mktemp -d -t sdxi-cli-rpm.XXXXXX); \
	 trap "rm -rf $$stage_dir" EXIT INT TERM; \
	 mkdir -p $$stage_dir/$(RPM_PKG)-$(VERSION); \
	 tar --exclude='./$(BUILDDIR)' \
	     --exclude='./$(BUILDDIR)-static' \
	     --exclude='./.git' \
	     --exclude='./.codex' \
	     --exclude='./$(SUBMODULE)' \
	     -cf - . | \
	   tar -xf - -C $$stage_dir/$(RPM_PKG)-$(VERSION); \
	 mkdir -p $$stage_dir/$(RPM_PKG)-$(VERSION)/$(SUBMODULE); \
	 git -C $(SUBMODULE) ls-files -z | \
	   tar -C $(SUBMODULE) --null -T - -cf - | \
	   tar -xf - -C $$stage_dir/$(RPM_PKG)-$(VERSION)/$(SUBMODULE); \
	 tar -C $$stage_dir -czf $(RPM_SOURCE_TARBALL) $(RPM_PKG)-$(VERSION)
	@echo "Source tarball: $(RPM_SOURCE_TARBALL)"

$(RPM_SPEC): $(RPM_SPEC_IN) $(RPM_SOURCE_TARBALL)
	@mkdir -p $(dir $(RPM_SPEC))
	@sed -e 's/@VERSION@/$(VERSION)/g' \
	     -e "s/@CHANGELOG_DATE@/`LC_ALL=C date '+%a %b %d %Y'`/" \
	     $(RPM_SPEC_IN) > $(RPM_SPEC)

# RPMBUILD_FLAGS is appended to every rpmbuild invocation. The default
# passes --nodeps so the local build doesn't fail on Debian/Ubuntu hosts
# where the BuildRequires names are tracked by dpkg, not rpm. Callers on
# RPM-based distros may override this on the command line, e.g.:
#   make rpm RPMBUILD_FLAGS=
RPMBUILD_FLAGS ?= --nodeps

srcrpm: $(RPM_SPEC)
	@rpmbuild --define "_topdir $(CURDIR)/$(RPM_TOPDIR)" \
	          $(RPMBUILD_FLAGS) -bs $(RPM_SPEC)
	@echo "SRPM(s):"
	@ls $(RPM_TOPDIR)/SRPMS/*.src.rpm

rpm: $(RPM_SPEC)
	@rpmbuild --define "_topdir $(CURDIR)/$(RPM_TOPDIR)" \
	          $(RPMBUILD_FLAGS) -ba $(RPM_SPEC)
	@echo "Built RPM(s):"
	@find $(RPM_TOPDIR)/RPMS $(RPM_TOPDIR)/SRPMS -name '*.rpm' -print

# ----------------------------------------------------------------------
# Debian packaging
#
# `make deb` produces an installable .deb under $(DEB_TOPDIR). The build
# is fully local: dpkg-buildpackage runs inside a staged copy of the
# source tree under $(DEB_BUILDDIR) so the working tree is left clean.
# ----------------------------------------------------------------------

deb: $(SUBMODULE)/.git
	@command -v dpkg-buildpackage >/dev/null 2>&1 || { \
		echo "dpkg-buildpackage not found. Install dpkg-dev and debhelper first."; \
		exit 1; }
	@command -v dh >/dev/null 2>&1 || { \
		echo "debhelper not found. Install debhelper first."; \
		exit 1; }
	@echo "Staging $(DEB_PKG)-$(VERSION) source tree..."
	@rm -rf $(DEB_BUILDDIR)
	@mkdir -p $(DEB_STAGE)
	@tar --exclude='./$(BUILDDIR)' \
	     --exclude='./$(BUILDDIR)-static' \
	     --exclude='./.git' \
	     --exclude='./.codex' \
	     --exclude='./$(SUBMODULE)' \
	     -cf - . | tar -xf - -C $(DEB_STAGE)
	@mkdir -p $(DEB_STAGE)/$(SUBMODULE)
	@git -C $(SUBMODULE) ls-files -z | \
	     tar -C $(SUBMODULE) --null -T - -cf - | \
	     tar -xf - -C $(DEB_STAGE)/$(SUBMODULE)
	@mkdir -p $(DEB_STAGE)/debian/source
	@cp $(DEB_TEMPLATE)/copyright $(DEB_STAGE)/debian/copyright
	@cp $(DEB_TEMPLATE)/rules $(DEB_STAGE)/debian/rules
	@chmod +x $(DEB_STAGE)/debian/rules
	@cp $(DEB_TEMPLATE)/source/format $(DEB_STAGE)/debian/source/format
	@sed -e 's/@VERSION@/$(VERSION)/g' \
	     $(DEB_TEMPLATE)/control.in > $(DEB_STAGE)/debian/control
	@sed -e 's/@VERSION@/$(VERSION)/g' \
	     -e "s/@CHANGELOG_DATE@/`LC_ALL=C date -R`/" \
	     $(DEB_TEMPLATE)/changelog.in > $(DEB_STAGE)/debian/changelog
	@cd $(DEB_STAGE) && dpkg-buildpackage -b -us -uc -d
	@echo "Built Debian artifacts:"
	@ls $(DEB_BUILDDIR)/$(DEB_PKG)*.deb \
	    $(DEB_BUILDDIR)/$(DEB_PKG)*.changes \
	    $(DEB_BUILDDIR)/$(DEB_PKG)*.buildinfo 2>/dev/null

help:
	@echo "Available targets:"
	@echo "  all       - Build the project (default)"
	@echo "  clean     - Remove build artifacts"
	@echo "  install   - Install the built binary (use PREFIX=/path to set install location)"
	@echo "  uninstall - Uninstall the binary"
	@echo "  dist      - Create distribution tarball"
	@echo "  static    - Build static binary"
	@echo "  rpm       - Build installable binary RPM (and SRPM) under $(RPM_TOPDIR)"
	@echo "  srcrpm    - Build source RPM only under $(RPM_TOPDIR)/SRPMS"
	@echo "  deb       - Build installable Debian package under $(DEB_TOPDIR)"
	@echo "  help      - Show this help message"
	@echo "  submodule - Update and fetch submodules"
	@echo ""
	@echo "Variables:"
	@echo "  PREFIX    - Installation prefix (default: /usr/local)"
	@echo "  DESTDIR   - Destination directory for staged installs"

submodule:
	@$(SUBMODULE_UPDATE)
