# space-wine Makefile
#
# Primary baseline:  wine-11.5
# Backport baseline: wine-10.0
#
# Targets:
#   make test-tools                    — compile test .exe files (requires mingw-w64)
#   make clone                         — clone the selected Wine source tag
#   make patch                         — apply the versioned patch/workaround series
#   make build                         — configure + build patched Wine
#   make test                          — run standalone verification tools
#   make prove                         — full pipeline: clone, patch, build, test
#   make prove WINE_VERSION=10.0       — full backport pipeline for wine-10.0
#
# Versioned series:
#   patches/series-11.5.txt
#   patches/series-10.0.txt
#
# Variables (override on command line):
#   WINE_VERSION — supported values: 11.5 (default), 10.0
#   WINE_TAG     — explicit upstream tag (default: wine-$(WINE_VERSION))
#   WINE_SRC     — Wine source directory (default: wine-src-$(WINE_VERSION))
#   BUILD_DIR    — build output directory (default: $(WINE_SRC)/build)
#   PREFIX       — install prefix (default: /opt/wine-$(WINE_VERSION))
#   NCPU         — parallel build jobs (auto-detected)

SUPPORTED_WINE_VERSIONS := 10.0 11.5
WINE_VERSION ?= 11.5
WINE_TAG ?= wine-$(WINE_VERSION)
WINE_SRC ?= wine-src-$(WINE_VERSION)
BUILD_DIR ?= $(WINE_SRC)/build
PREFIX ?= /opt/wine-$(WINE_VERSION)
NCPU ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
MINGW_GCC ?= x86_64-w64-mingw32-gcc
PATCH_SERIES := patches/series-$(WINE_VERSION).txt
WORKAROUND_SERIES := workarounds/series-$(WINE_VERSION).txt
MACOS_FREETYPE_PREFIX ?= $(PREFIX)
MACOS_FREETYPE_INCLUDEDIR ?= $(MACOS_FREETYPE_PREFIX)/include/freetype2
MACOS_FREETYPE_LIBDIR ?= $(MACOS_FREETYPE_PREFIX)/lib
MACOS_FREETYPE_LIB ?= $(MACOS_FREETYPE_LIBDIR)/libfreetype.6.dylib
MACOS_FREETYPE_STAGE_PREFIX ?= /tmp/freetype-x86
MACOS_FREETYPE_STAGE_LIBDIR ?= $(MACOS_FREETYPE_STAGE_PREFIX)/lib
MACOS_FREETYPE_STAGE_LIB ?= $(MACOS_FREETYPE_STAGE_LIBDIR)/libfreetype.6.dylib
MACOS_FREETYPE_STAGE_INCLUDE ?= $(MACOS_FREETYPE_STAGE_PREFIX)/include/freetype2

ifeq ($(filter $(WINE_VERSION),$(SUPPORTED_WINE_VERSIONS)),)
$(error Unsupported WINE_VERSION '$(WINE_VERSION)'; supported values: $(SUPPORTED_WINE_VERSIONS))
endif

# Detect platform
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

# On macOS ARM64, prefix commands with arch -x86_64
ifeq ($(UNAME_S),Darwin)
  ifeq ($(UNAME_M),arm64)
    ARCH_PREFIX := arch -x86_64
  endif
endif

# Test executables
TEST_EXES := build/locktest.exe build/lockstress.exe build/fonttest.exe build/edittest.exe build/fdleaktest.exe

.PHONY: all prove clone patch build test test-tools install clean help build-dir macos-freetype-prefix

all: prove

help:
	@echo "Targets:"
	@echo "  make prove                         — full pipeline for wine-$(WINE_VERSION)"
	@echo "  make prove WINE_VERSION=10.0       — full pipeline for the wine-10.0 backport"
	@echo "  make test-tools                    — compile test .exe files"
	@echo "  make clone                         — clone Wine $(WINE_TAG) source"
	@echo "  make patch                         — apply $(PATCH_SERIES) and $(WORKAROUND_SERIES)"
	@echo "  make build                         — configure + build patched Wine"
	@echo "  make test                          — run tests against patched Wine"
	@echo "  make install                       — install to $(PREFIX)"
	@echo "  make clean                         — remove build artifacts for $(WINE_VERSION)"

