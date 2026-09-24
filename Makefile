CC ?= cc
AR ?= ar

CFLAGS += -MMD -MP

SRCS = r2f2.c r2f2_alloc.c r2f2_defines.c r2f2_file.c r2f2_gc.c r2f2_metadata.c util/helpers.c
SRCS += ecc/bch/bch.c
OBJS = $(patsubst %.c,build/%.o,$(SRCS))

TESTS_SRCS = tests/basic.c tests/ecc.c
TESTS_OBJS = $(patsubst %.c,build/%.o,$(TESTS_SRCS))
TESTS_BINS = $(patsubst %.c,build/%,$(TESTS_SRCS))

CFLAGS += -g -O0 -std=c11 -I.
CFLAGS += -I ecc/bch
CFLAGS += -Wall -Wextra -Wpedantic
# disable warnings for the logging macros:
CFLAGS += -Wno-gnu-zero-variadic-macro-arguments -Wno-gnu-auto-type
# disable warnings for ugly ecc macros (which are temporary with a FIXME)
CFLAGS += -Wno-gnu-statement-expression-from-macro-expansion
#CFLAGS += -fno-omit-frame-pointer -fno-optimize-sibling-calls -fsanitize=address,undefined -fno-sanitize-recover=all
LDFLAGS += -L build -l r2f2

OUTLIB = build/libr2f2.a

DEPS = $(OBJS:.o=.d)


all: $(OUTLIB)

$(OUTLIB): $(OBJS)
	mkdir -p $(dir $@)
	$(AR) rcs $@ $^

tests: $(TESTS_BINS)

build/tests/%: build/tests/%.o $(OUTLIB)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< $(OUTLIB) $(LDFLAGS) $(LDLIBS) -o $@

build/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build

-include $(DEPS)
.PHONY: all tests clean
