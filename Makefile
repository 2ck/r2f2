CC ?= cc
AR ?= ar

CFLAGS += -MMD -MP

BUILDDIR ?= build

SRCS = r2f2.c r2f2_alloc.c r2f2_defines.c r2f2_file.c r2f2_gc.c r2f2_metadata.c util/helpers.c
SRCS += ecc/bch/bch.c
OBJS = $(patsubst %.c,$(BUILDDIR)/%.o,$(SRCS))

TESTS_SRCS = tests/basic.c tests/ecc.c
TESTS_OBJS = $(patsubst %.c,$(BUILDDIR)/%.o,$(TESTS_SRCS))
TESTS_BINS = $(patsubst %.c,$(BUILDDIR)/%,$(TESTS_SRCS))

CFLAGS += -g -O3 -std=c11 -I.
CFLAGS += -I ecc/bch
CFLAGS += -Wall -Wextra -Wpedantic
# disable warnings for the logging macros:
CFLAGS += -Wno-gnu-zero-variadic-macro-arguments -Wno-gnu-auto-type
# disable warnings for ugly ecc macros (which are temporary with a FIXME)
CFLAGS += -Wno-gnu-statement-expression-from-macro-expansion

# in test/native environment only:
TEST_ENVIRONMENT ?= 0
ifeq ($(TEST_ENVIRONMENT),1)
	CFLAGS += -fno-omit-frame-pointer -fno-optimize-sibling-calls -fsanitize=address,undefined -fno-sanitize-recover=all
    LDFLAGS += -fsanitize=address,undefined
	CFLAGS += -DTEST_ENVIRONMENT
endif

LDFLAGS += -L $(BUILDDIR) -l r2f2

OUTLIB = $(BUILDDIR)/libr2f2.a

DEPS = $(OBJS:.o=.d)


all: $(OUTLIB)

$(OUTLIB): $(OBJS)
	mkdir -p $(dir $@)
	$(AR) rcs $@ $^

tests:
	$(MAKE) TEST_ENVIRONMENT=1 BUILDDIR=build/test tests-build

tests-build: $(TESTS_BINS)

$(BUILDDIR)/tests/%: $(BUILDDIR)/tests/%.o $(OUTLIB)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< $(OUTLIB) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILDDIR)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILDDIR)

-include $(DEPS)
.PHONY: all tests tests-build clean
