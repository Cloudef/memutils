PREFIX ?= /usr/local
bindir ?= /bin

MAKEFLAGS += --no-builtin-rules

WARNINGS := -Wall -Wextra -Wpedantic -Wformat=2 -Wstrict-aliasing=3 -Wstrict-overflow=5 -Wstack-usage=12500 \
	-Wfloat-equal -Wcast-align -Wpointer-arith -Wchar-subscripts -Warray-bounds=2

override CFLAGS ?= -g
override CFLAGS += -std=c99 $(WARNINGS)
override CPPFLAGS += -Isrc

bins = ptrace-region-rw ptrace-address-rw uio-region-rw uio-address-rw binsearch bintrim
all: $(bins)

$(bins): %:
	$(LINK.c) $^ $(LDLIBS) -o $@

ptrace-address-rw: src/ptrace-address-rw.c src/cli/proc-address-rw.c src/mem/io-ptrace.c src/mem/io-stream.c
ptrace-region-rw: src/ptrace-region-rw.c src/cli/proc-region-rw.c src/mem/io-ptrace.c src/mem/io-stream.c
uio-region-rw: private CPPFLAGS += -D_GNU_SOURCE
uio-region-rw: src/uio-region-rw.c src/cli/proc-region-rw.c src/mem/io-uio.c src/mem/io-stream.c
uio-address-rw: private CPPFLAGS += -D_GNU_SOURCE
uio-address-rw: src/uio-address-rw.c src/cli/proc-address-rw.c src/mem/io-uio.c src/mem/io-stream.c
binsearch: src/binsearch.c
bintrim: src/bintrim.c

install-bin: $(bins)
	install -Dm755 $^ -t "$(DESTDIR)$(PREFIX)$(bindir)"

install: install-bin

clean:
	$(RM) $(bins)

.PHONY: all clean install
