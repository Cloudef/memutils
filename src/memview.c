#include <unistd.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <errno.h>
#include "mem/io.h"
#include "util.h"

// Some of this based on this nice essay: http://xn--rpa.cc/essays/term

#define TERM_STREAM stdout
#define TERM_FILENO STDOUT_FILENO
#define WRITE_CONST(raw) fwrite(raw, 1, sizeof(raw), TERM_STREAM)
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

#define PLAIN "0"
#define FG "3"
#define BG "4"
#define BR_FG "9"
#define BR_BG "10"
#define BLACK "0"
#define RED "1"
#define GREEN "2"
#define YELLOW "3"
#define BLUE "4"
#define MAGNETA "5"
#define CYAN "6"
#define WHITE "7"

#define ALT_BUF "?1049"
#define CURS "?25"
#define TERM_CLEAR "2J"
#define CLEAR_LINE "2K"
#define HIGH "h"
#define LOW "l"
#define JUMP "H"

#define ESCA "\x1b["
#define FMT(f) ESCA f "m"

static void
usage(const char *argv0)
{
   fprintf(stderr, "usage: %s pid [regions]\n"
                   "       regions must be in /proc/<pid>/maps format", argv0);
   exit(EXIT_FAILURE);
}

struct key {
   unsigned char seq[6], i;
   bool is_csi;
};

static struct {
   struct named_region {
      struct region region;
      const char *name;
   } *named;
   size_t num_regions, allocated_regions, active_region;

   struct {
      char *buffer;
      const char *fmt;
      size_t pointer, size;
   } screen;

   struct {
      struct { unsigned char *data; size_t mapped; } memory[2];
      size_t scroll, offset;
      unsigned char octects_per_group;
   } hexview, last_hexview;

   struct {
      char err[255];
      struct termios initial, current;
      struct { unsigned int w; unsigned int h; } ws;
      struct { unsigned int x; unsigned int y; } cur;
   } term;

   struct key last_key;
   struct mem_io io;
} ctx = { .hexview.octects_per_group = 1 };

const char*
hex_for_byte(unsigned char byte)
{
   // optimization to avoid vprintf for the hex view
   static const char *hex[256] = {
      "00", "01", "02", "03", "04", "05", "06", "07", "08", "09", "0a", "0b", "0c", "0d", "0e", "0f",
      "10", "11", "12", "13", "14", "15", "16", "17", "18", "19", "1a", "1b", "1c", "1d", "1e", "1f",
      "20", "21", "22", "23", "24", "25", "26", "27", "28", "29", "2a", "2b", "2c", "2d", "2e", "2f",
      "30", "31", "32", "33", "34", "35", "36", "37", "38", "39", "3a", "3b", "3c", "3d", "3e", "3f",
      "40", "41", "42", "43", "44", "45", "46", "47", "48", "49", "4a", "4b", "4c", "4d", "4e", "4f",
      "50", "51", "52", "53", "54", "55", "56", "57", "58", "59", "5a", "5b", "5c", "5d", "5e", "5f",
      "60", "61", "62", "63", "64", "65", "66", "67", "68", "69", "6a", "6b", "6c", "6d", "6e", "6f",
      "70", "71", "72", "73", "74", "75", "76", "77", "78", "79", "7a", "7b", "7c", "7d", "7e", "7f",
      "80", "81", "82", "83", "84", "85", "86", "87", "88", "89", "8a", "8b", "8c", "8d", "8e", "8f",
      "90", "91", "92", "93", "94", "95", "96", "97", "98", "99", "9a", "9b", "9c", "9d", "9e", "9f",
      "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", "a8", "a9", "aa", "ab", "ac", "ad", "ae", "af",
      "b0", "b1", "b2", "b3", "b4", "b5", "b6", "b7", "b8", "b9", "ba", "bb", "bc", "bd", "be", "bf",
      "c0", "c1", "c2", "c3", "c4", "c5", "c6", "c7", "c8", "c9", "ca", "cb", "cc", "cd", "ce", "cf",
      "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d8", "d9", "da", "db", "dc", "dd", "de", "df",
      "e0", "e1", "e2", "e3", "e4", "e5", "e6", "e7", "e8", "e9", "ea", "eb", "ec", "ed", "ee", "ef",
      "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "fa", "fb", "fc", "fd", "fe", "ff"
   };
   return hex[byte];
}

