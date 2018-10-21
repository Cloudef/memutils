#pragma once

#include <err.h>

static inline bool
is_hex(const char *str)
{
   return (strlen(str) > 2 && str[0] == '0' && str[1] == 'x');
}

static inline unsigned long long int
hexdecstrtoull(const char *str, char **endptr)
{
   return strtoull(str, endptr, (is_hex(str) ? 16 : 10));
}

struct region {
   size_t start, end, offset;
};

static inline bool
region_parse(struct region *region, const char *line)
{
   *region = (struct region){0};
   if (sscanf(line, "%zx-%zx %*s %zx", &region->start, &region->end, &region->offset) < 3 || region->start > region->end) {
      warnx("failed to parse mapping:\n%s", line);
      return false;
   }
   return true;
}

static inline void
for_each_line_in_file(FILE *f, void (*cb)(const char *line, void *data), void *data)
{
   char *buffer = NULL;
   size_t step = 1024, allocated = 0, written = 0, read = 0;
   do {
      if (written + read >= allocated && !(buffer = realloc(buffer, (allocated += step) + 1)))
         err(EXIT_FAILURE, "realloc");

      buffer[(written += read)] = 0;

      size_t ate = 0;
      for (char *line = buffer, *nl; (nl = strchr(line, '\n')); line = nl + 1) {
         *nl = 0;
         cb(line, data);
         ate += nl + 1 - line;
      }

      memmove(buffer, buffer + ate, (written = written - ate));
   } while ((read = fread(buffer + written, 1, allocated - written, f)));
   free(buffer);
}
