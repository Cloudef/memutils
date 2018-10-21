#include "io.h"
#include <stdio.h>
#include <err.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

static size_t
mem_io_ptrace_do(const struct mem_io *io, void *ptr, const size_t offset, const size_t size, size_t (*iofun)(void*, size_t, size_t, FILE*))
{
   if (fseek(io->backing, offset, SEEK_SET) != 0) {
      warn("fseek(/proc/%u/mem, %zu)", io->pid, offset);
      return 0;
   }

   return iofun(ptr, 1, size, io->backing);
}

static size_t
mem_io_ptrace_write(const struct mem_io *io, const void *ptr, const size_t offset, const size_t size)
{
   clearerr(io->backing);
   const size_t ret = mem_io_ptrace_do(io, (void*)ptr, offset, size, (size_t(*)())fwrite);

   if (ferror(io->backing))
      warn("fwrite(/proc/%u/mem)", io->pid);

   return ret;
}

static size_t
mem_io_ptrace_read(const struct mem_io *io, void *ptr, const size_t offset, const size_t size)
{
   clearerr(io->backing);
   const size_t ret = mem_io_ptrace_do(io, ptr, offset, size, fread);

   if (ferror(io->backing))
      warn("fread(/proc/%u/mem)", io->pid);

   return ret;
}

static void
mem_io_ptrace_cleanup(struct mem_io *io)
{
   if (io->backing)
      fclose(io->backing);

   if (io->pid)
      ptrace(PTRACE_DETACH, io->pid, 1, 0);
}

bool
mem_io_ptrace_init(struct mem_io *io, const pid_t pid)
{
   *io = (struct mem_io){
      .pid = pid,
      .read = mem_io_ptrace_read,
      .write = mem_io_ptrace_write,
      .cleanup = mem_io_ptrace_cleanup
   };

   if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) == -1L) {
      warn("ptrace(PTRACE_ATTACH, %u, NULL, NULL)", pid);
      goto fail;
   }

   {
      int status;
      if (waitpid(pid, &status, 0) == -1 || !WIFSTOPPED(status)) {
         warn("waitpid(%u) == %d", pid, status);
         goto fail;
      }
   }

   char path[128];
   snprintf(path, sizeof(path), "/proc/%u/mem", pid);
   if (!(io->backing = fopen(path, "w+b"))) {
      warn("fopen(%s)", path);
      goto fail;
   }

   return true;

fail:
   io->cleanup(io);
   return false;
}
