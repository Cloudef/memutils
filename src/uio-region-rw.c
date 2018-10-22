#include "cli/cli.h"
#include "mem/io.h"

// This region-rw uses uio
// It needs recent kernel, but may be racy as it reads / writes while process is running.
// Ideal for realtime memory tools, however uio can't write non writable memory(!).

int
main(int argc, const char *argv[])
{
   return proc_region_rw(argc, argv, mem_io_uio_init);
}