static bool
offset_is_in_named_region(const size_t offset, const struct named_region *named)
{
   return (named->region.start <= offset && named->region.end >= offset);
}

static const struct named_region*
named_region_for_offset(const size_t offset, const bool set_active)
{
   if (!ctx.num_regions)
      return NULL;

   if (offset_is_in_named_region(offset, &ctx.named[ctx.active_region]))
      return &ctx.named[ctx.active_region];

   for (size_t i = 1 ; ctx.active_region + i < ctx.num_regions || ctx.active_region >= i; ++i) {
      if (ctx.active_region + i < ctx.num_regions &&
          offset_is_in_named_region(offset, &ctx.named[ctx.active_region + i])) {
         ctx.active_region += i * set_active;
         return &ctx.named[ctx.active_region + i * !set_active];
      }
      if (ctx.active_region >= i &&
          offset_is_in_named_region(offset, &ctx.named[ctx.active_region - i])) {
         ctx.active_region -= i * set_active;
         return &ctx.named[ctx.active_region - i * !set_active];
      }
   }

   return NULL;
}

static size_t
bytes_fits_row(void)
{
   // 000000000000: 00 00 00 00 00 00 00 00 00 .........
   const size_t preamble = snprintf(NULL, 0, "%.13zx: ", (size_t)0);
   if (ctx.term.ws.w <= preamble) return 0;
   return (ctx.term.ws.w - preamble) / ((ctx.hexview.octects_per_group * 3 /* 00?? + ws */) + ctx.hexview.octects_per_group /* for chr view */);
}

static size_t
bytes_fits_screen(void)
{
   if (ctx.term.ws.h <= 3) return 0;
   return bytes_fits_row() * (ctx.term.ws.h - 3); // 3 for top and bottom bars
}

static void
__attribute__((format(printf, 1, 2)))
screen_printf(const char *fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);
   ctx.screen.pointer += vsnprintf(ctx.screen.buffer + ctx.screen.pointer, ctx.screen.size - ctx.screen.pointer, fmt, ap);
   ctx.screen.pointer = (ctx.screen.pointer > ctx.screen.size ? ctx.screen.size : ctx.screen.pointer);
   va_end(ap);
}

static void
screen_print(const char *str)
{
   const size_t slen = strlen(str);
   const size_t len = (slen < ctx.screen.size - ctx.screen.pointer ? slen : ctx.screen.size - ctx.screen.pointer);
   memcpy(ctx.screen.buffer + ctx.screen.pointer, str, len);
   ctx.screen.pointer += len;
}

static void
screen_format(const char *fmt)
{
   // optimization for reducing format changes in hexview
   if (ctx.screen.fmt == fmt)
      return;

   if (ctx.screen.fmt)
      screen_print(FMT(PLAIN));

   screen_print((ctx.screen.fmt = fmt));
}

static void
screen_cursor(const unsigned int x, const unsigned int y)
{
   screen_printf(ESCA "%u;%u" JUMP, (ctx.term.cur.y = y) + 1, (ctx.term.cur.x = x) + 1);
}

static void
screen_fill(const unsigned int y, const char *str)
{
   screen_cursor(0, y);
   for (size_t x = 0; x < ctx.term.ws.w; ++x)
      screen_print(str);
}

static void
screen_flush(void)
{
   fwrite(ctx.screen.buffer, 1, ctx.screen.pointer, TERM_STREAM);
   ctx.screen.pointer = 0;
   ctx.screen.fmt = FMT(PLAIN);
}

static size_t
scroll_for_offset(const struct named_region *named, const size_t offset)
{
   if (!named)
      return 0;

   const size_t bw = bytes_fits_row(), bs = bytes_fits_screen();
   const size_t active_row = (offset - named->region.start) / bw;
   if (active_row * bw >= ctx.hexview.scroll + bs) {
      ctx.hexview.scroll = active_row * bw - (bs - bw);
   } else if (active_row * bw <= ctx.hexview.scroll) {
      ctx.hexview.scroll = active_row * bw;
   }

   return ctx.hexview.scroll;
}

