# Copied from color's Makefile, which came from security's, font's and model's,
# per CONVENTIONS.md section 12. SUITE, PROJECT and the dependency list are what
# changed; color's oracle section and its fuzz rules are removed rather than
# renamed. Story 16 added this library's own fuzz target (tests/fuzz/fuzz_render.c:
# a list of degenerate paths, glyph runs, clips and groups rendered through the
# sink under small limits, caller memory, damage and cancel, CAP-15), replayed
# by `make test` and `make test-asan`.
#
# What this library adds to the usual set is the shape family's determinism and
# structure gates (AD-2, AD-17 and AD-22 of the shape-family spine), each in
# TEST_GATES and each shown to fail by check-gates:
#
#   check-fp-contract  every compile-flag set names -ffp-contract=off
#   check-edges        #includes and NEEDED entries stay inside EDGES_ALLOWED
#   check-libm         no libm function is referenced or defined
#   check-no-x87       no x87 instruction in the objects (the i686 arm is run
#                      by suite/tools/xarch/shape.sh, which needs a cross compiler)
#   check-gates        plants a violation for each of the above and requires
#                      the gate to fail on it, and to pass its control
#
# The test-executable rules keep section 6's shape: the static archive is a
# normal prerequisite of every test because the recipe links it, and the .so
# is order-only because check-symbols wants it and the tests do not link it.

SUITE := ghoti.io
PROJECT := shape-cpu

# The libraries this one depends on: the fourth field of its line in
# suite/libraries.txt, without the optional ones. Each is found by pkg-config
# and is a hard error when it is not. This is what the sources include: the
# coverage path needs shape-geometry alone, and the sink (story 13b) adds
# shape-scene's contract, image's GIMG_Raster targets, math's gmath_pow, color's
# gamut and transfer names and cutil's checked arithmetic. In
# suite/libraries.txt order.
DEPS := cutil color math image shape-geometry shape-scene

# The libraries whose headers this one may include and whose shared objects it
# may link (check-edges), as a regular expression alternation. Itself is always
# allowed. It is wider than DEPS where a dependency's own public headers
# include a further library (AD-2: transitive reach is not a manifest edge, but
# it is not a violation to include what a dependency's header already pulls in).
EDGES_ALLOWED := shape-geometry|shape-scene|image|color|cutil|math

# no, for every library: nothing is exempt from check-libm (AD-22, amended
# 2026-10-18; math is judged too). Anything else fails check-gates.
LIBM_EXEMPT := no

BUILD ?= release
MAJOR_VERSION := 0
MINOR_VERSION := 0.0
VERSION_MINOR_ONLY := $(word 1,$(subst ., ,$(MINOR_VERSION)))
VERSION_PATCH_ONLY := $(or $(word 2,$(subst ., ,$(MINOR_VERSION))),0)
VERSION := $(MAJOR_VERSION).$(MINOR_VERSION)

# Names this build everywhere: the .pc file, the install directory, the soname
# and the symbol token. Defaults to the major version. Override for a build
# that wants its own identity: make BRANCH=-dev
BRANCH ?= -$(MAJOR_VERSION)

ifeq ($(BRANCH),-$(MAJOR_VERSION))
VERSION_STRING := $(VERSION)
else
VERSION_STRING := $(VERSION)$(BRANCH)
endif

# override: BRANCH may have come from the command line, and a command-line
# variable otherwise beats a plain assignment. Without it, `make BRANCH=-dev
# BUILD=debug` produced a debug build carrying the release token.
ifeq ($(BUILD),debug)
    override BRANCH := $(BRANCH)-debug
    override VERSION_STRING := $(VERSION_STRING)-debug
endif

# Decided here, before the platform rewrite below. `BUILD := linux/$(BUILD)`
# is a plain assignment, so a command-line BUILD=debug stays "debug" and an
# environment BUILD=debug becomes "linux/debug". Testing it up here is true
# in both cases. Release is -O2 because that is what ships.
ifeq ($(BUILD),debug)
OPT_CFLAGS := -O0
else
OPT_CFLAGS := -O2
endif

# The shared object records only the libraries it uses (AD-2: transitive reach
# does not count). pkg-config --libs of a dependency lists that dependency's own
# dependencies too (image brings compress and security), and without this they
# would all appear in NEEDED, where check-edges rightly refuses them.
ifeq ($(shell uname -s),Linux)
AS_NEEDED := -Wl,--as-needed
else
AS_NEEDED :=
endif

BASE_NAME := lib$(SUITE)-$(PROJECT)$(BRANCH).so
LIBVER_SYMBOL := $(shell echo "ghotiio_$(PROJECT)$(BRANCH)" | sed 's/[.-]/_/g')
BASE_NAME_PREFIX := lib$(SUITE)-$(PROJECT)$(BRANCH)
SO_NAME := $(BASE_NAME).$(MAJOR_VERSION)
STATIC_TARGET := $(BASE_NAME_PREFIX).a
ENV_VARS :=

# Do not assign PKG_CONFIG_PATH. Make exports an inherited variable with
# whatever value the makefile last gave it, so overwriting it handed every
# sub-make a different path from the parent's.
PKG_CONFIG_PATH_ENV := $(PKG_CONFIG_PATH)

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S), Linux)
	OS_NAME := Linux
	LIB_EXTENSION := so
	OS_SPECIFIC_CXX_FLAGS := -shared
	OS_SPECIFIC_LIBRARY_NAME_FLAG := -Wl,-soname,$(SO_NAME)
	TARGET := $(SO_NAME).$(MINOR_VERSION)
	EXE_EXTENSION :=
	PC_INSTALL_PATH := /usr/local/share/pkgconfig
	INCLUDE_INSTALL_PATH := /usr/local/include
	LIB_INSTALL_PATH := /usr/local/lib
	PC_INCLUDE_DIR := $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)
	PC_LIB_DIR := $(LIB_INSTALL_PATH)/$(SUITE)
	override BUILD := linux/$(BUILD)

else ifeq ($(UNAME_S), Darwin)
	OS_NAME := Mac
	LIB_EXTENSION := dylib
	OS_SPECIFIC_CXX_FLAGS := -shared
	OS_SPECIFIC_LIBRARY_NAME_FLAG := -Wl,-install_name,$(BASE_NAME_PREFIX).dylib
	TARGET := $(BASE_NAME_PREFIX).dylib
	EXE_EXTENSION :=
	PC_INSTALL_PATH := /usr/local/share/pkgconfig
	INCLUDE_INSTALL_PATH := /usr/local/include
	LIB_INSTALL_PATH := /usr/local/lib
	PC_INCLUDE_DIR := $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)
	PC_LIB_DIR := $(LIB_INSTALL_PATH)/$(SUITE)
	override BUILD := mac/$(BUILD)

# TODO(windows): the Windows branches in this file were adapted from color's
# and font's and have never been run for this library. suite/tools/xwin/shape.sh
# runs the cross-target hash fixture under wine, not this Makefile.
else ifeq ($(findstring MINGW32_NT,$(UNAME_S)),MINGW32_NT)
	OS_NAME := Windows
	LIB_EXTENSION := dll
	OS_SPECIFIC_CXX_FLAGS := -shared
	OS_SPECIFIC_LIBRARY_NAME_FLAG = -Wl,--out-implib,$(APP_DIR)/$(BASE_NAME_PREFIX).dll.a
	TARGET := $(BASE_NAME_PREFIX).dll
	EXE_EXTENSION := .exe
	PC_INSTALL_PATH := /mingw32/lib/pkgconfig
	INCLUDE_INSTALL_PATH := /mingw32/include
	LIB_INSTALL_PATH := /mingw32/lib
	BIN_INSTALL_PATH := /mingw32/bin
	PC_INCLUDE_DIR = $(shell cygpath -m $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH))
	PC_LIB_DIR = $(shell cygpath -m $(LIB_INSTALL_PATH)/$(SUITE))
	override BUILD := win32/$(BUILD)

