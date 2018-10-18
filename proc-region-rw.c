#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <err.h>

struct context {
   void *buf;

   struct {
      size_t offset, len;
      bool with_offset_and_len;

      struct {
         FILE *src;
      } write;

      enum {
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
   fprintf(stderr, "usage: %s pid write file [offset len] < regions\n"
                   "       %s pid read [offset len] < regions\n\n"
                   "       regions must be in /proc/<pid>/maps format", argv0, argv0);
   exit(EXIT_FAILURE);
}

static void
context_init(struct context *ctx, int argc, const char *argv[])
{
   if (argc < 3)
      usage(argv[0]);

   *ctx = (struct context){0};
   ctx->proc.pid = strtoull(argv[1], NULL, 10);

   {
      bool w, r;
      const char *mode = argv[2];
      if ((w = strcmp(mode, "write")) && (r = strcmp(mode, "read")))
         err(EXIT_FAILURE, "mode must be write or read");

      ctx->op.mode = (!w ? MODE_WRITE : MODE_READ);
   }

   if (ctx->op.mode == MODE_WRITE && argc < 4)
      usage(argv[0]);

   if (argc > (ctx->op.mode == MODE_WRITE) + 3) {
      if (argc < (ctx->op.mode == MODE_WRITE) + 5)
         usage(argv[0]);

      ctx->op.offset = strtoull(argv[(ctx->op.mode == MODE_WRITE) + 3], NULL, 10);
      ctx->op.len = strtoull(argv[(ctx->op.mode == MODE_WRITE) + 4], NULL, 10);
      ctx->op.with_offset_and_len = true;
   }

   if (ctx->op.mode == MODE_WRITE && !(ctx->op.write.src = fopen(argv[3], "rb")))
      err(EXIT_FAILURE, "fopen(%s)", argv[3]);

   char path[128];
   snprintf(path, sizeof(path), "/proc/%u/mem", ctx->proc.pid);
   if (!(ctx->proc.mem = fopen(path, "w+b")))
      err(EXIT_FAILURE, "fopen(%s)", path);
}

static void
context_release(struct context *ctx)
{
   if (ctx->op.write.src)
      fclose(ctx->op.write.src);

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
   unsigned long start, end, offset;
   if (sscanf(line, "%lx-%lx %*s %lx", &start, &end, &offset) < 3) {
      warnx("failed to parse mapping:\n%s", line);
      return;
   }

   warnx("%s", line);

   offset = (ctx->op.with_offset_and_len ? 0 : offset);
   const size_t len = (ctx->op.with_offset_and_len ? ctx->op.len : end - start);
   if (start + ctx->op.offset > end) {
      warnx("write offset %lx is out of bounds", start + ctx->op.offset);
      return;
   }

   if (len > (end - start - ctx->op.offset)) {
      warnx("%zu bytes doesn't fit in the region", len);
      return;
   }

   if (!len)
      return;

   if (!(ctx->buf = realloc(ctx->buf, len)))
      err(EXIT_FAILURE, "realloc");

   clearerr(ctx->proc.mem);
   if (ctx->op.mode == MODE_WRITE) {
      if (fseek(ctx->op.write.src, offset, SEEK_SET) != 0)
         err(EXIT_FAILURE, "fseek");

      const size_t rd = fread(ctx->buf, 1, len, ctx->op.write.src);

      if (fseek(ctx->proc.mem, start + ctx->op.offset, SEEK_SET) != 0)
         err(EXIT_FAILURE, "fseek");

      const size_t wd = fwrite(ctx->buf, 1, rd, ctx->proc.mem);

      if (ferror(ctx->proc.mem)) {
         warn("fread(/proc/%u/mem)", ctx->proc.pid);
      } else {
         warnx("wrote %lu bytes from offset 0x%lx to offset 0x%lx", wd, offset, start + ctx->op.offset);
      }
   } else {
      if (fseek(ctx->proc.mem, start + ctx->op.offset, SEEK_SET) != 0)
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