static void
repaint_static_areas(void)
{
   screen_fill(1, "┉");
   screen_fill(ctx.term.ws.h - 1, "┉");
}

static void
repaint_top_bar(const struct named_region *named)
{
   screen_cursor(0, 0);
   screen_print(ESCA CLEAR_LINE);
   if (named) {
      screen_printf("%s", named->name);
   } else {
      screen_print("no region");
   }
}

static void
draw_error(const char *line, void *data)
{
   (void)data;
   const size_t w = snprintf(NULL, 0, "%s", line);
   screen_cursor(ctx.term.ws.w / 2 - w / 2, ctx.term.ws.h / 2);
   screen_printf("%s", line);
}

static void
repaint_hexview(const struct named_region *named, const bool update)
{
   if (!named)
      return;

   const size_t bs = bytes_fits_screen(), bw = bytes_fits_row();
   const size_t start = named->region.start + scroll_for_offset(named, ctx.hexview.offset), len = named->region.end - start;
   const bool scrolled = (ctx.hexview.scroll != ctx.last_hexview.scroll);

   if (update || scrolled) {
      memcpy(ctx.hexview.memory[1].data, ctx.hexview.memory[0].data, ctx.hexview.memory[0].mapped * !scrolled);
      ctx.hexview.memory[0].mapped = ctx.io.read(&ctx.io, ctx.hexview.memory[0].data, start, (bs > len ? len : bs));
      memcpy(ctx.hexview.memory[1].data, ctx.hexview.memory[0].data, ctx.hexview.memory[0].mapped * scrolled);
   }

   screen_format(FMT(PLAIN));
   for (size_t pointer = 0; pointer < ctx.hexview.memory[0].mapped;) {
      screen_cursor(0, 2 + (pointer / bw));
      screen_printf(FMT(FG YELLOW) "%.13zx: " FMT(PLAIN), start + pointer);
      ctx.screen.fmt = FMT(FG YELLOW);

      const size_t row_start = pointer;
      for (size_t x = 0; x < bw && pointer < bs; ++x) {
         const bool selected = (start + pointer == ctx.hexview.offset);

         bool changed = false;
         for (size_t o = pointer; o < pointer + ctx.hexview.octects_per_group && o < bs; ++o) {
            if (ctx.hexview.memory[0].data[o] == ctx.hexview.memory[1].data[o])
               continue;

            changed = true;
            break;
         }

         if (selected)
            screen_format(FMT(BR_BG RED));
         else if (changed)
            screen_format(FMT(FG RED));
         else
            screen_format(FMT(PLAIN));

         for (size_t o = 0; o < ctx.hexview.octects_per_group && pointer < bs; ++o) {
            if (pointer > ctx.hexview.memory[0].mapped) {
               screen_print("  ");
            } else {
               screen_print(hex_for_byte(ctx.hexview.memory[0].data[pointer++]));
            }
         }

         if (selected)
            screen_format(FMT(PLAIN));

         screen_print(" ");
      }

      for (size_t x = row_start; x < pointer; ++x) {
         const bool selected = (start + x == ctx.hexview.offset);
         const bool changed = (ctx.hexview.memory[0].data[x] != ctx.hexview.memory[1].data[x]);

         if (selected)
            screen_format(FMT(BR_BG RED));
         else if (changed)
            screen_format(FMT(FG RED));
         else
            screen_format(FMT(FG CYAN));

         if (x > ctx.hexview.memory[0].mapped) {
            screen_print(" ");
         } else {
            screen_print((char[]){(isprint(ctx.hexview.memory[0].data[x]) ? ctx.hexview.memory[0].data[x] : '.'), 0});
         }
      }
   }
   screen_format(FMT(PLAIN));

   if (!ctx.hexview.memory[0].mapped)
      screen_cursor(0, 1);

   for (size_t i = 0; i < (bs - ctx.hexview.memory[0].mapped) / bw; ++i) {
      screen_cursor(0, ctx.term.cur.y + 1);
      screen_print(ESCA CLEAR_LINE);
   }

   if (!ctx.hexview.memory[0].mapped && ctx.term.ws.h > 3) {
      fflush(stderr);
      for_each_token_in_str(ctx.term.err, '\n', draw_error, NULL);
      memset(ctx.term.err, 0, sizeof(ctx.term.err));
   }
}

