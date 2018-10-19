#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <err.h>

// It's recommended to `setcap cap_sys_ptrace=eip proc-region-rw` to run this tool without sudo

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

   struct {
      FILE *mem;
      pid_t pid;
   } proc;
};

static void
usage(const char *argv0)
{
   fprintf(stderr, "usage: %s pid map file [offset] [len] < regions\n"
                   "       %s pid write file [offset] [len] < regions\n"
                   "       %s pid read [offset] [len] < regions\n"
                   "       regions must be in /proc/<pid>/maps format", argv0, argv0, argv0);
   exit(EXIT_FAILURE);
}

static void
context_init(struct context *ctx, size_t argc, const char *argv[])
{
   if (argc < 3)
      usage(argv[0]);

   size_t arg = 1;
   *ctx = (struct context){0};
   ctx->proc.pid = strtoull(argv[arg++], NULL, 10);

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

   char path[128];
   snprintf(path, sizeof(path), "/proc/%u/mem", ctx->proc.pid);
   if (!(ctx->proc.mem = fopen(path, "w+b")))
      err(EXIT_FAILURE, "fopen(%s)", path);
}

static void
context_release(struct context *ctx)
{
   if (ctx->op.wm.src)
      fclose(ctx->op.wm.src);

   fclose(ctx->proc.mem);
   free(ctx->buf);
   *ctx = (struct context){0};
}

static void
for_each_line_in_file(FILE *f, void (*cb)(const char *line, void *data), void *data)
{
   char *buffer = NULL;
   size_t step = 1024, allocated = 0, written = 0, read = 0;
   do {
      if (written + read >= allocated && !(buffer = realloc(buffer, (allocated += step) + 1)))
         err(EXIT_FAILURE, "realloc");
      buffer[(written += read)] = 0;
      size_t ate = 0;
      for (char *line = buffer, *nl; (nl = strchr(line, '\n')); line = nl + 1) {
         *nl = 0;
         cb(line, data);
         ate += nl + 1 - line;
      }
      memmove(buffer, buffer + ate, (written = written - ate));
   } while ((read = fread(buffer + written, 1, allocated - written, f)));
   free(buffer);
}

static void
region_cb(const char *line, void *data)
{
   struct context *ctx = data;
   unsigned long start, end, region_offset;
   if (sscanf(line, "%lx-%lx %*s %lx", &start, &end, &region_offset) < 3) {
      warnx("failed to parse mapping:\n%s", line);
      return;
   }

   warnx("%s", line);
   start += ctx->op.offset;

   if (start > end) {
      warnx("write offset %lx is out of bounds", start);
      return;
   }

   region_offset = (ctx->op.mode == MODE_MAP ? region_offset : 0);

   // requested write/read
   const size_t rlen = (ctx->op.has_len ? ctx->op.len : (ctx->op.mode == MODE_READ ? end - start : ctx->op.wm.size));

   // actual write/read
   const size_t len = (rlen > end - start ? end - start : rlen);

   if (!len)
      return;

   if (!(ctx->buf = realloc(ctx->buf, len)))
      err(EXIT_FAILURE, "realloc");

   clearerr(ctx->proc.mem);
   if (ctx->op.mode == MODE_MAP || ctx->op.mode == MODE_WRITE) {
      if (fseek(ctx->op.wm.src, region_offset, SEEK_SET) != 0)
         err(EXIT_FAILURE, "fseek");

      const size_t rd = fread(ctx->buf, 1, len, ctx->op.wm.src);

      if (fseek(ctx->proc.mem, start, SEEK_SET) != 0)
         err(EXIT_FAILURE, "fseek");

      const size_t wd = fwrite(ctx->buf, 1, rd, ctx->proc.mem);

      if (ferror(ctx->proc.mem)) {
         warn("fread(/proc/%u/mem)", ctx->proc.pid);
         return;
      }

      if (ctx->op.mode == MODE_WRITE) {
         if (rlen > wd) {
            warnx("wrote %lu bytes (%lu bytes truncated) to offset 0x%lx", wd, rlen - wd, start);
         } else {
            warnx("wrote %lu bytes to offset 0x%lx", wd, start);
         }
      } else {
         if (rlen > wd) {
            warnx("mapped %lu bytes (%lu bytes truncated) from offset 0x%lx to offset 0x%lx", wd, rlen - wd, region_offset, start);
         } else {
            warnx("mapped %lu bytes from offset 0x%lx to offset 0x%lx", wd, region_offset, start);
         }
      }
   } else {
      if (fseek(ctx->proc.mem, start, SEEK_SET) != 0)
         err(EXIT_FAILURE, "fseek");

      const size_t rd = fread(ctx->buf, 1, len, ctx->proc.mem);

      if (ferror(ctx->proc.mem))
         warn("fread(/proc/%u/mem)", ctx->proc.pid);

      if (fwrite(ctx->buf, 1, rd, stdout) != rd)
         err(EXIT_FAILURE, "fwrite");
   }
}

int
main(int argc, const char *argv[])
{
   struct context ctx;
   context_init(&ctx, argc, argv);

   if (ptrace(PTRACE_ATTACH, ctx.proc.pid, NULL, NULL) == -1L)
      err(EXIT_FAILURE, "ptrace(PTRACE_ATTACH, %u, NULL, NULL)", ctx.proc.pid);

   {
      int status;
      if (waitpid(ctx.proc.pid, &status, 0) == -1 || !WIFSTOPPED(status))
         err(EXIT_FAILURE, "waitpid");
   }

   for_each_line_in_file(stdin, region_cb, &ctx);
   ptrace(PTRACE_DETACH, ctx.proc.pid, 1, 0);
   context_release(&ctx);
   return EXIT_SUCCESS;
}
