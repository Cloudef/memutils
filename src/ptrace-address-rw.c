#include "cli/cli.h"
#include "mem/io.h"

// This address-rw uses ptrace
// This works with older kernels, but it also ensures non racy read/writes, as it stops the process.
// It's recommended to `setcap cap_sys_ptrace=eip ptrace-region-rw` to run this tool without sudo

int
main(int argc, const char *argv[])
{
   return proc_address_rw(argc, argv, mem_io_ptrace_init);
}