else ifeq ($(findstring MINGW64_NT,$(UNAME_S)),MINGW64_NT)
	OS_NAME := Windows
	LIB_EXTENSION := dll
	OS_SPECIFIC_CXX_FLAGS := -shared
	OS_SPECIFIC_LIBRARY_NAME_FLAG = -Wl,--out-implib,$(APP_DIR)/$(BASE_NAME_PREFIX).dll.a
	TARGET := $(BASE_NAME_PREFIX).dll
	EXE_EXTENSION := .exe
	PC_INSTALL_PATH := /mingw64/lib/pkgconfig
	INCLUDE_INSTALL_PATH := /mingw64/include
	LIB_INSTALL_PATH := /mingw64/lib
	BIN_INSTALL_PATH := /mingw64/bin
	PC_INCLUDE_DIR = $(shell cygpath -m $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH))
	PC_LIB_DIR = $(shell cygpath -m $(LIB_INSTALL_PATH)/$(SUITE))
	override BUILD := win64/$(BUILD)

else
    $(error Unsupported OS: $(UNAME_S))
endif

ifdef PREFIX
INCLUDE_INSTALL_PATH := $(PREFIX)/include
LIB_INSTALL_PATH := $(PREFIX)/lib
BIN_INSTALL_PATH := $(PREFIX)/bin
PC_INSTALL_PATH := $(PREFIX)/share/pkgconfig
ifeq ($(OS_NAME), Windows)
PC_INCLUDE_DIR = $(shell cygpath -m $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH))
PC_LIB_DIR = $(shell cygpath -m $(LIB_INSTALL_PATH)/$(SUITE))
else
PC_INCLUDE_DIR := $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)
PC_LIB_DIR := $(LIB_INSTALL_PATH)/$(SUITE)
endif
LDCONF_INSTALL_PATH :=
endif

PKG_CONFIG_LOOKUP_PATH := $(if $(PKG_CONFIG_PATH_ENV),$(PKG_CONFIG_PATH_ENV):)$(PC_INSTALL_PATH)

# AD-17 and AD-26: a 32-bit x86 host computes a double in SSE, not in the x87
# stack, whose 80-bit registers round a sum twice. A cross build for i686 gets
# the same flags from xarch_fp_cflags in suite/tools/xarch/targets.sh.
ifneq ($(filter i386 i486 i586 i686,$(shell uname -m)),)
ARCH_FP_FLAGS := -msse2 -mfpmath=sse
else
ARCH_FP_FLAGS :=
endif

# -ffp-contract=off is named, not inherited (CONVENTIONS section 6): a fused
# multiply-add rounds once where the reference rounds twice. GCC contracts by
# default only in the GNU modes and clang's default is "on". Every set below
# that names a -std carries it, and tools/check-fp-contract.sh fails when one
# does not, the sanitizer sets included. A fuzz build added later needs its
# own FUZZ_CFLAGS and FUZZ_CXXFLAGS naming it, which the gate will then read.
CXX := g++
CXXFLAGS := -pedantic-errors -Wall -Wextra -Werror -Wno-error=unused-function -Wfatal-errors -std=c++20 -O1 -g -ffp-contract=off $(ARCH_FP_FLAGS) $(EXTRA_CXXFLAGS)
CC := cc
# -Wstrict-aliasing=1 and -fstrict-aliasing, named rather than inherited.
# -Wall sets the aliasing warning to level 3, which is silent on the probe
# check-aliasing compiles. An explicit level beats -Wall from either side.
# -fstrict-aliasing is off below -O2 unless named, so a debug or coverage
# tree would otherwise have the warning armed and the assumption off.
# The probe is a pointer parameter stored through a second variable: that
# shape is reported at level 1 and silent at 0, 2, and 3. Do not simplify it
# to `*(int *)&local`, which fires at every level from 1 up and certifies
# nothing. check-aliasing is the measurement.
CFLAGS := -pedantic-errors -Wall -Wextra -Werror -Wfloat-conversion -fstrict-aliasing -Wstrict-aliasing=1 -Wno-error=unused-function -Wfatal-errors -std=c17 $(OPT_CFLAGS) -ffp-contract=off $(ARCH_FP_FLAGS) -g $(EXTRA_CFLAGS)
ifeq ($(OS_NAME), Windows)
CFLAGS += -DGSHCPU_STATIC
CXXFLAGS += -DGSHCPU_STATIC
endif
LIB_CFLAGS := $(CFLAGS) -fvisibility=hidden -DGSHCPU_BUILD $(EXTRA_CFLAGS)
LDFLAGS := -L /usr/lib -lstdc++ -lm $(EXTRA_LDFLAGS)
ifdef PREFIX
LDFLAGS += -Wl,-rpath,$(LIB_INSTALL_PATH)/$(SUITE)
ifeq ($(OS_NAME), Windows)
export PATH := $(BIN_INSTALL_PATH):$(PATH)
endif
endif

BUILD_DIR := ./build/$(BUILD)
OBJ_DIR := $(BUILD_DIR)/objects
FLAGS_STAMP := $(OBJ_DIR)/.flags
GEN_DIR := $(BUILD_DIR)/generated
APP_DIR := $(BUILD_DIR)/apps

ifeq ($(UNAME_S), Linux)
	LIB_CFLAGS += -fPIC
endif

INCLUDE := -I include/ -I $(GEN_DIR)/

# libver-gen is here so that suite/tools/xarch/shape.sh can write the generated
# header for a cross build without any dependency installed.
DEPLESS_GOALS := docs docs-pdf clean fuzz-clean cloc help libver-gen
ifeq ($(filter-out $(DEPLESS_GOALS),$(or $(MAKECMDGOALS),all)),)
SKIP_DEP_CHECK := 1
endif

# The names carry $(BRANCH). ghoti.io-cutil never matches the installed file.
DEP_PCS := $(foreach d,$(DEPS),ghoti.io-$(d)$(BRANCH))
$(foreach pc,$(DEP_PCS),$(if $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --exists $(pc) 2>/dev/null && echo yes),,$(if $(SKIP_DEP_CHECK),,$(error $(pc) was not found by pkg-config. Run ./bootstrap.sh at the root of the workspace - two levels up, the directory holding libs/ - to build and install the suite into a local prefix, then pass the same PREFIX here - or point PKG_CONFIG_PATH at the directory holding its .pc file. There is deliberately no sibling-checkout fallback.))))
DEP_CFLAGS := $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --cflags $(DEP_PCS) 2>/dev/null)
DEP_LIBS := $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --libs $(DEP_PCS) 2>/dev/null)
INCLUDE += $(DEP_CFLAGS)

SOURCES := $(shell find src -type f -name '*.c')
LIBOBJECTS := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(SOURCES))

TESTFLAGS := `PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --libs --cflags gtest`

# coverage clears this: --coverage links the gcov runtime, whose mangle_path
# check-symbols is right to reject in a shipping library.
PLANT_JOBS ?= 4

TEST_GATES ?= check-symbols check-aliasing check-stamps check-fp-contract check-edges check-libm check-no-x87 check-gates check-vectors check-corpus-hash check-fuzz-corpus fuzz-replay run-examples

VALGRIND_FLAGS := --leak-check=full --show-leak-kinds=definite,indirect,possible --track-origins=yes --error-exitcode=1 --suppressions=tests/valgrind.supp

