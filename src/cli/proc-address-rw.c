#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mem/io.h"
#include "mem/io-stream.h"
#include "util.h"

static void
usage(const char *argv0)
{
   fprintf(stderr, "usage: %s pid write offset [len] < data\n"
                   "       %s pid read offset len\n", argv0, argv0);
   exit(EXIT_FAILURE);
}

struct options {
   size_t offset, len;
   enum {
      MODE_WRITE,
      MODE_READ
   } mode;
   bool has_len;
};

static inline void
options_init(struct options *opt, size_t argc, const char *argv[])
{
   size_t arg = 0;
   *opt = (struct options){0};

   {
      bool w = false, r = false;
      const char *mode = argv[arg++];
      if (!(w = !strcmp(mode, "write")) && !(r = !strcmp(mode, "read")))
         errx(EXIT_FAILURE, "mode must be write or read");

      opt->mode = (w ? MODE_WRITE : MODE_READ);
   }

   opt->offset = hexdecstrtoull(argv[arg++], NULL);

   if (argc >= arg + 1) {
      opt->len = hexdecstrtoull(argv[arg++], NULL);
      opt->has_len = true;
   }

   if (opt->mode == MODE_READ && !opt->has_len)
      usage(argv[0]);
}

int
proc_address_rw(int argc, const char *argv[], bool (*mem_io_init)(struct mem_io*, const pid_t))
{
   if (argc < 4)
      usage(argv[0]);

   const pid_t pid = strtoull(argv[1], NULL, 10);

   struct options opt;
   options_init(&opt, argc - 2, argv + 2);

   struct mem_io io;
   if (!mem_io_init(&io, pid))
       return EXIT_FAILURE;

   size_t trw = 0;
   if (opt.mode == MODE_WRITE) {
      struct mem_io_istream stream = mem_io_istream_from_file(stdin);
      trw = mem_io_write_from_stream(&io, &stream, opt.offset, (opt.has_len ? opt.len : (size_t)~0));
   } else {
      struct mem_io_ostream stream = mem_io_ostream_from_file(stdout);
      trw = mem_io_read_to_stream(&io, &stream, opt.offset, opt.len);
   }

   mem_io_release(&io);
   return trw;
}
