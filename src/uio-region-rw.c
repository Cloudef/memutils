#include "cli/proc-region-rw.h"
#include "io/io.h"

// This region-rw uses uio
// It needs recent kernel, but may be racy as it reads / writes while process is running.
// Ideal for realtime memory tools.
// It's recommended to `setcap cap_sys_ptrace=eip uio-region-rw` to run this tool without sudo

int
main(int argc, const char *argv[])
{
   return proc_region_rw(argc, argv, io_uio_init);
}
