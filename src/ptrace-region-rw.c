#include "cli/proc-region-rw.h"
#include "io/io.h"

// This region-rw uses ptrace
// This works with older kernels, but it also ensures non racy read/writes, as it stops the process.
// It's recommended to `setcap cap_sys_ptrace=eip ptrace-region-rw` to run this tool without sudo

int
main(int argc, const char *argv[])
{
   return proc_region_rw(argc, argv, io_ptrace_init);
}