TEST_HELPER_SRC := $(wildcard tests/test_helpers.cpp)
TEST_HELPER_OBJ := $(patsubst tests/%.cpp,$(OBJ_DIR)/tests/%.o,$(TEST_HELPER_SRC))
# The tests' second engines, C compiled for test programs only (tests/support/exact_s4b.c: the exact step of strokes
# story S4b, frozen, the other side of the dual render).  Never part of the library or its archive.
TEST_SUPPORT_SRC := $(wildcard tests/support/*.c)
TEST_SUPPORT_OBJ := $(patsubst tests/%.c,$(OBJ_DIR)/tests/%.o,$(TEST_SUPPORT_SRC))

# The archive, not the shared library: a static link resolves hidden symbols.
# --whole-archive because a constructor-registered object would otherwise be
# dropped. The archive is a normal prerequisite of every test, so a clean
# tree builds it; the .so is order-only because check-symbols wants it and
# the tests do not link it.
SELF_LIBRARY := -Wl,--whole-archive $(APP_DIR)/$(STATIC_TARGET) -Wl,--no-whole-archive $(DEP_LIBS)

ifeq ($(OS_NAME), Windows)
TEST_LDFLAGS := -Wl,--wrap=__mingw_fprintf
else
TEST_LDFLAGS :=
endif

TEST_PAIRS := $(shell find tests -type f -name 'test_*.cpp' 2>/dev/null | sort | grep -v test_helpers | while read f; do \
	echo "$$f|$$(basename "$$f" .cpp | sed 's/test_/test/; s/^test\([a-z]\)/test\U\1/')"; done)
TEST_SOURCES := $(foreach pair,$(TEST_PAIRS),$(word 1,$(subst |, ,$(pair))))
TEST_NAMES := $(foreach pair,$(TEST_PAIRS),$(word 2,$(subst |, ,$(pair))))
TEST_EXECUTABLES := $(addprefix $(APP_DIR)/,$(addsuffix $(EXE_EXTENSION),$(TEST_NAMES)))

EXAMPLE_SOURCES := $(shell find examples -type f -name '*.c' 2>/dev/null)
EXAMPLES := $(patsubst examples/%.c,$(APP_DIR)/examples/%$(EXE_EXTENSION),$(EXAMPLE_SOURCES))

all: $(APP_DIR)/$(TARGET) $(APP_DIR)/$(STATIC_TARGET) ## Build shared + static libraries

TEST_DEPFILES := $(foreach pair,$(TEST_PAIRS),$(OBJ_DIR)/tests/$(basename $(notdir $(word 1,$(subst |, ,$(pair))))).d)
DEPFILES := $(LIBOBJECTS:.o=.d) $(TEST_HELPER_OBJ:.o=.d) $(TEST_SUPPORT_OBJ:.o=.d) $(TEST_DEPFILES)
-include $(DEPFILES)

LIBVER_GEN := $(GEN_DIR)/ghoti.io/$(PROJECT)/libver_gen.h

.PHONY: force-libver
force-libver:

$(LIBVER_GEN): force-libver
	@if [ -z "$(LIBVER_SYMBOL)" ]; then \
		printf "### LIBVER_SYMBOL is empty ###\n" >&2; \
		printf "Every exported symbol would lose its version namespace.\n" >&2; \
		exit 1; \
	fi
	@mkdir -p $(@D)
	@printf '%s\n' \
		'// Generated by the Makefile. Do not edit; see CONVENTIONS.md section 4.' \
		'#ifndef GHOTI_IO_GSHCPU_LIBVER_GEN_H' \
		'#define GHOTI_IO_GSHCPU_LIBVER_GEN_H' \
		'' \
		'/** The symbol namespace for this build, from the Makefile'"'"'s BRANCH. */' \
		'#define GHOTIIO_SHAPE_CPU_NAME $(LIBVER_SYMBOL)' \
		'' \
		'/** Human-readable version of this build. */' \
		'#define GHOTIIO_SHAPE_CPU_VERSION "$(VERSION_STRING)"' \
		'' \
		'/** The same version as three integers. */' \
		'#define GHOTIIO_SHAPE_CPU_VERSION_MAJOR $(MAJOR_VERSION)' \
		'#define GHOTIIO_SHAPE_CPU_VERSION_MINOR $(VERSION_MINOR_ONLY)' \
		'#define GHOTIIO_SHAPE_CPU_VERSION_PATCH $(VERSION_PATCH_ONLY)' \
		'' \
		'#endif // GHOTI_IO_GSHCPU_LIBVER_GEN_H' > $@.tmp
	@if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi

libver-gen: $(LIBVER_GEN) ## Write the generated version header (needs no dependency)

$(OBJ_DIR)/%.o: src/%.c $(FLAGS_STAMP) | $(LIBVER_GEN)
	@mkdir -p $(@D)
	$(CC) $(LIB_CFLAGS) $(INCLUDE) -c $< -MMD -MP -MF $(@:.o=.d) -o $@

$(APP_DIR)/$(TARGET): $(LIBOBJECTS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -shared -o $@ $^ $(LDFLAGS) $(AS_NEEDED) $(DEP_LIBS) $(OS_SPECIFIC_LIBRARY_NAME_FLAG)
ifeq ($(OS_NAME), Linux)
	@ln -f -s $(TARGET) $(APP_DIR)/$(SO_NAME)
	@ln -f -s $(SO_NAME) $(APP_DIR)/$(BASE_NAME)
endif

$(APP_DIR)/$(STATIC_TARGET): $(LIBOBJECTS)
	@mkdir -p $(@D)
	@rm -f $@
	ar rcs $@ $^

ifneq ($(TEST_HELPER_SRC),)
$(TEST_HELPER_OBJ): $(TEST_HELPER_SRC) $(FLAGS_STAMP) | $(LIBVER_GEN)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDE) -c $< -MMD -MP -MF $(@:.o=.d) -o $@
endif

$(OBJ_DIR)/tests/support/%.o: tests/support/%.c $(FLAGS_STAMP) | $(LIBVER_GEN)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDE) -c $< -MMD -MP -MF $(@:.o=.d) -o $@

$(OBJ_DIR)/tests/%.o: tests/%.cpp $(FLAGS_STAMP) | $(LIBVER_GEN)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDE) -Itests -c $< -MMD -MP -MF $(@:.o=.d) -o $@

$(OBJ_DIR)/tests/%.o: tests/unit/%.cpp $(FLAGS_STAMP) | $(LIBVER_GEN)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDE) -Itests -c $< -MMD -MP -MF $(@:.o=.d) -o $@

define test-executable-rule
TEST_OBJ_$1 := $(OBJ_DIR)/tests/$(basename $(notdir $1)).o

$(APP_DIR)/$2$(EXE_EXTENSION): $$(TEST_OBJ_$1) $(TEST_HELPER_OBJ) $(TEST_SUPPORT_OBJ) \
		$(APP_DIR)/$(STATIC_TARGET) | $(APP_DIR)/$(TARGET)
	@mkdir -p $$(@D)
	$(CXX) $(CXXFLAGS) -o $$@ $$(TEST_OBJ_$1) $(TEST_HELPER_OBJ) $(TEST_SUPPORT_OBJ) $(LDFLAGS) $(TEST_LDFLAGS) $(SELF_LIBRARY) $(DEP_LIBS) $(TESTFLAGS)
endef

$(foreach pair,$(TEST_PAIRS),\
	$(eval $(call test-executable-rule,$(word 1,$(subst |, ,$(pair))),$(word 2,$(subst |, ,$(pair))))))

$(APP_DIR)/examples/%$(EXE_EXTENSION): examples/%.c $(APP_DIR)/$(STATIC_TARGET) \
		$(FLAGS_STAMP) | $(APP_DIR)/$(TARGET) $(LIBVER_GEN)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $< $(LDFLAGS) $(SELF_LIBRARY) $(DEP_LIBS)

