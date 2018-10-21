#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h> // pid_t

struct mem_io {
   size_t (*read)(const struct mem_io *io, void *ptr, const size_t offset, const size_t size);
   size_t (*write)(const struct mem_io *io, const void *ptr, const size_t offset, const size_t size);
   void (*cleanup)(struct mem_io *io);
   void *backing;
   pid_t pid;
};

static inline void
mem_io_release(struct mem_io *io)
{
   if (io->cleanup)
      io->cleanup(io);
   *io = (struct mem_io){0};
}

void
mem_io_release(struct mem_io *io);

bool
mem_io_uio_init(struct mem_io *io, const pid_t pid);

bool
mem_io_ptrace_init(struct mem_io *io, const pid_t pid);
