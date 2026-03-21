# space-wine Makefile
#
# Targets:
#   make test-tools    — compile test .exe files (requires mingw-w64)
#   make clone         — clone Wine 11.0 source
#   make patch         — apply all patches to wine-src/
#   make build         — configure + build patched Wine
#   make test          — run all tests against patched Wine
#   make prove         — full pipeline: clone, patch, build, test
#   make clean         — remove build artifacts
#
# Variables (override on command line):
#   WINE_SRC    — Wine source directory (default: wine-src)
#   BUILD_DIR   — build output directory (default: $(WINE_SRC)/build)
#   WINE_TAG    — Wine git tag to clone (default: wine-11.0)
#   PREFIX      — Wine install prefix (default: /opt/wine)
#   NCPU        — parallel build jobs (auto-detected)

WINE_TAG    ?= wine-11.0
WINE_SRC    ?= wine-src
BUILD_DIR   ?= $(WINE_SRC)/build
PREFIX      ?= /opt/wine
NCPU        ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
MINGW_GCC   ?= x86_64-w64-mingw32-gcc

# Detect platform
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

# On macOS ARM64, prefix commands with arch -x86_64
ifeq ($(UNAME_S),Darwin)
  ifeq ($(UNAME_M),arm64)
    ARCH_PREFIX := arch -x86_64
  endif
endif

# Patches (applied in order)
PATCHES := \
	patches/ntdll-fix-NtLockFile-FIXMEs.patch \
	patches/win32u-fix-OEM_CHARSET.patch \
	patches/kernelbase-fix-UnlockFileEx.patch \
	patches/user32-fix-edit-BuildLineDefs.patch \
	patches/comctl32-fix-edit-BuildLineDefs.patch \
	patches/kernel32-tests-expand-lockfile.patch

# Test executables
TEST_EXES := build/locktest.exe build/lockstress.exe build/fonttest.exe build/edittest.exe build/fdleaktest.exe

.PHONY: all prove clone patch build test test-tools install clean help

all: prove

help:
	@echo "Targets:"
	@echo "  make prove       — full pipeline: clone, patch, build, test"
	@echo "  make test-tools  — compile test .exe files"
	@echo "  make clone       — clone Wine $(WINE_TAG) source"
	@echo "  make patch       — apply all patches to $(WINE_SRC)/"
	@echo "  make build       — configure + build patched Wine"
	@echo "  make test        — run tests against patched Wine"
	@echo "  make install     — install to $(PREFIX)"
	@echo "  make clean       — remove build artifacts"

# ── Test tool compilation ─────────────────────────────────────────

build/locktest.exe: tests/locktest.c | build
	$(MINGW_GCC) -o $@ $< -lntdll

build/lockstress.exe: tests/lockstress.c | build
	$(MINGW_GCC) -o $@ $<

build/fonttest.exe: tests/fonttest.c | build
	$(MINGW_GCC) -o $@ $< -lgdi32 -luser32

build/edittest.exe: tests/edittest.c | build
	$(MINGW_GCC) -o $@ $< -lgdi32 -luser32

build/fdleaktest.exe: tests/fdleaktest.c | build
	$(MINGW_GCC) -o $@ $<

build:
	mkdir -p build

test-tools: $(TEST_EXES)

# ── Clone Wine source ─────────────────────────────────────────────

clone: $(WINE_SRC)/.git

$(WINE_SRC)/.git:
	git clone --depth 1 --branch $(WINE_TAG) https://gitlab.winehq.org/wine/wine.git $(WINE_SRC)

# ── Apply patches ─────────────────────────────────────────────────

.patch-applied: $(PATCHES) | $(WINE_SRC)/.git
	cd $(WINE_SRC) && for p in $(addprefix ../,$(PATCHES)); do git apply "$$p"; done
	touch .patch-applied

patch: .patch-applied

# ── Configure + Build ─────────────────────────────────────────────

$(BUILD_DIR)/Makefile: .patch-applied
	mkdir -p $(BUILD_DIR)
ifeq ($(UNAME_S),Darwin)
	cd $(BUILD_DIR) && $(ARCH_PREFIX) ../configure \
		--enable-archs=x86_64,i386 --with-mingw --prefix=$(PREFIX) \
		FREETYPE_CFLAGS="-I/tmp/freetype-x86/include/freetype2" \
		FREETYPE_LIBS="-L/tmp/freetype-x86/lib -lfreetype"
else
	cd $(BUILD_DIR) && ../configure \
		--enable-archs=x86_64,i386 --with-mingw --prefix=$(PREFIX)
