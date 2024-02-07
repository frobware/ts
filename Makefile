# If this Makefile changes, then everthing depending on MAKEFILE_PATH
# should be rebuilt.
MAKEFILE_PATH   := $(abspath $(lastword $(MAKEFILE_LIST)))
NIX_FILES       := $(wildcard *.nix)

HAVE_JQ         := $(shell command -v jq)
HAVE_SED        := $(shell command -v sed)
IS_CLANG        := $(shell $(CC) --version | grep -q "clang" && echo "yes" || echo "no")

INSTALL_BINDIR  ?= /usr/local/bin
BUILD_DIR       ?= build

BIN_DIR         := $(BUILD_DIR)/bin
DEP_DIR         := $(BUILD_DIR)/dep
JSON_DIR        := $(BUILD_DIR)/json
OBJ_DIR         := $(BUILD_DIR)/obj
COV_DIR         := $(BUILD_DIR)/cov

APP             := $(BIN_DIR)/ts

SRCS            := ts.c
OBJS            := $(patsubst %.c,$(OBJ_DIR)/%.o,$(SRCS))
DEPS            := $(patsubst %.c,$(DEP_DIR)/%.d,$(SRCS))
JSON_FILES      := $(patsubst %.c,$(JSON_DIR)/%.json,$(SRCS))

CFLAGS          ?= -Wall -Wformat -Wextra -Werror -Wshadow -Wunused
LIBS            += -lpcre

ifeq ($(IS_CLANG),yes)
COMPILE.c       += -MJ$(JSON_DIR)/$*.json
endif

ifeq ($(TS_BUILD_WITH_ASAN),1)
CFLAGS          += -fsanitize=address,undefined,bounds \
		   -fsanitize-address-use-after-scope
endif

ifeq ($(TS_BUILD_WITH_DEBUG),1)
CFLAGS          += -g -ggdb3 -O0 -fno-inline -fno-omit-frame-pointer -U_FORTIFY_SOURCE
else
CFLAGS          += -O3 -finline-functions -march=native -funroll-loops -fno-omit-frame-pointer
endif

CFLAGS          += $(EXTRA_CFLAGS)

BUILD_FILES     := $(MAKEFILE_PATH) $(NIX_FILES) $(BUILD_DIR)/build.env

$(APP): $(OBJS) $(BUILD_FILES) | $(BIN_DIR)
	$(LINK.c) $(OBJS) -o $@ $(LIBS) $(EXTRA_LIBS)

$(OBJ_DIR)/%.o: %.c $(BUILD_FILES) | $(OBJ_DIR) $(DEP_DIR) $(JSON_DIR)
ifeq ($(IS_CLANG),yes)
	$(CC) $(CC_IMPLICIT_INCLUDE_DIRS) $(CFLAGS) -MJ$(JSON_DIR)/$*.json -MMD -MP -MF$(DEP_DIR)/$*.d -c $< -o $@
else
	$(CC) $(CC_IMPLICIT_INCLUDE_DIRS) $(CFLAGS) -MMD -MP -MF$(DEP_DIR)/$*.d -c $< -o $@
endif

$(BUILD_DIR)/build.env: FORCE | $(BUILD_DIR)
	@printenv | grep '^HOST' > $@.tmp || true
	@printenv | grep '^NIX' >> $@.tmp || true
	@printenv | grep '^BUILD_WITH' >> $@.tmp || true
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp

FORCE:

$(BIN_DIR) $(OBJ_DIR) $(DEP_DIR) $(JSON_DIR) $(INSTALL_BINDIR) $(BUILD_DIR):
	@mkdir -p $@

.PHONY: install
install: $(APP) | $(INSTALL_BINDIR)
	@install -m 755 $(APP) $(INSTALL_BINDIR)

.PHONY: clean
clean:
	$(RM) -r $(OBJS) $(DEPS) $(JSON_FILES) $(APP)

.PHONY: rclean
rclean:
	$(RM) -r $(BUILD_DIR)

.PHONY: include-what-you-use
include-what-you-use:
	$(RM) $(OBJS)
	$(MAKE) CC="include-what-you-use -Xiwyu --comment_style=none" $(OBJS)

.PHONY: cppcheck
cppcheck:
	cppcheck $(SRCS)

.PHONY: covcheck
covcheck:
	$(MAKE) clean
	cov-build --dir=$(COV_DIR) $(MAKE)
	cov-analyze --dir=$(COV_DIR) --wait-for-license
	cov-format-errors --dir=$(COV_DIR) --emacs-style

