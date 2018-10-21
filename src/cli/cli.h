#pragma once

#include <stdbool.h>
#include <sys/types.h> // pid_t

struct mem_io;

int
proc_address_rw(int argc, const char *argv[], bool (*)(struct mem_io*, const pid_t));

int
proc_region_rw(int argc, const char *argv[], bool (*)(struct mem_io*, const pid_t));