.PHONY: clean cloc docs docs-pdf examples coverage check-symbols check-stamps check-aliasing
.PHONY: run-examples
.PHONY: check-fp-contract check-edges check-libm check-no-x87 check-gates libver-gen
.PHONY: check-vectors check-corpus-hash check-planted test-tsan
.PHONY: all install test test-quiet test-asan test-valgrind test-valgrind-quiet test-watch uninstall watch
.PHONY: all-debug install-debug test-debug test-valgrind-debug test-watch-debug uninstall-debug watch-debug
.PHONY: fuzz fuzz-clean check-fuzz-corpus fuzz-replay

watch: ## Watch sources and rebuild
	@while true; do \
		make --no-print-directory all; \
		inotifywait -qr -e modify -e create -e delete -e move src include tests Makefile; \
		done

test-watch: ## Watch sources and rerun the tests
	@while true; do \
		make --no-print-directory test; \
		inotifywait -qr -e modify -e create -e delete -e move src include tests Makefile; \
		done

examples: $(APP_DIR)/$(TARGET) $(EXAMPLES) ## Build the examples

# The fuzz target (story 16; CAP-15, AD-19), replayed over its committed corpus in an ordinary build: no libFuzzer,
# no clang. The replay fails on no input and on a corpus that misses one of the outcomes the target lists, and the
# corpus must be what tools/gen-fuzz-corpus.py writes.
FUZZ_INPUTS = $(shell find tests/fuzz/corpus/render -type f 2>/dev/null | sort)
REPLAY := $(APP_DIR)/fuzz/replay_render$(EXE_EXTENSION)

$(REPLAY): tests/fuzz/fuzz_render.c tests/fuzz/replay_main.c $(TEST_SUPPORT_SRC) $(APP_DIR)/$(STATIC_TARGET) \
		$(FLAGS_STAMP) | $(APP_DIR)/$(TARGET) $(LIBVER_GEN)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ tests/fuzz/fuzz_render.c tests/fuzz/replay_main.c $(TEST_SUPPORT_SRC) $(LDFLAGS) $(SELF_LIBRARY) $(DEP_LIBS)

check-fuzz-corpus: ## Fail if the committed fuzz corpus is not what tools/gen-fuzz-corpus.py writes
	@python3 tools/gen-fuzz-corpus.py --check

# The gate is seen to fail twice before it is believed: the replay built with GSHCPU_FUZZ_PLANT (one byte of the
# second render changed before the determinism check) must abort on the corpus, and the real replay over one file
# must refuse a corpus that leaves an outcome unreached.
REPLAY_PLANTED := $(APP_DIR)/fuzz/replay_render_planted$(EXE_EXTENSION)

$(REPLAY_PLANTED): tests/fuzz/fuzz_render.c tests/fuzz/replay_main.c $(TEST_SUPPORT_SRC) $(APP_DIR)/$(STATIC_TARGET) \
		$(FLAGS_STAMP) | $(APP_DIR)/$(TARGET) $(LIBVER_GEN)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DGSHCPU_FUZZ_PLANT $(INCLUDE) -o $@ tests/fuzz/fuzz_render.c tests/fuzz/replay_main.c $(TEST_SUPPORT_SRC) $(LDFLAGS) $(SELF_LIBRARY) $(DEP_LIBS)

fuzz-replay: $(REPLAY) $(REPLAY_PLANTED) ## Feed every corpus file once through the fuzz entry point
	@if [ -z "$(strip $(FUZZ_INPUTS))" ]; then printf 'fuzz-replay: the corpus is empty; this measures nothing\n' >&2; exit 1; fi
	@if LD_LIBRARY_PATH="$(TEST_LD_PATH)" $(REPLAY_PLANTED) $(FUZZ_INPUTS) > $(BUILD_DIR)/fuzz-planted.out 2>&1; then \
		printf 'fuzz-replay: FAIL: the planted replay passed; the harness checks nothing\n' >&2; exit 1; fi
	@grep -q 'invariant failed' $(BUILD_DIR)/fuzz-planted.out || { printf 'fuzz-replay: FAIL: the planted replay failed for another reason\n' >&2; cat $(BUILD_DIR)/fuzz-planted.out >&2; exit 1; }
	@if LD_LIBRARY_PATH="$(TEST_LD_PATH)" $(REPLAY) $(firstword $(FUZZ_INPUTS)) > $(BUILD_DIR)/fuzz-one.out 2>&1; then \
		printf 'fuzz-replay: FAIL: one input reached every outcome; the outcome check checks nothing\n' >&2; exit 1; fi
	@grep -q 'never reached' $(BUILD_DIR)/fuzz-one.out || { printf 'fuzz-replay: FAIL: one input failed for another reason\n' >&2; cat $(BUILD_DIR)/fuzz-one.out >&2; exit 1; }
	@printf 'fuzz-replay: the planted replay aborted and one input left outcomes unreached, as they must\n'
	@LD_LIBRARY_PATH="$(TEST_LD_PATH)" $(REPLAY) $(FUZZ_INPUTS)

# Every example is run, not only compiled (story 16): one that is only compiled rots unseen, its calls still linking
# while what it claims stops being true. Each example checks what it shows and exits non-zero when that does not
# hold; tools/run-examples.sh first shows itself failing on planted examples, then runs these. A target that runs a
# built program sets LD_LIBRARY_PATH (CONVENTIONS section 6).
run-examples: $(EXAMPLES) ## Build and run every example; fail on one that exits non-zero
	@LD_LIBRARY_PATH="$(TEST_LD_PATH)" sh tools/run-examples.sh $(BUILD_DIR)/examples-out $(EXAMPLES)

# The dependencies' library directories, from the -L words pkg-config gave, so
# a test run finds them when they are installed in a prefix of their own.
EMPTY :=
SPACE := $(EMPTY) $(EMPTY)
DEP_LIB_DIRS := $(subst $(SPACE),:,$(strip $(patsubst -L%,%,$(filter -L%,$(DEP_LIBS)))))
TEST_LD_PATH := $(APP_DIR):$(LIB_INSTALL_PATH)/$(SUITE)$(if $(DEP_LIB_DIRS),:$(DEP_LIB_DIRS))

check-aliasing: ## Fail if -Wstrict-aliasing is not armed at level 1
	@mkdir -p $(BUILD_DIR)
	@printf 'int gshcpu_alias_probe(float * f);\nint gshcpu_alias_probe(float * f) { int * i = (int *)f; *i = 7; return *i; }\n' > $(BUILD_DIR)/alias_probe.c
	@printf 'int gshcpu_alias_clean(int * i);\nint gshcpu_alias_clean(int * i) { *i = 7; return *i; }\n' > $(BUILD_DIR)/alias_clean.c
	@probe=$$($(CC) $(CFLAGS) -Wno-error -c $(BUILD_DIR)/alias_probe.c -o $(BUILD_DIR)/alias_probe.o 2>&1); \
	ctl=$$($(CC) $(CFLAGS) -Wno-error -c $(BUILD_DIR)/alias_clean.c -o $(BUILD_DIR)/alias_clean.o 2>&1); ctlrc=$$?; \
	if [ $$ctlrc -ne 0 ]; then \
		printf 'check-aliasing: the control file did not compile, so this gate is measuring nothing:\n%s\n' "$$ctl" >&2; \
		exit 1; \
	fi; \
	if printf '%s' "$$ctl" | grep -q 'strict-aliasing'; then \
		printf 'check-aliasing: the control file drew a strict-aliasing diagnostic, so the probe proves nothing:\n%s\n' "$$ctl" >&2; \
		exit 1; \
	fi; \
	if printf '%s' "$$probe" | grep -q 'strict-aliasing'; then \
		printf 'check-aliasing: the planted violation is reported\n'; \
		exit 0; \
	fi; \
	qout=$$($(CC) -Q --help=warnings $(CFLAGS) 2>/dev/null); qrc=$$?; \
	level=$$(printf '%s' "$$qout" | awk '/-Wstrict-aliasing=</{print $$2}'); \
	if [ $$qrc -ne 0 ] || [ -z "$$level" ]; then \
		printf 'check-aliasing: no diagnostic, and %s reports no -Wstrict-aliasing level. That is a compiler which accepts the option and implements nothing.\n' "$(CC)" >&2; \
	elif [ "$$level" = 1 ]; then \
		printf 'check-aliasing: level 1 is set and the planted store still drew no diagnostic.\n' >&2; \
	else \
		printf 'check-aliasing: CFLAGS resolves to -Wstrict-aliasing=%s; only level 1 reports this probe.\n' "$$level" >&2; \
	fi; \
	exit 1