# The following variables are to help generate a compile_commands.json
# on macOS where bear(1) does not work.
#
# Detect default C include paths. This command runs the compiler with
# flags that output include paths. The first sed command extracts the
# paths listed between specific markers in the compiler output. The
# second sed command removes any lines containing '(framework
# directory)' as seen on macOS with /usr/bin/clang.
CC_IMPLICIT_INCLUDE_PATHS := $(shell $(CC) -v -E -x c /dev/null 2>&1 | sed -n '/#include <...> search starts here:/,/End of search list./{//!p}' | sed '/(framework directory)/d')

CC_IMPLICIT_INCLUDES := $(patsubst %,-I%,$(CC_IMPLICIT_INCLUDE_PATHS))

# make CC=clang clean compile_commands.json
.PHONY: compile_commands.json
compile_commands.json:
ifeq ($(IS_CLANG),yes)
	$(MAKE)	EXTRA_CFLAGS="$(EXTRA_CFLAGS) $(CC_IMPLICIT_INCLUDES)" clean $(APP)
	@echo '[' > $@
	@$(foreach file,$(JSON_FILES),cat $(file) >> $@;)
	@@sed -i '$$s/,$$/]/' $@
ifneq ($(HAVE_JQ),)
	@jq '.' $@ > $@.tmp && mv $@.tmp $@
endif
else
	@echo "IS_CLANG=$(IS_CLANG); need clang to generate $@"
endif

.PHONY: verify
verify:
	@echo "APP=$(APP)"
	@echo "BUILD_FILES=$(BUILD_FILES)"
	@echo "CC_IMPLICIT_INCLUDES=$(CC_IMPLICIT_INCLUDES)"
	@echo "CC_IMPLICIT_INCLUDE_PATHS=$(CC_IMPLICIT_INCLUDE_PATHS)"
	@echo "CFLAGS=$(CFLAGS)"
	@echo "DEPS=$(DEPS)"
	@echo "DEP_DIR=$(DEP_DIR)"
	@echo "EXTRA_CFLAGS=$(EXTRA_CFLAGS)"
	@echo "EXTRA_LIBS=$(EXTRA_LIBS)"
	@echo "HAVE_JQ=$(HAVE_JQ)"
	@echo "HAVE_SED=$(HAVE_SED)"
	@echo "IS_CLANG=$(IS_CLANG)"
	@echo "JSON_FILES=$(JSON_FILES)"
	@echo "LIBS=$(LIBS)"
	@echo "MAKEFILE_LIST=$(MAKEFILE_LIST)"
	@echo "MAKEFILE_PATH=$(MAKEFILE_PATH)"
	@echo "NIX_FILES=$(NIX_FILES)"
	@echo "OBJS=$(OBJS)"
	@echo "OBJ_DIR=$(OBJ_DIR)"
	@echo "SRCS=$(SRCS)"

.PHONY: pgo
pgo: pgo-generate pgo-run pgo-use
	hyperfine --warmup 5 --min-runs 1 --export-markdown pgo-results.md 'cat z | ./build/bin/ts "%F %.T"' 'cat z | ts "%F %H:%M:%.S"'
	hyperfine --warmup 5 --min-runs 1 --export-markdown pgo-results.md 'cat z | ./build/bin/ts -r' 'cat z | ts -r'

.PHONY: pgo-generate
pgo-generate: clean
	$(MAKE) TS_BUILD_WITH_DEBUG=0 TS_BUILD_WITH_ASAN=0 EXTRA_CFLAGS="$(EXTRA_CFLAGS) -fprofile-generate -flto" clean $(APP)

.PHONY: pgo-run
pgo-run:
	@echo "Running $(APP) to generate profile data..."
	$(APP) < test-data > /dev/null
	$(APP) -r < test-data > /dev/null
	$(APP) '%F %T' < test-data > /dev/null
	$(APP) '%F %H:%M:%.S' < test-data > /dev/null
	@echo "Profile data generated."

.PHONY: pgo-use
pgo-use: clean
	$(MAKE) TS_BUILD_WITH_DEBUG=0 TS_BUILD_WITH_ASAN=0 EXTRA_CFLAGS="$(EXTRA_CFLAGS) -fprofile-use -fprofile-correction -flto" clean $(APP)

.PHONY: nix-build
nix-build:
	nix build --print-build-logs .

-include $(DEP)
