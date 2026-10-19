#!/bin/sh
# SPDX-License-Identifier: LGPL-3.0-only
#
# Fail unless the hash a probe prints is the committed one (CAP-13).
#
# Usage: check-corpus-hash.sh PROBE HASH_FILE
#
# PROBE is tools/corpus-hash.c built for this host (or a planted variant);
# HASH_FILE is tests/corpus/coverage.hash, whose `hash` and `count` lines are
# the committed values.  No value may be empty, so a probe that crashed, or a
# hash file that was emptied, fails rather than comparing nothing with nothing.
probe="${1:?usage: check-corpus-hash.sh PROBE HASH_FILE}"
file="${2:?usage: check-corpus-hash.sh PROBE HASH_FILE}"

got="$("$probe")" || { printf 'check-corpus-hash: the probe failed\n' >&2; exit 1; }
got_hash=$(printf '%s\n' "$got" | awk '$1=="shape-cpu.hash"{print $2}')
got_count=$(printf '%s\n' "$got" | awk '$1=="shape-cpu.count"{print $2}')
want_hash=$(awk '$1=="hash"{print $2}' "$file" 2>/dev/null)
want_count=$(awk '$1=="count"{print $2}' "$file" 2>/dev/null)
if [ -z "$want_hash" ] || [ -z "$want_count" ] || [ -z "$got_hash" ] || [ -z "$got_count" ]; then
  printf 'check-corpus-hash: nothing to compare (probe %s/%s, committed %s/%s in %s)\n' \
    "$got_hash" "$got_count" "$want_hash" "$want_count" "$file" >&2
  exit 1
fi
if [ "$got_hash" != "$want_hash" ] || [ "$got_count" != "$want_count" ]; then
  printf 'check-corpus-hash: FAIL: %s over %s results, committed %s over %s\n' \
    "$got_hash" "$got_count" "$want_hash" "$want_count" >&2
  exit 1
fi
printf 'check-corpus-hash: ok, %s over %s results\n' "$got_hash" "$got_count"