check-stamps: ## Fail if a compile rule names no flags stamp, or a stamp omits a variable
	@python3 tools/check-stamps.py

####################################################################
# The shape family's gates (AD-2, AD-17, AD-22). tools/check-gates.sh
# plants a violation for each and requires the gate to fail on it.
####################################################################

check-fp-contract: ## Fail if a compile-flag set lacks -ffp-contract=off (a fused multiply-add changes bits)
	@tools/check-fp-contract.sh

check-edges: $(APP_DIR)/$(TARGET) ## Fail on an #include or NEEDED edge outside EDGES_ALLOWED (AD-2)
	@tools/check-edges.sh --allowed '$(EDGES_ALLOWED)' --self $(PROJECT) --includes .
	@tools/check-edges.sh --allowed '$(EDGES_ALLOWED)' --self $(PROJECT) --links $(APP_DIR)

check-libm: $(APP_DIR)/$(TARGET) $(APP_DIR)/$(STATIC_TARGET) ## Fail if any libm function is referenced or defined (AD-22)
	@tools/check-libm.sh $(APP_DIR)/$(TARGET) $(APP_DIR)/$(STATIC_TARGET)

check-no-x87: $(APP_DIR)/$(STATIC_TARGET) ## Fail if an x86 object holds an x87 instruction (AD-17)
	@tools/check-no-x87.sh $(APP_DIR)/$(STATIC_TARGET)

# The cross-target probe (tools/corpus-hash.c), linked against the static
# archive as the examples are.
HASH_PROBE := $(APP_DIR)/corpus-hash$(EXE_EXTENSION)

$(HASH_PROBE): tools/corpus-hash.c tests/render_fixtures.h tests/dense_vectors.h $(APP_DIR)/$(STATIC_TARGET) \
		$(FLAGS_STAMP) | $(APP_DIR)/$(TARGET) $(LIBVER_GEN)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $< $(LDFLAGS) $(SELF_LIBRARY) $(DEP_LIBS)

check-corpus-hash: $(HASH_PROBE) ## Fail unless the probe prints the committed cross-target hash (CAP-13)
	@LD_LIBRARY_PATH="$(TEST_LD_PATH)" tools/check-corpus-hash.sh $(HASH_PROBE) tests/corpus/coverage.hash tests/corpus/render.hash tests/corpus/dense.hash tests/corpus/render-fast.hash

check-vectors: ## Fail if tests/known_answers.h or tests/render_vectors.h is not what its independent generator writes
	@python3 tools/gen-coverage-vectors.py --check
	@python3 tools/gen-render-vectors.py --check
	@python3 tools/gen-dense-cells.py --check

# Each planted defect in tools/check-planted.py is built into a copy of the
# sources and must fail a test (or the probe's hash); the unplanted copy is the
# control and must pass.
check-planted: $(HASH_PROBE) $(LIBOBJECTS) $(TEST_EXECUTABLES) ## Fail unless each planted defect fails a test and the control passes
	@PLANT_CC='$(CC)' PLANT_CFLAGS='$(LIB_CFLAGS) $(INCLUDE)' \
	 PLANT_PROBE_CFLAGS='$(CFLAGS) $(INCLUDE)' PLANT_PROBE_LIBS='$(LDFLAGS) $(DEP_LIBS)' \
	 PLANT_CXX='$(CXX) $(CXXFLAGS)' PLANT_CXX_INCLUDE='$(INCLUDE) -Itests' \
	 PLANT_LDFLAGS='$(LDFLAGS) $(DEP_LIBS) $(TESTFLAGS)' \
	 PLANT_TESTS='$(TEST_NAMES)' PLANT_APP_DIR='$(APP_DIR)' PLANT_OBJ_DIR='$(OBJ_DIR)' \
	 PLANT_LD_PATH='$(TEST_LD_PATH)' PLANT_WORK='$(BUILD_DIR)/planted' PLANT_JOBS='$(PLANT_JOBS)' \
	 python3 tools/check-planted.py

check-gates: ## Prove each gate fails on its planted violation and passes its control
	@CC='$(CC)' GATE_PROJECT='$(PROJECT)' GATE_ALLOWED='$(EDGES_ALLOWED)' GATE_LIBM_EXEMPT='$(LIBM_EXEMPT)' tools/check-gates.sh

####################################################################
# Symbols
####################################################################

check-symbols: $(APP_DIR)/$(TARGET) ## Fail if any exported symbol lacks the version namespace
ifeq ($(OS_NAME), Linux)
	@leaked=$$(nm -D --defined-only $(APP_DIR)/$(TARGET) \
		| awk '$$2 ~ /^[TDBR]$$/ {print $$3}' \
		| grep -v '^$(LIBVER_SYMBOL)_' | grep -v '^_' || true); \
	if [ -n "$$leaked" ]; then \
		printf '### Exported symbols missing the $(LIBVER_SYMBOL)_ namespace ###\n%s\n' "$$leaked" >&2; \
		exit 1; \
	fi
	@unexported=$$(find include -name '*.h' -exec awk '/^#if DOXYGEN/{d=1} d==0 && /^[a-z_][A-Za-z0-9_ ]*\**[[:space:]]*gshcpu_[a-z0-9_]+[[:space:]]*\(/{print FILENAME": "$$0} /^#endif/{d=0}' {} + \
		| grep -vE 'typedef|static inline' || true); \
	if [ -n "$$unexported" ]; then \
		printf '### Public declarations without GSHCPU_API ###\n%s\n' "$$unexported" >&2; \
		exit 1; \
	fi
	@split=$$(nm -D --undefined-only $(APP_DIR)/$(TARGET) \
		| awk '{print $$2}' | grep '^$(LIBVER_SYMBOL)_' || true); \
	if [ -n "$$split" ]; then \
		printf '### Renamed but undefined - a split symbol ###\n%s\n' "$$split" >&2; \
		exit 1; \
	fi
	@nomacros=$$(find include src -name '*.h' \
		! -name 'libver.h' ! -name 'libver_gen.h' ! -name 'namespace.h' ! -name 'macros.h' \
		-exec grep -L '#include <ghoti.io/$(PROJECT)/macros.h>' {} + || true); \
	if [ -n "$$nomacros" ]; then \
		printf '### Headers that do not include macros.h ###\n%s\n' "$$nomacros" >&2; \
		exit 1; \
	fi
	@badguards=$$(find include src -name '*.h' -exec awk 'FNR==1{d=0} !d && /^#ifndef/{print $$2; d=1}' {} + \
		| awk '$$1 !~ /^GHOTI_IO_GSHCPU_/ {print $$1}' || true); \
	if [ -n "$$badguards" ]; then \
		printf '### Include guards with the wrong prefix ###\n%s\n' "$$badguards" >&2; \
		exit 1; \
	fi
	@dupguards=$$(find include src -name '*.h' -exec awk 'FNR==1{d=0} !d && /^#ifndef/{print $$2; d=1}' {} + \
		| sort | uniq -d || true); \
	if [ -n "$$dupguards" ]; then \
		printf '### Headers sharing an include guard ###\n%s\n' "$$dupguards" >&2; \
		exit 1; \
	fi
	@printf 'Every exported symbol carries the %s_ namespace.\n' "$(LIBVER_SYMBOL)"
