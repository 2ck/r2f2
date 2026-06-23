CC ?= cc
AR ?= ar

SRCS = r2f2.c r2f2_alloc.c r2f2_file.c r2f2_metadata.c
OBJS = $(patsubst %.c,build/%.o,$(SRCS))

TESTS_SRCS = tests/basic.c
TESTS_OBJS = $(patsubst %.c,build/%.o,$(TESTS_SRCS))
TESTS_BINS = $(patsubst %.c,build/%,$(TESTS_SRCS))

CFLAGS += -g -O0 -std=c11 -I.
LDFLAGS += -L build -l r2f2

OUTLIB = build/libr2f2.a

.PHONY: all tests clean

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
