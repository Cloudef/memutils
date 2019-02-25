PREFIX ?= /usr/local
bindir ?= /bin

MAKEFLAGS += --no-builtin-rules

WARNINGS := -Wall -Wextra -Wpedantic -Wformat=2 -Wstrict-aliasing=3 -Wstrict-overflow=5 -Wstack-usage=12500 \
	-Wfloat-equal -Wcast-align -Wpointer-arith -Wchar-subscripts -Warray-bounds=2

override CFLAGS ?= -g -O2 $(WARNINGS)
override CFLAGS += -std=c99
override CPPFLAGS ?= -D_FORTIFY_SOURCE=2
override CPPFLAGS += -Isrc

bins = ptrace-region-rw ptrace-address-rw uio-region-rw uio-address-rw memview binsearch bintrim
all: $(bins)

%.a:
	$(LINK.c) -c $(filter %.c,$^) $(LDLIBS) -o $@

$(bins): %:
	$(LINK.c) $(filter %.c %.a,$^) $(LDLIBS) -o $@

memio-ptrace.a: src/mem/io-ptrace.c src/mem/io.h
memio-uio.a: private override CPPFLAGS += -D_GNU_SOURCE
memio-uio.a: src/mem/io-uio.c src/mem/io.h
memio-stream.a: src/mem/io-stream.c src/mem/io-stream.h

proc-address-rw.a: src/cli/proc-address-rw.c src/cli/cli.h src/util.h
proc-region-rw.a: src/cli/proc-region-rw.c src/cli/cli.h src/util.h
ptrace-address-rw: src/ptrace-address-rw.c proc-address-rw.a memio-ptrace.a memio-stream.a
ptrace-region-rw: src/ptrace-region-rw.c proc-region-rw.a memio-ptrace.a memio-stream.a
uio-address-rw: src/uio-address-rw.c proc-address-rw.a memio-uio.a memio-stream.a
uio-region-rw: src/uio-region-rw.c proc-region-rw.a memio-uio.a memio-stream.a

memview: src/memview.c src/util.h memio-uio.a
binsearch: src/binsearch.c src/util.h
bintrim: src/bintrim.c src/util.h

install-bin: $(bins)
	install -Dm755 $^ -t "$(DESTDIR)$(PREFIX)$(bindir)"

install: install-bin

clean:
	$(RM) $(bins) *.a

.PHONY: all clean install
