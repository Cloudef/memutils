#pragma once

#include <stdbool.h>
#include <sys/types.h> // pid_t

struct io;

int
proc_region_rw(int argc, const char *argv[], bool (*io_init)(struct io*, const pid_t));
