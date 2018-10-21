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
   region->end = (region->end > 0 ? region->end - 1 : 0);
   return true;
}

static inline size_t
for_each_token_in_str(const char *str, const char token, void (*cb)(const char *line, void *data), void *data)
{
   size_t ate = 0;
   for (char *line = (char*)str, *nl; (nl = strchr(line, token)); line = nl + 1) {
      *nl = 0;
      cb(line, data);
      *nl = token;
      ate += nl + 1 - line;
   }
   return ate;
}

static inline void
for_each_token_in_file(FILE *f, const char token, void (*cb)(const char *line, void *data), void *data)
{
   char *buffer = NULL;
   const size_t step = 1024;
   size_t allocated = 0, written = 0, read = 0;
   do {
      if (written + read >= allocated && !(buffer = realloc(buffer, (allocated += step) + 1)))
         err(EXIT_FAILURE, "realloc");

      buffer[(written += read)] = 0;
      const size_t ate = for_each_token_in_str(buffer, token, cb, data);
      memmove(buffer, buffer + ate, (written = written - ate));
   } while ((read = fread(buffer + written, 1, allocated - written, f)));

   if (written > 0)
      cb(buffer, data);

   free(buffer);
}
