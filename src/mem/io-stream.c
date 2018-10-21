#include <stdio.h>
#include "io-stream.h"
#include "io.h"

static size_t
file_istream_read(const struct mem_io_istream *stream, void *ptr, const size_t size)
{
   return fread(ptr, 1, size, stream->backing);
}

struct mem_io_istream
mem_io_istream_from_file(FILE *file)
{
   return (struct mem_io_istream){
      .read = file_istream_read,
      .backing = file
   };
}

size_t
mem_io_write_from_stream(const struct mem_io *io, const struct mem_io_istream *stream, const size_t offset, const size_t size)
{
   size_t trw = 0;
   unsigned char buf[4096];
   for (size_t rd, trd = 0; (rd = stream->read(stream, buf, (trd + sizeof(buf) > size ? size - trd : sizeof(buf)))) && trd < size; trd += rd)
      trw += io->write(io, buf, offset + trd, rd);
   return trw;
}

static size_t
file_ostream_write(const struct mem_io_ostream *stream, const void *ptr, const size_t size)
{
   return fwrite(ptr, 1, size, stream->backing);
}

struct mem_io_ostream
mem_io_ostream_from_file(FILE *file)
{
   return (struct mem_io_ostream){
      .write = file_ostream_write,
      .backing = file
   };
}

size_t
mem_io_read_to_stream(const struct mem_io *io, const struct mem_io_ostream *stream, const size_t offset, const size_t size)
{
   size_t trw = 0;
   unsigned char buf[4096];
   for (size_t rd, trd = 0; (rd = io->read(io, buf, offset + trd, (trd + sizeof(buf) > size ? size - trd : sizeof(buf)))) && trd < size; trd += rd)
      trw += stream->write(stream, buf, rd);
   return trw;
}
