#pragma once

#include <stddef.h>

struct mem_io;

struct mem_io_istream {
   size_t (*read)(const struct mem_io_istream *stream, void *ptr, const size_t size);
   void *backing;
};

struct mem_io_istream
mem_io_istream_from_file(FILE *file);

size_t
mem_io_write_from_stream(const struct mem_io *io, const struct mem_io_istream *stream, const size_t offset, const size_t size);

struct mem_io_ostream {
   size_t (*write)(const struct mem_io_ostream *stream, const void *ptr, const size_t size);
   void *backing;
};

struct mem_io_ostream
mem_io_ostream_from_file(FILE *file);

size_t
mem_io_read_to_stream(const struct mem_io *io, const struct mem_io_ostream *stream, const size_t offset, const size_t size);
