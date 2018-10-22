#include "cli/cli.h"
#include "mem/io.h"

// This region-rw uses ptrace
// This works with older kernels, but it also ensures non racy read/writes, as it stops the process.

int
main(int argc, const char *argv[])
{
   return proc_region_rw(argc, argv, mem_io_ptrace_init);
}
