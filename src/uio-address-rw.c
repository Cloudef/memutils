#include "cli/cli.h"
#include "mem/io.h"

// This address-rw uses uio
// It needs recent kernel, but may be racy as it reads / writes while process is running.
// Ideal for realtime memory tools.
// It's recommended to `setcap cap_sys_ptrace=eip uio-region-rw` to run this tool without sudo

int
main(int argc, const char *argv[])
{
   return proc_address_rw(argc, argv, mem_io_uio_init);
}
