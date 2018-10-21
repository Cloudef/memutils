#!/bin/bash
# usage: ./brute-map.bash pid file [window-size] < regions
# Sometimes region offsets aren't available, but we know that some regions map a file
# Fix the region offsets by bruteforcing the offsets from a known file
while read -r region; do
   offset=$(printf '%d' "0x$(awk '{print $3}' <<<"$region")")
   if ((offset == 0)); then
      offset=$(binsearch <(ptrace-region-rw "$1" read <(printf '%s\n' "$region") | bintrim) first $3 < "$2" 2>/dev/null)
      if [[ -n "$offset" ]]; then
         hex=$(printf '%.8x' "$offset")
         awk '{printf "%s %s %s %s %s %s\n", $1, $2, "'"$hex"'", $4, $5, $6, $7}' <<<"$region"
      fi
   else
      printf '%s\n' "$region"
   fi
done