static void
repaint_dynamic_areas(const bool full_repaint)
{
   const struct named_region *last_active = &ctx.named[ctx.active_region];
   const struct named_region *named = named_region_for_offset(ctx.hexview.offset, true);

   if (named != last_active) {
      repaint_top_bar(named);
      ctx.last_hexview.scroll = (size_t)~0; // avoid diffing
   }

   if (named && memcmp(&ctx.hexview, &ctx.last_hexview, sizeof(ctx.hexview))) {
      repaint_hexview(named, (full_repaint || named != last_active));
      ctx.last_hexview = ctx.hexview;
   }

   screen_cursor(0, ctx.term.ws.h);
   screen_print(ESCA CLEAR_LINE);
   screen_printf("%zx", ctx.hexview.offset);

   const unsigned char *seq = ctx.last_key.seq + ctx.last_key.is_csi;
   const int seq_len = ctx.last_key.i - ctx.last_key.is_csi;
   const size_t rstrlen = snprintf(NULL, 0, "%s%.*s %zu/%zu", (ctx.last_key.is_csi ? "^[" : ""), seq_len, seq, ctx.active_region + 1, ctx.num_regions);
   screen_cursor(ctx.term.ws.w - rstrlen, ctx.term.ws.h);
   screen_printf("%s%.*s %zu/%zu", (ctx.last_key.is_csi ? "^[" : ""), seq_len, seq, ctx.active_region + 1, ctx.num_regions);
}

static void
repaint(void)
{
   ctx.last_hexview.scroll = (size_t)~0; // avoid diffing
   screen_print(ESCA TERM_CLEAR);
   repaint_static_areas();
   repaint_top_bar(named_region_for_offset(ctx.hexview.offset, false));
   repaint_dynamic_areas(true);
   screen_flush();
}

static void
resize(int sig)
{
   signal(sig, SIG_IGN);

   struct winsize ws;
   ioctl(TERM_FILENO, TIOCGWINSZ, &ws);
   ctx.term.ws.w = (ws.ws_col > 0 ? ws.ws_col : 0);
   ctx.term.ws.h = (ws.ws_row > 0 ? ws.ws_row - 1 : 0);

   for (size_t i = 0; i < ARRAY_SIZE(ctx.hexview.memory); ++i) {
      free(ctx.hexview.memory[i].data); ctx.hexview.memory[i].data = NULL;
      if (!(ctx.hexview.memory[i].data = malloc(bytes_fits_screen())))
         err(EXIT_FAILURE, "malloc");
   }

   ctx.screen.pointer = 0;
   ctx.screen.size = (ctx.term.ws.w * ctx.term.ws.h) * 2; // bit extra for formatting
   free(ctx.screen.buffer); ctx.screen.buffer = NULL;
   if (!(ctx.screen.buffer = malloc(ctx.screen.size)))
      err(EXIT_FAILURE, "malloc");

   repaint();
   signal(sig, resize);
}

static void
next_region(int arg)
{
   size_t region;
   if (arg < 0 && ctx.active_region < (size_t)(arg * -1))
      region = ctx.num_regions - ((arg * -1) - ctx.active_region);
   else
      region = (ctx.active_region + arg) % ctx.num_regions;
   ctx.hexview.offset = ctx.named[region].region.start;
}

enum {
   MOVE_PAGE_UP,
   MOVE_PAGE_DOWN,
   MOVE_START,
   MOVE_END,
   MOVE_UP,
   MOVE_DOWN,
   MOVE_RIGHT,
   MOVE_LEFT
};

