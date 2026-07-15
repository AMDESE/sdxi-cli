.PHONY: all clean install uninstall dist static help

BUILDDIR = builddir
PREFIX ?= /usr/local
DESTDIR ?=
VERSION = 0.1.0
PROJECT = sdxi

all: $(BUILDDIR)
	@meson compile -C $(BUILDDIR)

$(BUILDDIR):
	@meson setup $(BUILDDIR)

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

dist:
	@meson dist -C $(BUILDDIR) --no-tests --formats=gztar || \
		(meson setup $(BUILDDIR) && meson dist -C $(BUILDDIR) --no-tests --formats=gztar)
	@echo "Distribution tarball created in $(BUILDDIR)/meson-dist/"

static:
	@rm -rf $(BUILDDIR)-static
	@LDFLAGS="-static" meson setup $(BUILDDIR)-static -Ddefault_library=static -Dprefer_static=true
	@meson compile -C $(BUILDDIR)-static
	@echo "Static binary built: $(BUILDDIR)-static/$(PROJECT)"

help:
	@echo "Available targets:"
	@echo "  all       - Build the project (default)"
	@echo "  clean     - Remove build artifacts"
	@echo "  install   - Install the built binary (use PREFIX=/path to set install location)"
	@echo "  uninstall - Uninstall the binary"
	@echo "  dist      - Create distribution tarball"
	@echo "  static    - Build static binary"
	@echo "  help      - Show this help message"
	@echo ""
	@echo "Variables:"
	@echo "  PREFIX    - Installation prefix (default: /usr/local)"
	@echo "  DESTDIR   - Destination directory for staged installs"
