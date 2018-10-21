#include "io.h"
#include <stdint.h>
#include <err.h>
#include <sys/uio.h>

static size_t
mem_io_uio_do(const struct mem_io *io, const void *ptr, const size_t offset, const size_t size, ssize_t (*iofun)(pid_t, const struct iovec*, unsigned long, const struct iovec*, unsigned long, unsigned long))
{
   const struct iovec lio = { .iov_base = (void*)ptr, .iov_len = size };
   const struct iovec rio = { .iov_base = (void*)(intptr_t)offset, .iov_len = size };
   return iofun(io->pid, &lio, 1, &rio, 1, 0);
}

static size_t
mem_io_uio_write(const struct mem_io *io, const void *ptr, const size_t offset, const size_t size)
{
   const size_t ret = mem_io_uio_do(io, ptr, offset, size, process_vm_writev);

   if (ret == (size_t)-1)
      warn("process_vm_writev(%u)", io->pid);

   return (ret == (size_t)-1 ? 0 : ret);
}

static size_t
mem_io_uio_read(const struct mem_io *io, void *ptr, const size_t offset, const size_t size)
{
   const size_t ret = mem_io_uio_do(io, ptr, offset, size, process_vm_readv);

   if (ret == (size_t)-1)
      warn("process_vm_readv(%u)", io->pid);

   return (ret == (size_t)-1 ? 0 : ret);
}

bool
mem_io_uio_init(struct mem_io *io, const pid_t pid)
{
   *io = (struct mem_io){ .pid = pid, .read = mem_io_uio_read, .write = mem_io_uio_write };
   return true;
}