# ── Test tool compilation ─────────────────────────────────────────

build/locktest.exe: tests/locktest.c | build-dir
	$(MINGW_GCC) -o $@ $< -lntdll

build/lockstress.exe: tests/lockstress.c | build-dir
	$(MINGW_GCC) -o $@ $<

build/fonttest.exe: tests/fonttest.c | build-dir
	$(MINGW_GCC) -o $@ $< -lgdi32 -luser32

build/edittest.exe: tests/edittest.c | build-dir
	$(MINGW_GCC) -o $@ $< -lgdi32 -luser32

build/fdleaktest.exe: tests/fdleaktest.c | build-dir
	$(MINGW_GCC) -o $@ $<

build-dir:
	mkdir -p build

test-tools: $(TEST_EXES)

# ── Clone Wine source ─────────────────────────────────────────────

clone: $(WINE_SRC)/.git

$(WINE_SRC)/.git:
	git clone --depth 1 --branch $(WINE_TAG) https://gitlab.winehq.org/wine/wine.git $(WINE_SRC)

# ── Apply patches ─────────────────────────────────────────────────

.patch-applied-$(WINE_VERSION): $(PATCH_SERIES) $(WORKAROUND_SERIES) tools/apply-series.sh | $(WINE_SRC)/.git
	./tools/apply-series.sh "$(WINE_SRC)" "$(PATCH_SERIES)" "$(WORKAROUND_SERIES)"
	touch $@

patch: .patch-applied-$(WINE_VERSION)

# ── Configure + Build ─────────────────────────────────────────────

macos-freetype-prefix:
ifeq ($(UNAME_S),Darwin)
	@if [ -f "$(MACOS_FREETYPE_LIB)" ]; then \
		exit 0; \
	fi
	@test -f "$(MACOS_FREETYPE_STAGE_LIB)" || { \
		echo "missing $(MACOS_FREETYPE_STAGE_LIB); install x86_64 FreeType into $(MACOS_FREETYPE_PREFIX) or stage it under /tmp/freetype-x86"; \
		exit 1; \
	}
	mkdir -p "$(MACOS_FREETYPE_LIBDIR)" "$(MACOS_FREETYPE_INCLUDEDIR)"
	cp -R "$(MACOS_FREETYPE_STAGE_LIBDIR)/." "$(MACOS_FREETYPE_LIBDIR)/"
	cp -R "$(MACOS_FREETYPE_STAGE_INCLUDE)/." "$(MACOS_FREETYPE_INCLUDEDIR)/"
else
	@true
endif

$(BUILD_DIR)/Makefile: .patch-applied-$(WINE_VERSION) macos-freetype-prefix
	mkdir -p $(BUILD_DIR)
ifeq ($(UNAME_S),Darwin)
	cd $(BUILD_DIR) && PATH="/opt/homebrew/opt/bison/bin:$(PATH)" $(ARCH_PREFIX) ../configure \
		--enable-archs=x86_64,i386 --with-mingw --prefix=$(PREFIX) \
		CC="clang -arch x86_64" CXX="clang++ -arch x86_64" \
		FREETYPE_CFLAGS="-I$(MACOS_FREETYPE_INCLUDEDIR)" \
		FREETYPE_LIBS="-L$(MACOS_FREETYPE_LIBDIR) -lfreetype"
else
	cd $(BUILD_DIR) && ../configure \
		--enable-archs=x86_64,i386 --with-mingw --prefix=$(PREFIX)
endif

$(BUILD_DIR)/server/wineserver: $(BUILD_DIR)/Makefile
ifeq ($(UNAME_S),Darwin)
	cd $(BUILD_DIR) && PATH="/opt/homebrew/opt/bison/bin:$(PATH)" $(ARCH_PREFIX) make -j$(NCPU)
else
	cd $(BUILD_DIR) && $(ARCH_PREFIX) make -j$(NCPU)
endif

build: $(BUILD_DIR)/server/wineserver

# ── Install ───────────────────────────────────────────────────────