else
	@printf 'check-symbols: skipped (Linux only)\n'
endif

test: $(APP_DIR)/$(TARGET) $(TEST_EXECUTABLES) $(TEST_GATES) ## Build and run the tests
	@for test_exe in $(TEST_EXECUTABLES); do \
		test_name=$$(basename $$test_exe $(EXE_EXTENSION)); \
		printf '\n### Running %s ###\n\n' "$$test_name"; \
		LD_LIBRARY_PATH="$(TEST_LD_PATH)" $$test_exe --gtest_brief=1 || exit 1; \
	done

test-quiet: $(APP_DIR)/$(TARGET) $(TEST_EXECUTABLES) ## Run tests, one line per suite
	@total_tests=0; total_passed=0; total_failed=0; total_time=0; failed_suites=""; any_failed=0; \
	printf '\n%-30s %8s %10s %s\n' "Test Suite" "Tests" "Time" "Status"; \
	for test_exe in $(TEST_EXECUTABLES); do \
		test_name=$$(basename $$test_exe $(EXE_EXTENSION)); \
		output=$$(LD_LIBRARY_PATH="$(TEST_LD_PATH)" $$test_exe --gtest_brief=1 2>&1); \
		exit_code=$$?; \
		num_tests=$$(echo "$$output" | grep -oP '\[\s*=+\s*\]\s*\K\d+(?=\s+tests?)' | head -1); \
		time_ms=$$(echo "$$output" | grep -oP '\(\K\d+(?=\s*ms\s*total\))' | head -1); \
		[ -z "$$num_tests" ] && num_tests=0; \
		[ -z "$$time_ms" ] && time_ms=0; \
		total_tests=$$((total_tests + num_tests)); \
		total_time=$$((total_time + time_ms)); \
		if [ $$exit_code -eq 0 ]; then \
			total_passed=$$((total_passed + num_tests)); \
			printf '%-30s %8d %8dms PASS\n' "$$test_name" "$$num_tests" "$$time_ms"; \
		else \
			any_failed=1; \
			failures=$$(echo "$$output" | grep -oP '\[\s*FAILED\s*\]\s*\K\d+' | head -1); \
			[ -z "$$failures" ] && failures=$$num_tests; \
			[ "$$failures" -eq 0 ] && failures=1; \
			total_failed=$$((total_failed + failures)); \
			total_passed=$$((total_passed + num_tests - failures)); \
			printf '%-30s %8d %8dms FAIL (exit %d)\n' "$$test_name" "$$num_tests" "$$time_ms" "$$exit_code"; \
			failed_suites="$$failed_suites\n=== $$test_name FAILURES ===\n$$output\n"; \
		fi; \
	done; \
	if [ $$any_failed -eq 0 ]; then \
		printf '%-30s %8d %6dms PASS\n\n' "TOTAL" "$$total_tests" "$$total_time"; \
	else \
		printf '%-30s %8d %6dms FAIL (%d failed)\n' "TOTAL" "$$total_tests" "$$total_time" "$$total_failed"; \
		printf '%s\n' "$$failed_suites"; \
		exit 1; \
	fi

test-valgrind: $(APP_DIR)/$(TARGET) $(TEST_EXECUTABLES) ## Run the tests under Valgrind
ifeq ($(OS_NAME), Linux)
	@for test_exe in $(TEST_EXECUTABLES); do \
		printf '\n### Valgrind %s ###\n\n' "$$(basename $$test_exe)"; \
		LD_LIBRARY_PATH="$(TEST_LD_PATH)" valgrind $(VALGRIND_FLAGS) $$test_exe --gtest_brief=1 || exit 1; \
	done
else
	@printf 'Valgrind is only available on Linux\n' >&2; exit 1
endif

test-valgrind-quiet: $(APP_DIR)/$(TARGET) $(TEST_EXECUTABLES) ## Valgrind, one line per suite
ifeq ($(OS_NAME), Linux)
	@total_tests=0; total_failed=0; total_time=0; failed_suites=""; \
	printf '\n%-30s %8s %10s %s\n' "Test Suite (Valgrind)" "Tests" "Time" "Status"; \
	for test_exe in $(TEST_EXECUTABLES); do \
		test_name=$$(basename $$test_exe $(EXE_EXTENSION)); \
		output=$$(LD_LIBRARY_PATH="$(TEST_LD_PATH)" valgrind $(VALGRIND_FLAGS) $$test_exe --gtest_brief=1 2>&1); \
		exit_code=$$?; \
		num_tests=$$(echo "$$output" | grep -oP '\[\s*=+\s*\]\s*\K\d+(?=\s+tests?)' | head -1); \
		time_ms=$$(echo "$$output" | grep -oP '\(\K\d+(?=\s*ms\s*total\))' | head -1); \
		[ -z "$$num_tests" ] && num_tests=0; \
		[ -z "$$time_ms" ] && time_ms=0; \
		total_tests=$$((total_tests + num_tests)); \
		total_time=$$((total_time + time_ms)); \
		has_leak=$$(echo "$$output" | grep -c "are definitely lost\|are indirectly lost\|are possibly lost" || true); \
		if [ $$exit_code -eq 0 ] && [ $$has_leak -eq 0 ]; then \
			printf '%-30s %8d %8dms PASS\n' "$$test_name" "$$num_tests" "$$time_ms"; \
		else \
			total_failed=$$((total_failed + 1)); \
			printf '%-30s %8d %8dms FAIL\n' "$$test_name" "$$num_tests" "$$time_ms"; \
			failed_suites="$$failed_suites\n=== $$test_name ===\n$$output\n"; \
		fi; \
	done; \
	if [ $$total_failed -eq 0 ]; then \
		printf '%-30s %8d %6dms PASS\n\n' "TOTAL" "$$total_tests" "$$total_time"; \
	else \
		printf '%-30s %8d %6dms FAIL (%d suites)\n' "TOTAL" "$$total_tests" "$$total_time" "$$total_failed"; \
		printf '%s\n' "$$failed_suites"; \
		exit 1; \
	fi
else
	@printf 'Valgrind is only available on Linux\n' >&2; exit 1
endif

####################################################################
# ASan + UBSan, in their own tree
####################################################################

UBSAN_CHECKS := undefined,float-cast-overflow
SAN_LABEL := ASan+UBSan
ASAN_UBSAN_FLAGS := -fsanitize=address,$(UBSAN_CHECKS) -fno-sanitize-recover=$(UBSAN_CHECKS) -fno-omit-frame-pointer -g -O1
COV_BUILD_DIR := ./build/$(BUILD)-cov
ASAN_BUILD_DIR := ./build/$(BUILD)-asan
ASAN_OBJ_DIR := $(ASAN_BUILD_DIR)/objects
ASAN_FLAGS_STAMP := $(ASAN_OBJ_DIR)/.flags
ASAN_APP_DIR := $(ASAN_BUILD_DIR)/apps
ASAN_LIBOBJECTS := $(patsubst src/%.c,$(ASAN_OBJ_DIR)/%.o,$(SOURCES))
ASAN_DEPFILES := $(ASAN_LIBOBJECTS:.o=.d) \
	$(foreach pair,$(TEST_PAIRS),$(ASAN_OBJ_DIR)/tests/$(basename $(notdir $(word 1,$(subst |, ,$(pair))))).d)