static void
navigate(int arg)
{
   const struct named_region *named;
   if (!(named = named_region_for_offset(ctx.hexview.offset, true)))
      return;

   switch (arg) {
      case MOVE_PAGE_UP: {
            const size_t bs = bytes_fits_screen();
            if (bs <= ctx.hexview.offset - named->region.start) {
               ctx.hexview.offset -= bs;
               ctx.hexview.scroll -= bs;
            }
         } break;
      case MOVE_PAGE_DOWN: {
            const size_t bs = bytes_fits_screen();
            if (bs <= named->region.end - ctx.hexview.offset) {
               ctx.hexview.offset += bs;
               ctx.hexview.scroll += bs;
            }
         } break;
      case MOVE_START:
         ctx.hexview.offset = named->region.start;
         break;
      case MOVE_END:
         ctx.hexview.offset = named->region.end;
         break;
      case MOVE_UP: {
            const size_t bw = bytes_fits_row();
            if (bw <= ctx.hexview.offset - named->region.start)
               ctx.hexview.offset -= bw;
         } break;
      case MOVE_DOWN: {
            const size_t bw = bytes_fits_row();
            if (bw <= named->region.end - ctx.hexview.offset)
               ctx.hexview.offset += bw;
         } break;
      case MOVE_LEFT:
         ctx.hexview.offset -= !!(ctx.hexview.offset - named->region.start) * ctx.hexview.octects_per_group;
         break;
      case MOVE_RIGHT:
         ctx.hexview.offset += !!(named->region.end - ctx.hexview.offset) * ctx.hexview.octects_per_group;
         break;
   }
}

static void
key_press(const struct key *key)
{
   const struct {
      unsigned char seq[sizeof(key->seq)];
      void (*fun)(int);
      int arg;
   } keys[] = {
      { .seq = { 0x1b, '[', '1', ';', '2', 'C' }, .fun = next_region, .arg = 1 },
      { .seq = { 0x1b, '[', '1', ';', '2', 'D' }, .fun = next_region, .arg = -1 },
      { .seq = { 0x1b, '[', '5', '~' }, .fun = navigate, .arg = MOVE_PAGE_UP },
      { .seq = { 0x1b, '[', '6', '~' }, .fun = navigate, .arg = MOVE_PAGE_DOWN },
      { .seq = { 0x1b, '[', 'H' }, .fun = navigate, .arg = MOVE_START },
      { .seq = { 0x1b, '[', 'F' }, .fun = navigate, .arg = MOVE_END },
      { .seq = { 0x1b, '[', 'A' }, .fun = navigate, .arg = MOVE_UP },
      { .seq = { 0x1b, '[', 'B' }, .fun = navigate, .arg = MOVE_DOWN },
      { .seq = { 0x1b, '[', 'C' }, .fun = navigate, .arg = MOVE_RIGHT },
      { .seq = { 0x1b, '[', 'D' }, .fun = navigate, .arg = MOVE_LEFT },
   };

   for (size_t i = 0; i < ARRAY_SIZE(keys); ++i) {
      if (memcmp(keys[i].seq, key->seq, key->i))
         continue;

      keys[i].fun(keys[i].arg);
      break;
   }

   ctx.last_key = *key;
}

static void
quit(void)
{
   for (size_t i = 0; i < ctx.num_regions; ++i)
      free((char*)ctx.named[i].name);

   mem_io_release(&ctx.io);

   for (size_t i = 0; i < ARRAY_SIZE(ctx.hexview.memory); ++i)
      free(ctx.hexview.memory[i].data);

   free(ctx.screen.buffer);

   if (!memcmp(&ctx.term.initial, &ctx.term.current, sizeof(ctx.term.initial)))
      return;

   WRITE_CONST(
      ESCA ALT_BUF HIGH
      ESCA TERM_CLEAR
      ESCA CURS HIGH
      ESCA ALT_BUF LOW
   );

   tcsetattr(TERM_FILENO, TCSANOW, &ctx.term.initial);
   memset(&ctx, 0, sizeof(ctx));
}

static void
init(void)
{
   freopen("/dev/null", "wb", stderr);
   setvbuf(stderr, ctx.term.err, _IOFBF, sizeof(ctx.term.err));
   setvbuf(TERM_STREAM, NULL, _IONBF, 0);
   tcgetattr(TERM_FILENO, &ctx.term.initial);
   ctx.term.current = ctx.term.initial;

   atexit(quit);
   signal(SIGTERM, exit);
   signal(SIGINT, exit);
   signal(SIGSEGV, exit);
   signal(SIGABRT, exit);

   ctx.term.current.c_lflag &= (~ECHO & ~ICANON);
   tcsetattr(TERM_FILENO, TCSANOW, &ctx.term.current);

   WRITE_CONST(
      ESCA ALT_BUF HIGH
      ESCA TERM_CLEAR
      ESCA CURS LOW
   );
}

