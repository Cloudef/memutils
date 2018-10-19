PREFIX ?= /usr/local
bindir ?= /bin

MAKEFLAGS += --no-builtin-rules

WARNINGS := -Wall -Wextra -Wpedantic -Wformat=2 -Wstrict-aliasing=3 -Wstrict-overflow=5 -Wstack-usage=12500 \
	-Wfloat-equal -Wcast-align -Wpointer-arith -Wchar-subscripts -Warray-bounds=2

override CFLAGS ?= -g
override CFLAGS += -std=c99 $(WARNINGS)
override CPPFLAGS += -Isrc

bins = ptrace-region-rw uio-region-rw binsearch bintrim
all: $(bins)

$(bins): %:
	$(LINK.c) $^ $(LDLIBS) -o $@

ptrace-region-rw: src/ptrace-region-rw.c src/cli/proc-region-rw.c src/io/io-ptrace.c
uio-region-rw: private CPPFLAGS += -D_GNU_SOURCE
uio-region-rw: src/uio-region-rw.c src/cli/proc-region-rw.c src/io/io-uio.c
binsearch: src/binsearch.c
bintrim: src/bintrim.c

install-bin: $(bins)
	install -Dm755 $^ -t "$(DESTDIR)$(PREFIX)$(bindir)"

install: install-bin

clean:
	$(RM) $(bins)

.PHONY: all clean install
