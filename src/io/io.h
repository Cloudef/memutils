#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h> // pid_t

struct io {
   void *backing;
   size_t (*read)(const struct io *io, void *ptr, const size_t offset, const size_t size);
   size_t (*write)(const struct io *io, const void *ptr, const size_t offset, const size_t size);
   void (*cleanup)(struct io *io);
   pid_t pid;
};

static inline void
io_release(struct io *io)
{
   if (io->cleanup)
      io->cleanup(io);
   *io = (struct io){0};
}

bool
io_uio_init(struct io *io, const pid_t pid);

bool
io_ptrace_init(struct io *io, const pid_t pid);
