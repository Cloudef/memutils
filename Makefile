PREFIX ?= /usr/local
bindir ?= /bin

MAKEFLAGS += --no-builtin-rules

WARNINGS := -Wall -Wextra -Wpedantic -Wformat=2 -Wstrict-aliasing=3 -Wstrict-overflow=5 -Wstack-usage=12500 \
	-Wfloat-equal -Wcast-align -Wpointer-arith -Wchar-subscripts -Warray-bounds=2

override CFLAGS ?= -g
override CFLAGS += -std=c99 -D_DEFAULT_SOURCE $(WARNINGS)

bins = proc-region-rw binsearch bintrim
all: $(bins)

$(bins): %:
	$(LINK.c) $^ $(LDLIBS) -o $@

proc-region-rw: proc-region-rw.c
binsearch: binsearch.c
bintrim: bintrim.c

install-bin: $(bins)
	install -Dm755 $^ -t "$(DESTDIR)$(PREFIX)$(bindir)"

install: install-bin

clean:
	$(RM) $(bins)

.PHONY: all clean install