-include $(ASAN_DEPFILES)
ASAN_ARCHIVE := $(ASAN_APP_DIR)/$(STATIC_TARGET)
ASAN_SELF_LIBRARY := -Wl,--whole-archive $(ASAN_ARCHIVE) -Wl,--no-whole-archive $(DEP_LIBS)
ASAN_CFLAGS := $(CFLAGS) $(ASAN_UBSAN_FLAGS) -DGSHCPU_BUILD
ASAN_CXXFLAGS := $(CXXFLAGS) $(ASAN_UBSAN_FLAGS)
ASAN_LDFLAGS := $(LDFLAGS) $(ASAN_UBSAN_FLAGS)
ifeq ($(UNAME_S), Linux)
	ASAN_CFLAGS += -fPIC
endif

$(ASAN_OBJ_DIR)/%.o: src/%.c $(ASAN_FLAGS_STAMP) | $(LIBVER_GEN)
	@mkdir -p $(@D)
	$(CC) $(ASAN_CFLAGS) $(INCLUDE) -c $< -MMD -MP -MF $(@:.o=.d) -o $@

$(ASAN_ARCHIVE): $(ASAN_LIBOBJECTS)
	@mkdir -p $(@D)
	@rm -f $@
	ar rcs $@ $^

ASAN_TEST_SUPPORT_OBJ := $(patsubst tests/%.c,$(ASAN_OBJ_DIR)/tests/%.o,$(TEST_SUPPORT_SRC))

$(ASAN_OBJ_DIR)/tests/support/%.o: tests/support/%.c $(ASAN_FLAGS_STAMP) | $(LIBVER_GEN)
	@mkdir -p $(@D)
	$(CC) $(ASAN_CFLAGS) $(INCLUDE) -c $< -MMD -MP -MF $(@:.o=.d) -o $@

$(ASAN_OBJ_DIR)/tests/%.o: tests/%.cpp $(ASAN_FLAGS_STAMP) | $(LIBVER_GEN)
	@mkdir -p $(@D)
	$(CXX) $(ASAN_CXXFLAGS) $(INCLUDE) -Itests -c $< -MMD -MP -MF $(@:.o=.d) -o $@

$(ASAN_OBJ_DIR)/tests/%.o: tests/unit/%.cpp $(ASAN_FLAGS_STAMP) | $(LIBVER_GEN)
	@mkdir -p $(@D)
	$(CXX) $(ASAN_CXXFLAGS) $(INCLUDE) -Itests -c $< -MMD -MP -MF $(@:.o=.d) -o $@

define asan-test-executable-rule
ASAN_TEST_OBJ_$1 := $(ASAN_OBJ_DIR)/tests/$(basename $(notdir $1)).o

$(ASAN_APP_DIR)/$2$(EXE_EXTENSION): $$(ASAN_TEST_OBJ_$1) $(ASAN_TEST_SUPPORT_OBJ) $(ASAN_ARCHIVE)
	@mkdir -p $$(@D)
	$(CXX) $(ASAN_CXXFLAGS) -o $$@ $$(ASAN_TEST_OBJ_$1) $(ASAN_TEST_SUPPORT_OBJ) $(ASAN_LDFLAGS) $(ASAN_SELF_LIBRARY) $(DEP_LIBS) $(TESTFLAGS)
endef

$(foreach pair,$(TEST_PAIRS),\
	$(eval $(call asan-test-executable-rule,$(word 1,$(subst |, ,$(pair))),$(word 2,$(subst |, ,$(pair))))))

ASAN_TEST_EXECUTABLES := $(addprefix $(ASAN_APP_DIR)/,$(addsuffix $(EXE_EXTENSION),$(TEST_NAMES)))
ASAN_RUNTIME := $(shell $(CC) -print-file-name=libasan.so 2>/dev/null)
ASAN_REPLAY := $(ASAN_APP_DIR)/fuzz/replay_render$(EXE_EXTENSION)

$(ASAN_REPLAY): tests/fuzz/fuzz_render.c tests/fuzz/replay_main.c $(TEST_SUPPORT_SRC) $(ASAN_ARCHIVE) $(ASAN_FLAGS_STAMP)
	@mkdir -p $(@D)
	$(CC) $(ASAN_CFLAGS) $(INCLUDE) -o $@ tests/fuzz/fuzz_render.c tests/fuzz/replay_main.c $(TEST_SUPPORT_SRC) $(ASAN_LDFLAGS) $(ASAN_SELF_LIBRARY) $(DEP_LIBS)

test-asan: $(ASAN_TEST_EXECUTABLES) $(ASAN_REPLAY) ## Build with ASan+UBSan and run the tests, and the fuzz replay
	@for test_exe in $(ASAN_TEST_EXECUTABLES); do \
		printf '\n### $(SAN_LABEL) %s ###\n\n' "$$(basename $$test_exe)"; \
		LD_PRELOAD="$(ASAN_RUNTIME)$${LD_PRELOAD:+:$$LD_PRELOAD}" \
		LD_LIBRARY_PATH="$(ASAN_APP_DIR):$(LIB_INSTALL_PATH)/$(SUITE)$(if $(DEP_LIB_DIRS),:$(DEP_LIB_DIRS))" \
			$$test_exe --gtest_brief=1 || exit 1; \
	done
	@printf '\n### $(SAN_LABEL) fuzz replay ###\n\n'
	@LD_PRELOAD="$(ASAN_RUNTIME)$${LD_PRELOAD:+:$$LD_PRELOAD}" \
		LD_LIBRARY_PATH="$(ASAN_APP_DIR):$(LIB_INSTALL_PATH)/$(SUITE)$(if $(DEP_LIB_DIRS),:$(DEP_LIB_DIRS))" \
			$(ASAN_REPLAY) $(FUZZ_INPUTS)
	@printf '\n$(SAN_LABEL) suite clean.\n'

####################################################################
# ThreadSanitizer, in its own tree: the ASan rules above with the sanitizer swapped
####################################################################

# The sink's one shared structure is the stroke cache (SC7, one lock) and the executor's stripes (batched strokes); the tests that run
# threads are test_render_stroke and test_render_lean.  A sub-make over the ASan rules: the same sources, tests and replay, built into
# build/<BUILD>-tsan with -fsanitize=thread (the runtime is linked in, so none is preloaded).
TSAN_FLAGS := -fsanitize=thread -fno-omit-frame-pointer -g -O1

test-tsan: ## Build with ThreadSanitizer and run the tests and the fuzz replay
	@$(MAKE) --no-print-directory test-asan ASAN_BUILD_DIR=./build/$(BUILD)-tsan ASAN_UBSAN_FLAGS='$(TSAN_FLAGS)' ASAN_RUNTIME= SAN_LABEL=TSan

####################################################################
# Fuzzing: a list rendered through the sink (CAP-15, AD-19)
####################################################################

# libFuzzer needs clang; `make fuzz-replay` (in `make test`) needs only the compiler of the build. The set below names
# -ffp-contract=off, which check-fp-contract reads. The whole library is compiled into the fuzzer, instrumented.
FUZZ_CC ?= clang
FUZZ_TIME ?= 60
FUZZ_CFLAGS := -std=c17 -O1 -g -ffp-contract=off $(ARCH_FP_FLAGS) -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined -DGSHCPU_BUILD

fuzz: | $(LIBVER_GEN) ## Build the libFuzzer target and run it for FUZZ_TIME seconds from the committed corpus
	@if ! command -v $(FUZZ_CC) >/dev/null 2>&1; then \
		printf 'fuzz: needs %s (libFuzzer); `make fuzz-replay` runs the committed corpus without it\n' "$(FUZZ_CC)" >&2; exit 1; \
	fi
	@mkdir -p $(BUILD_DIR)/fuzz $(BUILD_DIR)/fuzz-corpus
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(INCLUDE) -o $(BUILD_DIR)/fuzz/fuzz_render tests/fuzz/fuzz_render.c $(TEST_SUPPORT_SRC) $(SOURCES) $(LDFLAGS) $(DEP_LIBS)
	LD_LIBRARY_PATH="$(TEST_LD_PATH)" $(BUILD_DIR)/fuzz/fuzz_render -max_total_time=$(FUZZ_TIME) $(BUILD_DIR)/fuzz-corpus tests/fuzz/corpus/render

