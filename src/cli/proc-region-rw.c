#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "io/io.h"
#include "util.h"

static void
usage(const char *argv0)
{
   fprintf(stderr, "usage: %s pid map file [offset] [len] < regions\n"
                   "       %s pid write file [offset] [len] < regions\n"
                   "       %s pid read [offset] [len] < regions\n"
                   "       regions must be in /proc/<pid>/maps format", argv0, argv0, argv0);
   exit(EXIT_FAILURE);
}

struct context {
   void *buf;

   struct {
      size_t offset, len;
      bool has_offset, has_len;

      struct {
         FILE *src;
         size_t size;
      } wm; // write/map

      enum {
         MODE_MAP,
         MODE_WRITE,
         MODE_READ
      } mode;
   } op;

   struct io io;
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
         errx(EXIT_FAILURE, "mode must be write or read");

      ctx->op.mode = (m ? MODE_MAP : (w ? MODE_WRITE : MODE_READ));
   }

   const char *wmname = NULL;
   if (ctx->op.mode == MODE_MAP || ctx->op.mode == MODE_WRITE) {
      if (argc < arg + 1)
         usage(argv[0]);

      wmname = argv[arg++];
   }

   if (argc >= arg + 1) {
      ctx->op.offset = strtoull(argv[arg++], NULL, 10);
      ctx->op.has_offset = true;
   }

   if (argc >= arg + 1) {
      ctx->op.len = strtoull(argv[arg++], NULL, 10);
      ctx->op.has_len = true;
   }

   if (wmname) {
      if (!(ctx->op.wm.src = fopen(wmname, "rb")))
         err(EXIT_FAILURE, "fopen(%s)", wmname);

      if (fseek(ctx->op.wm.src, 0, SEEK_END) != 0)
         err(EXIT_FAILURE, "fseek");

      ctx->op.wm.size = ftell(ctx->op.wm.src);
   }
}

static void
context_release(struct context *ctx)
{
   if (ctx->op.wm.src)
      fclose(ctx->op.wm.src);

   free(ctx->buf);
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

   if (region.start > region.end) {
      warnx("write offset 0x%zx is out of bounds", region.start);
      return;
   }

   region.offset = (ctx->op.mode == MODE_MAP ? region.offset : 0);

   // requested write/read
   const size_t rlen = (ctx->op.has_len ? ctx->op.len : (ctx->op.mode == MODE_READ ? region.end - region.start : ctx->op.wm.size));

   // actual write/read
   const size_t len = (rlen > region.end - region.start ? region.end - region.start : rlen);

   if (!len)
      return;

   if (!(ctx->buf = realloc(ctx->buf, len)))
      err(EXIT_FAILURE, "realloc");

   if (ctx->op.mode == MODE_MAP || ctx->op.mode == MODE_WRITE) {
      if (fseek(ctx->op.wm.src, region.offset, SEEK_SET) != 0)
         err(EXIT_FAILURE, "fseek");

      const size_t rd = fread(ctx->buf, 1, len, ctx->op.wm.src);
      const size_t wd = ctx->io.write(&ctx->io, ctx->buf, region.start, rd);

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
      const size_t rd = ctx->io.read(&ctx->io, ctx->buf, region.start, len);

      if (fwrite(ctx->buf, 1, rd, stdout) != rd)
         err(EXIT_FAILURE, "fwrite");
   }
}

int
proc_region_rw(int argc, const char *argv[], bool (*io_init)(struct io*, const pid_t))
{
   if (argc < 3)
      usage(argv[0]);

   const pid_t pid = strtoull(argv[1], NULL, 10);
   struct context ctx;
   context_init(&ctx, argc - 2, argv + 2);

   if (!io_init(&ctx.io, pid))
       return EXIT_FAILURE;

   for_each_line_in_file(stdin, region_cb, &ctx);
   io_release(&ctx.io);
   context_release(&ctx);
   return EXIT_SUCCESS;
}