install: $(BUILD_DIR)/server/wineserver
	WINEPREFIX=$(CURDIR)/build/.wine-$(WINE_VERSION) $(WINESERVER) -k 2>/dev/null || true
	sleep 1
	cd $(BUILD_DIR) && $(ARCH_PREFIX) make install
	@echo "Installed $(WINE_TAG) to $(PREFIX)"

# ── Run tests ─────────────────────────────────────────────────────

WINE_CMD := $(BUILD_DIR)/wine
WINESERVER := $(BUILD_DIR)/server/wineserver
RESULTS_DIR := build/results/$(WINE_VERSION)

test: $(TEST_EXES) $(BUILD_DIR)/server/wineserver
	@mkdir -p $(RESULTS_DIR)
	@echo ""
	@echo "============================================================"
	@echo "  Running tests (patched $(WINE_TAG))"
	@echo "============================================================"
	@echo ""
	WINEPREFIX=$(CURDIR)/build/.wine-$(WINE_VERSION) WINESERVER=$(WINESERVER) \
		$(WINE_CMD) wineboot --init 2>/dev/null || true
	@sleep 3
	@echo "--- locktest ---"
	WINEPREFIX=$(CURDIR)/build/.wine-$(WINE_VERSION) WINESERVER=$(WINESERVER) \
		$(WINE_CMD) ./build/locktest.exe -v 2>/dev/null \
		| tee $(RESULTS_DIR)/locktest.txt | grep -E "Results:|FAIL" || true
	@echo ""
	@echo "--- lockstress ---"
	WINEPREFIX=$(CURDIR)/build/.wine-$(WINE_VERSION) WINESERVER=$(WINESERVER) \
		timeout 30 $(WINE_CMD) ./build/lockstress.exe 2>/dev/null \
		| tee $(RESULTS_DIR)/lockstress.txt | grep -E "Completed:|Hangs:|Time:" || true
	@echo ""
	@echo "--- fonttest ---"
	WINEPREFIX=$(CURDIR)/build/.wine-$(WINE_VERSION) WINESERVER=$(WINESERVER) \
		WINEDEBUG=fixme+font $(WINE_CMD) ./build/fonttest.exe \
		>$(RESULTS_DIR)/fonttest-stdout.txt 2>$(RESULTS_DIR)/fonttest-stderr.txt || true
	@cat $(RESULTS_DIR)/fonttest-stdout.txt
	@FIXME_COUNT=$$(grep -c 'charset 255' $(RESULTS_DIR)/fonttest-stderr.txt 2>/dev/null || echo 0); \
		echo "charset 255 FIXMEs: $$FIXME_COUNT"
	@echo ""
	@echo "--- edittest ---"
	WINEPREFIX=$(CURDIR)/build/.wine-$(WINE_VERSION) WINESERVER=$(WINESERVER) \
		$(WINE_CMD) ./build/edittest.exe \
		>$(RESULTS_DIR)/edittest-stdout.txt 2>$(RESULTS_DIR)/edittest-stderr.txt || true
	@cat $(RESULTS_DIR)/edittest-stdout.txt
	@EDIT_FIXMES=$$(grep -c 'EDIT_BuildLineDefs_ML' $(RESULTS_DIR)/edittest-stderr.txt 2>/dev/null || echo 0); \
		echo "EDIT_BuildLineDefs_ML FIXMEs: $$EDIT_FIXMES"
	@echo ""
	@echo "--- fdleaktest ---"
	WINEPREFIX=$(CURDIR)/build/.wine-$(WINE_VERSION) WINESERVER=$(WINESERVER) \
		$(WINE_CMD) ./build/fdleaktest.exe -v 2>/dev/null \
		| tee $(RESULTS_DIR)/fdleaktest.txt | grep -E "Results:|FAIL|All tests" || true
	@echo ""
	@echo "Results saved to $(RESULTS_DIR)/"

# ── Full pipeline ─────────────────────────────────────────────────

prove: clone patch test-tools build test

# ── Clean ─────────────────────────────────────────────────────────

clean:
	rm -rf build/ .patch-applied-$(WINE_VERSION)
	@echo "Cleaned build artifacts for $(WINE_VERSION). Run 'make clean-all WINE_VERSION=$(WINE_VERSION)' to remove $(WINE_SRC)."

clean-all: clean
	rm -rf $(WINE_SRC)