fuzz-clean: ## Remove the fuzz build
	-@rm -rf $(BUILD_DIR)/fuzz

####################################################################
# Install
####################################################################

LDCONF_INSTALL_PATH ?= /etc/ld.so.conf.d
PC_REQUIRES := $(DEP_PCS)
PKGCONFIG_INSTALL_PATH ?= $(PC_INSTALL_PATH)

install: all ## Install the library
	@mkdir -p $(LIB_INSTALL_PATH)/$(SUITE)
ifeq ($(OS_NAME), Linux)
	@cp $(APP_DIR)/$(TARGET) $(LIB_INSTALL_PATH)/$(SUITE)/
	@ln -f -s $(TARGET) $(LIB_INSTALL_PATH)/$(SUITE)/$(SO_NAME)
	@ln -f -s $(SO_NAME) $(LIB_INSTALL_PATH)/$(SUITE)/$(BASE_NAME)
	@if [ -n "$(LDCONF_INSTALL_PATH)" ]; then mkdir -p $(LDCONF_INSTALL_PATH); fi
	@if [ -n "$(LDCONF_INSTALL_PATH)" ]; then echo "$(LIB_INSTALL_PATH)/$(SUITE)" > $(LDCONF_INSTALL_PATH)/$(SUITE)-$(PROJECT)$(BRANCH).conf; fi
endif
ifeq ($(OS_NAME), Windows)
	@mkdir -p $(BIN_INSTALL_PATH) $(LIB_INSTALL_PATH)/$(SUITE)
	@cp $(APP_DIR)/$(TARGET).a $(LIB_INSTALL_PATH)/$(SUITE)/
	@cp $(APP_DIR)/$(TARGET) $(BIN_INSTALL_PATH)/
endif
	@rm -rf $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)
	@mkdir -p $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)
	@if [ -d include/ghoti.io ]; then \
		cp -r include/ghoti.io $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)/ ; \
	fi
	@if [ -d $(GEN_DIR)/ghoti.io ]; then \
		cp -r $(GEN_DIR)/ghoti.io $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)/ ; \
	fi
	@mkdir -p $(PKGCONFIG_INSTALL_PATH)
	@cat pkgconfig/$(SUITE)-$(PROJECT).pc | sed 's/(SUITE)/$(SUITE)/g; s/(PROJECT)/$(PROJECT)/g; s/(BRANCH)/$(BRANCH)/g; s/(VERSION)/$(VERSION)/g; s|(PC_LIB_DIR)|$(PC_LIB_DIR)|g; s|(PC_INCLUDE_DIR)|$(PC_INCLUDE_DIR)|g; s|(REQUIRES)|$(PC_REQUIRES)|g' > $(PKGCONFIG_INSTALL_PATH)/$(SUITE)-$(PROJECT)$(BRANCH).pc
ifeq ($(OS_NAME), Linux)
	@if [ -n "$(LDCONF_INSTALL_PATH)" ]; then ldconfig >> /dev/null 2>&1; fi
endif
	@echo "Ghoti.io $(PROJECT)$(BRANCH) installed"

uninstall: ## Delete the installed files
ifeq ($(OS_NAME), Linux)
	@rm -f $(LIB_INSTALL_PATH)/$(SUITE)/$(BASE_NAME)*
	@rm -f $(LDCONF_INSTALL_PATH)/$(SUITE)-$(PROJECT)$(BRANCH).conf
endif
ifeq ($(OS_NAME), Windows)
	@rm -f $(LIB_INSTALL_PATH)/$(SUITE)/$(TARGET).a
	@rm -f $(BIN_INSTALL_PATH)/$(TARGET)
endif
	@rm -rf $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)
	@rm -f $(PKGCONFIG_INSTALL_PATH)/$(SUITE)-$(PROJECT)$(BRANCH).pc
ifeq ($(OS_NAME), Linux)
	@if [ -n "$(LDCONF_INSTALL_PATH)" ]; then ldconfig >> /dev/null 2>&1; fi
endif
	@echo "Ghoti.io $(PROJECT)$(BRANCH) has been uninstalled"

debug: ## Build in DEBUG mode
	make all BUILD=debug

install-debug: ## Install the DEBUG library
	make install BUILD=debug

uninstall-debug: ## Uninstall the DEBUG library
	make uninstall BUILD=debug

test-debug: ## Run the tests in DEBUG mode
	make test BUILD=debug

test-valgrind-debug: ## Valgrind in DEBUG mode
	make test-valgrind BUILD=debug

watch-debug: ## Watch and rebuild in DEBUG mode
	make watch BUILD=debug

test-watch-debug: ## Watch and test in DEBUG mode
	make test-watch BUILD=debug

docs: ## Generate documentation under docs/
	doxygen

docs-pdf: docs ## Generate the PDF manual
	cd ./docs/latex/ && make
	mv -f ./docs/latex/refman.pdf ./docs/$(SUITE)-$(PROJECT)$(BRANCH)-docs.pdf

cloc: ## Count the lines of code used in the project
	cloc src include tests Makefile

coverage: ## Build instrumented, run the tests, and report line coverage
	@rm -rf $(COV_BUILD_DIR)/objects/*.gcda \
		$(COV_BUILD_DIR)/objects/*/*.gcda 2> /dev/null || true
	@status=0; \
	$(MAKE) --no-print-directory test TEST_GATES= \
		BUILD_DIR=$(COV_BUILD_DIR) \
		EXTRA_CFLAGS="--coverage -O0 -fprofile-update=atomic" \
		EXTRA_LDFLAGS="--coverage" > /dev/null || status=$$?; \
	if [ $$status -eq 0 ]; then \
		tools/coverage.sh $(COV_BUILD_DIR)/objects || status=$$?; \
	else \
		printf 'coverage: the instrumented test run failed; no report\n' >&2; \
	fi; \
	exit $$status

clean: ## Remove the build directories
	-@rm -rf ./build

help: ## Display this help
	@grep -E '^[ a-zA-Z_-]+:.*?## .*$$' Makefile | sort | sed 's/\([^:]*\):.*## \(.*\)/\1:\2/' | awk -F: '{printf "%-22s %s\n", $$1, $$2}'

####################################################################
# Flag stamps. At the end so they are not the default goal, and so the
# directories they name have already been assigned.
####################################################################

.PHONY: force-flags

$(FLAGS_STAMP): force-flags
	@mkdir -p $(@D)
	@printf '%s\n' '$(CC) $(CXX) $(LIB_CFLAGS) $(CFLAGS) $(CXXFLAGS) $(LDFLAGS) $(INCLUDE) $(SELF_LIBRARY) $(DEP_LIBS) $(AS_NEEDED) $(TESTFLAGS) $(TEST_LDFLAGS) $(OS_SPECIFIC_LIBRARY_NAME_FLAG)' > $@.new
	@cmp -s $@.new $@ 2>/dev/null && rm -f $@.new || mv -f $@.new $@

$(ASAN_FLAGS_STAMP): force-flags
	@mkdir -p $(@D)
	@printf '%s\n' '$(CC) $(CXX) $(ASAN_CFLAGS) $(ASAN_CXXFLAGS) $(ASAN_LDFLAGS) $(INCLUDE) $(ASAN_SELF_LIBRARY) $(DEP_LIBS) $(TESTFLAGS)' > $@.new
	@cmp -s $@.new $@ 2>/dev/null && rm -f $@.new || mv -f $@.new $@