endif

$(BUILD_DIR)/server/wineserver: $(BUILD_DIR)/Makefile
	cd $(BUILD_DIR) && $(ARCH_PREFIX) make -j$(NCPU)

build: $(BUILD_DIR)/server/wineserver

# ── Install ───────────────────────────────────────────────────────

install: $(BUILD_DIR)/server/wineserver
	pkill -f wineserver || true
	sleep 1
	cd $(BUILD_DIR) && $(ARCH_PREFIX) make install
ifeq ($(UNAME_S),Darwin)
	cp /tmp/freetype-x86/lib/libfreetype.6.dylib $(PREFIX)/lib/
endif
	@echo "Installed to $(PREFIX)"

# ── Run tests ─────────────────────────────────────────────────────

WINE_CMD    := $(BUILD_DIR)/wine
WINESERVER  := $(BUILD_DIR)/server/wineserver
RESULTS_DIR := build/results

test: $(TEST_EXES) $(BUILD_DIR)/server/wineserver
	@mkdir -p $(RESULTS_DIR)
	@echo ""
	@echo "============================================================"
	@echo "  Running tests (patched Wine)"
	@echo "============================================================"
	@echo ""
	@# Init prefix
	WINEPREFIX=$(CURDIR)/build/.wine-test WINESERVER=$(WINESERVER) \
		$(WINE_CMD) wineboot --init 2>/dev/null || true
	@sleep 3
	@# locktest
	@echo "--- locktest ---"
	WINEPREFIX=$(CURDIR)/build/.wine-test WINESERVER=$(WINESERVER) \
		$(WINE_CMD) ./build/locktest.exe -v 2>/dev/null \
		| tee $(RESULTS_DIR)/locktest.txt | grep -E "Results:|FAIL" || true
	@echo ""
	@# lockstress
	@echo "--- lockstress ---"
	WINEPREFIX=$(CURDIR)/build/.wine-test WINESERVER=$(WINESERVER) \
		timeout 30 $(WINE_CMD) ./build/lockstress.exe 2>/dev/null \
		| tee $(RESULTS_DIR)/lockstress.txt | grep -E "Completed:|Hangs:|Time:" || true
	@echo ""
	@# fonttest
	@echo "--- fonttest ---"
	WINEPREFIX=$(CURDIR)/build/.wine-test WINESERVER=$(WINESERVER) \
		WINEDEBUG=fixme+font $(WINE_CMD) ./build/fonttest.exe \
		>$(RESULTS_DIR)/fonttest-stdout.txt 2>$(RESULTS_DIR)/fonttest-stderr.txt || true
	@cat $(RESULTS_DIR)/fonttest-stdout.txt
	@FIXME_COUNT=$$(grep -c 'charset 255' $(RESULTS_DIR)/fonttest-stderr.txt 2>/dev/null || echo 0); \
		echo "charset 255 FIXMEs: $$FIXME_COUNT"
	@echo ""
	@# edittest
	@echo "--- edittest ---"
	WINEPREFIX=$(CURDIR)/build/.wine-test WINESERVER=$(WINESERVER) \
		$(WINE_CMD) ./build/edittest.exe \
		>$(RESULTS_DIR)/edittest-stdout.txt 2>$(RESULTS_DIR)/edittest-stderr.txt || true
	@cat $(RESULTS_DIR)/edittest-stdout.txt
	@EDIT_FIXMES=$$(grep -c 'EDIT_BuildLineDefs_ML' $(RESULTS_DIR)/edittest-stderr.txt 2>/dev/null || echo 0); \
		echo "EDIT_BuildLineDefs_ML FIXMEs: $$EDIT_FIXMES"
	@echo ""
	@# fdleaktest
	@echo "--- fdleaktest ---"
	WINEPREFIX=$(CURDIR)/build/.wine-test WINESERVER=$(WINESERVER) \
		$(WINE_CMD) ./build/fdleaktest.exe -v 2>/dev/null \
		| tee $(RESULTS_DIR)/fdleaktest.txt | grep -E "Results:|FAIL|All tests" || true
	@echo ""
	@echo "Results saved to $(RESULTS_DIR)/"

# ── Full pipeline ─────────────────────────────────────────────────

prove: clone patch test-tools build test

# ── Clean ─────────────────────────────────────────────────────────

clean:
	rm -rf build/ .patch-applied
	@echo "Cleaned build artifacts. Run 'make clean-all' to also remove wine-src/."

clean-all: clean
	rm -rf $(WINE_SRC)
