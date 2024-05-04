MAKEFILE_PATH   := $(abspath $(lastword $(MAKEFILE_LIST)))
NIX_FILES       := $(wildcard *.nix)

HAVE_JQ         := $(shell command -v jq)
HAVE_SED        := $(shell command -v sed)
CC_IS_CLANG     := $(shell $(CC) --version | grep -q "clang" && echo "yes" || echo "no")
BUILD_HOSTNAME  := $(shell uname -n)

INSTALL_BINDIR  ?= /usr/local/bin
BUILD_DIR       ?= build

BIN_DIR         := $(BUILD_DIR)/bin
DEP_DIR         := $(BUILD_DIR)/dep
ENV_DIR         := $(BUILD_DIR)/env
JSON_DIR        := $(BUILD_DIR)/json
OBJ_DIR         := $(BUILD_DIR)/obj
COV_DIR         := $(BUILD_DIR)/cov

APP             := $(BIN_DIR)/ts

SRCS            := ts.c
OBJS            := $(patsubst %.c,$(OBJ_DIR)/%.o,$(SRCS))
DEPS            := $(patsubst %.c,$(DEP_DIR)/%.d,$(SRCS))
JSON_FILES      := $(patsubst %.c,$(JSON_DIR)/%.json,$(SRCS))

ENV_DEPS        := DEBUG USE_ASAN EXTRA_CFLAGS EXTRA_LDFLAGS EXTRA_LIBS BUILD_HOSTNAME
ENV_FILE_DEPS   := $(foreach var,$(ENV_DEPS),$(ENV_DIR)/$(var))
BUILD_CONFIGS   := $(ENV_FILE_DEPS) $(MAKEFILE_PATH) $(NIX_FILES) Makefile.clang

# Default values for PCRE2 include directory and library.
PCRE2_CFLAGS    ?= $(shell pkg-config --cflags libpcre2-8 || true)
PCRE2_LIBS      ?= $(shell pkg-config --libs libpcre2-8 || true)

CFLAGS          ?= -Wall -Wformat -Wextra -Werror -Wshadow -Wunused

ifeq ($(CC_IS_CLANG),yes)
COMPILE.c       += -MJ$(JSON_DIR)/$*.json
endif

ifeq ($(USE_ASAN),1)
CFLAGS          += -fsanitize=address,undefined,bounds \
		   -fsanitize-address-use-after-scope
endif

ifeq ($(DEBUG),1)
CFLAGS          += -g -ggdb3 -O0 -fno-inline -fno-omit-frame-pointer -U_FORTIFY_SOURCE
else
CFLAGS          += -O3 -finline-functions -march=native -funroll-loops -fno-omit-frame-pointer
endif

CFLAGS          += $(PCRE2_CFLAGS) $(EXTRA_CFLAGS)
LDFLAGS         += $(EXTRA_LDFLAGS)

$(APP): $(OBJS) $(BUILD_CONFIGS) | $(BIN_DIR)
	$(LINK.c) $(OBJS) -o $@ $(LDFLAGS) $(PCRE2_LIBS) $(EXTRA_LIBS)

$(OBJ_DIR)/%.o: %.c $(BUILD_CONFIGS) | $(OBJ_DIR) $(DEP_DIR) $(JSON_DIR)
	$(CC) $(CC_IMPLICIT_INCLUDE_DIRS) $(CFLAGS) $(if $(findstring yes,$(CC_IS_CLANG)),-MJ$(JSON_DIR)/$*.json,) -MD -MP -MF$(DEP_DIR)/$*.d -c $< -o $@

.PHONY: FORCE

define DEPENDABLE_VAR
$(ENV_DIR)/$(1): | $(ENV_DIR)
	@echo -n $($(1)) > $(ENV_DIR)/$(1)
ifneq ("$(file <$(ENV_DIR)/$(1))","$($(1))")
$(ENV_DIR)/$(1): FORCE
endif

endef

$(foreach var,$(ENV_DEPS),$(eval $(call DEPENDABLE_VAR,$(var))))

$(BIN_DIR) $(BUILD_DIR) $(DEP_DIR) $(ENV_DIR) $(INSTALL_BINDIR) $(JSON_DIR) $(OBJ_DIR):
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

.PHONY: verify
verify:
	@echo "APP=$(APP)"
	@echo "BUILD_CONFIGS=$(BUILD_CONFIGS)"
	@echo "BUILD_HOSTNAME=$(BUILD_HOSTNAME)"
	@echo "CC_IMPLICIT_INCLUDES=$(CC_IMPLICIT_INCLUDES)"
	@echo "CC_IMPLICIT_INCLUDE_PATHS=$(CC_IMPLICIT_INCLUDE_PATHS)"
	@echo "CC_IS_CLANG=$(CC_IS_CLANG)"
	@echo "CFLAGS=$(CFLAGS)"
	@echo "DEBUG=$(DEBUG)"
	@echo "DEPS=$(DEPS)"
	@echo "DEP_DIR=$(DEP_DIR)"
	@echo "ENV_DEPS=$(ENV_DEPS)"
	@echo "ENV_FILE_DEPS=$(ENV_FILE_DEPS)"
	@echo "EXTRA_CFLAGS=$(EXTRA_CFLAGS)"
	@echo "EXTRA_LIBS=$(EXTRA_LIBS)"
	@echo "HAVE_JQ=$(HAVE_JQ)"
	@echo "HAVE_SED=$(HAVE_SED)"
	@echo "JSON_FILES=$(JSON_FILES)"
	@echo "MAKEFILE_LIST=$(MAKEFILE_LIST)"
	@echo "MAKEFILE_PATH=$(MAKEFILE_PATH)"
	@echo "NIX_FILES=$(NIX_FILES)"
	@echo "OBJS=$(OBJS)"
	@echo "OBJ_DIR=$(OBJ_DIR)"
	@echo "PCRE2_CFLAGS=$(PCRE2_CFLAGS)"
	@echo "PCRE2_LIBS=$(PCRE2_LIBS)"
	@echo "SRCS=$(SRCS)"
	@echo "USE_ASAN=$(USE_ASAN)"

.PHONY: pgo
pgo: pgo-generate pgo-run pgo-use
	hyperfine --warmup 5 --min-runs 1 --export-markdown pgo-results.md 'cat z | ./build/bin/ts "%F %.T"' 'cat z | ts "%F %H:%M:%.S"'
	hyperfine --warmup 5 --min-runs 1 --export-markdown pgo-results.md 'cat z | ./build/bin/ts -r' 'cat z | ts -r'

.PHONY: pgo-generate
pgo-generate: clean
	$(MAKE) DEBUG=0 USE_ASAN=0 EXTRA_CFLAGS="$(EXTRA_CFLAGS) -fprofile-generate -flto" clean $(APP)

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
	$(MAKE) DEBUG=0 USE_ASAN=0 EXTRA_CFLAGS="$(EXTRA_CFLAGS) -fprofile-use -fprofile-correction -flto" clean $(APP)

.PHONY: nix-build
nix-build:
	nix build --print-build-logs .

include Makefile.clang

-include $(DEP)
