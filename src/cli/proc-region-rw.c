#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mem/io.h"
#include "mem/io-stream.h"
#include "util.h"

static void
usage(const char *argv0)
{
   fprintf(stderr, "usage: %s pid map regions data [offset] [len]\n"
                   "       %s pid write regions data [offset] [len]\n"
                   "       %s pid read regions [offset] [len]\n"
                   "       regions must be in /proc/<pid>/maps format", argv0, argv0, argv0);
   exit(EXIT_FAILURE);
}

struct context {
   struct {
      size_t offset, len;
      bool has_offset, has_len;

      enum {
         MODE_MAP,
         MODE_WRITE,
         MODE_READ
      } mode;
   } op;

   struct mem_io io;
   FILE *regions, *data;
   size_t data_len, trw;
};

static inline void
context_init(struct context *ctx, size_t argc, const char *argv[])
{
   size_t arg = 0;
   *ctx = (struct context){0};

   {
      bool m = false, w = false, r = false;
      const char *mode = argv[arg++];
      if (!(m = !strcmp(mode, "map")) && !(w = !strcmp(mode, "write")) && !(r = !strcmp(mode, "read")))
         errx(EXIT_FAILURE, "mode must be map, write or read");

      ctx->op.mode = (m ? MODE_MAP : (w ? MODE_WRITE : MODE_READ));
   }

   const char *regions_fname = argv[arg++], *data_fname = NULL;

   if (ctx->op.mode != MODE_READ && argc >= arg + 1)
      data_fname = argv[arg++];

   if (argc >= arg + 1) {
      ctx->op.offset = hexdecstrtoull(argv[arg++], NULL);
      ctx->op.has_offset = true;
   }

   if (argc >= arg + 1) {
      ctx->op.len = hexdecstrtoull(argv[arg++], NULL);
      ctx->op.has_len = true;
   }

   if (!(ctx->regions = fopen(regions_fname, "rb")))
      err(EXIT_FAILURE, "fopen(%s)", regions_fname);

   if (data_fname) {
      if (!(ctx->data = fopen(data_fname, "rb")))
         err(EXIT_FAILURE, "fopen(%s)", data_fname);

      if (fseek(ctx->data, 0, SEEK_END) != 0)
         err(EXIT_FAILURE, "fseek");

      ctx->data_len = ftell(ctx->data);
   }
}

static void
context_release(struct context *ctx)
{
   if (ctx->regions)
      fclose(ctx->regions);
   if (ctx->data)
      fclose(ctx->data);
   *ctx = (struct context){0};
}

static void
region_cb(const char *line, void *data)
{
   struct context *ctx = data;

   struct region region;
   if (!region_parse(&region, line))
       return;

   warnx("%s", line);
   region.start += ctx->op.offset;
   region.offset *= (ctx->op.mode == MODE_MAP);

   if (region.start > region.end) {
      warnx("write offset 0x%zx is out of bounds", region.start);
      return;
   }

   // requested write/read
   const size_t rlen = (ctx->op.has_len ? ctx->op.len : (ctx->op.mode == MODE_READ ? region.end - region.start : ctx->data_len));
   // actual write/read
   const size_t len = (rlen > region.end - region.start ? region.end - region.start : rlen);

   if (!len)
      return;

   if (ctx->op.mode == MODE_MAP || ctx->op.mode == MODE_WRITE) {
      if (fseek(ctx->data, region.offset, SEEK_SET) != 0)
         err(EXIT_FAILURE, "fseek");

      struct mem_io_istream stream = mem_io_istream_from_file(ctx->data);
      const size_t wd = mem_io_write_from_stream(&ctx->io, &stream, region.start, len);
      ctx->trw += wd;

      if (ctx->op.mode == MODE_WRITE) {
         if (rlen > wd) {
            warnx("wrote %zu bytes (%zu bytes truncated) to offset 0x%zx", wd, rlen - wd, region.start);
         } else {
            warnx("wrote %zu bytes to offset 0x%zx", wd, region.start);
         }
      } else {
         if (rlen > wd) {
            warnx("mapped %zu bytes (%zu bytes truncated) from offset 0x%zx to offset 0x%zx", wd, rlen - wd, region.offset, region.start);
         } else {
            warnx("mapped %zu bytes from offset 0x%zx to offset 0x%zx", wd, region.offset, region.start);
         }
      }
   } else {
      struct mem_io_ostream stream = mem_io_ostream_from_file(stdout);
      ctx->trw += mem_io_read_to_stream(&ctx->io, &stream, region.start, len);
   }
}

int
proc_region_rw(int argc, const char *argv[], bool (*mem_io_init)(struct mem_io*, const pid_t))
{
   if (argc < 4)
      usage(argv[0]);

   const pid_t pid = strtoull(argv[1], NULL, 10);

   struct context ctx;
   context_init(&ctx, argc - 2, argv + 2);

   if (!mem_io_init(&ctx.io, pid))
       return EXIT_FAILURE;

   for_each_line_in_file(ctx.regions, region_cb, &ctx);
   const size_t trw = ctx.trw;

   mem_io_release(&ctx.io);
   context_release(&ctx);
   return trw;
}