static const char*
basename(const char *path)
{
   const char *base = strrchr(path, '/');
   return (base ? base + 1 : path);
}

static void
region_cb(const char *line, void *data)
{
   (void)data;

   const size_t step = 1024;
   if (ctx.num_regions >= ctx.allocated_regions &&
      !(ctx.named = realloc(ctx.named, sizeof(*ctx.named) * (ctx.allocated_regions += step))))
      err(EXIT_FAILURE, "realloc");

   if (!region_parse(&ctx.named[ctx.num_regions].region, line))
       return;

   char path[255] = {0};
   int region_len_without_name = strlen(line);
   sscanf(line, "%*s %*s %*s %*s %*s%n %254s", &region_len_without_name, path);
   const char *base = basename(path);

   char *name;
   size_t name_sz = region_len_without_name + 1 + strlen(base) + 1;
   if (name_sz > 1 && !(name = malloc(name_sz)))
      err(EXIT_FAILURE, "malloc");

   snprintf(name, name_sz, "%.*s %s", region_len_without_name, line, base);
   ctx.active_region = (strstr(name, "[heap]") ? ctx.num_regions : ctx.active_region);
   ctx.named[ctx.num_regions++].name = name;
}

static bool
ignored(char c)
{
   return (c == 0x00 || (c >= 0x07 && c <= 0x0f) || c == 0x7f);
}

int
main(int argc, char *argv[])
{
   if (argc < 2)
      usage(argv[0]);

   const pid_t pid = strtoull(argv[1], NULL, 10);

   FILE *regions_file = NULL;
   if (argc > 2 && !(regions_file = fopen(argv[2], "rb"))) {
      err(EXIT_FAILURE, "fopen(%s)", argv[2]);
   } else {
      char path[128];
      snprintf(path, sizeof(path), "/proc/%u/maps", pid);
      if (!(regions_file = fopen(path, "rb")))
         err(EXIT_FAILURE, "fopen(%s)", path);
   }

   mem_io_uio_init(&ctx.io, pid);
   for_each_token_in_file(regions_file, '\n', region_cb, NULL);
   fclose(regions_file);
   ctx.hexview.offset = ctx.named[ctx.active_region].region.start;

   init();
   signal(SIGWINCH, resize);
   resize(0);

   struct key press = {0};
   while (true) {
      fd_set set;
      FD_ZERO(&set);
      FD_SET(TERM_FILENO, &set);
      struct timeval timeout = { .tv_sec = 1 };
      if (select(TERM_FILENO + 1, &set, NULL, NULL, &timeout) < 0) {
         if (errno == EINTR)
            continue;

         err(EXIT_FAILURE, "select");
      }

      if (!FD_ISSET(TERM_FILENO, &set)) {
         // timeout
         repaint_hexview(named_region_for_offset(ctx.hexview.offset, false), true);
         screen_flush();
         continue;
      }

      unsigned char input;
      fread(&input, 1, 1, TERM_STREAM);

      if (ignored(input))
         continue;

      switch (input) {
         case 0x04:
            goto quit;
         case 0x18: // ^X
         case 0x1a: // ^Z
            press = (struct key){0};
            break;
         case 0x1b: // ^[
            press = (struct key){0};
            fread(&input, 1, 1, TERM_STREAM);
            if (input != '[') {
               press.i = 0;
               break;
            }
            /* fallthrough */
         case 0x9b: // CSI
            press = (struct key){0};
            press.seq[press.i++] = 0x1b;
            press.seq[press.i++] = '[';
            press.is_csi = true;
            break;
         default:
            press.seq[press.i++] = input;
            if (!(press.is_csi && ((input >= '0' && input <= '9') || input == ';'))) {
               key_press(&press);
               press = (struct key){0};
               repaint_dynamic_areas(false);
               screen_flush();
            }
            break;
      }
   }

quit:
   return EXIT_SUCCESS;
}