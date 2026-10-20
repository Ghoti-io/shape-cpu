#!/bin/sh
# SPDX-License-Identifier: LGPL-3.0-only
#
# Fail unless the hashes a probe prints are the committed ones (CAP-13).
#
# Usage: check-corpus-hash.sh PROBE HASH_FILE [RENDER_FILE]
#
# PROBE is tools/corpus-hash.c built for this host (or a planted variant);
# HASH_FILE is tests/corpus/coverage.hash, whose `hash` and `count` lines are
# the committed values of the coverage part.  RENDER_FILE, when given, is
# tests/corpus/render.hash: one `render FIXTURE FORMAT HASH` line per render
# and the `hash` and `count` of their fold, each of which the probe must print
# (story 13b).  No value may be empty, so a probe that crashed, or a hash file
# that was emptied, fails rather than comparing nothing with nothing.
probe="${1:?usage: check-corpus-hash.sh PROBE HASH_FILE [RENDER_FILE]}"
file="${2:?usage: check-corpus-hash.sh PROBE HASH_FILE [RENDER_FILE]}"
renders="${3:-}"

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
[ -n "$renders" ] || exit 0

got_rhash=$(printf '%s\n' "$got" | awk '$1=="shape-cpu.render-hash"{print $2}')
got_rcount=$(printf '%s\n' "$got" | awk '$1=="shape-cpu.render-count"{print $2}')
want_rhash=$(awk '$1=="hash"{print $2}' "$renders" 2>/dev/null)
want_rcount=$(awk '$1=="count"{print $2}' "$renders" 2>/dev/null)
got_lines=$(printf '%s\n' "$got" | awk '$1=="shape-cpu.render"{print $2, $3, $4}')
want_lines=$(awk '$1=="render"{print $2, $3, $4}' "$renders" 2>/dev/null)
if [ -z "$want_rhash" ] || [ -z "$want_rcount" ] || [ -z "$got_rhash" ] || [ -z "$got_rcount" ] \
  || [ -z "$want_lines" ] || [ -z "$got_lines" ]; then
  printf 'check-corpus-hash: no renders to compare (probe %s/%s, committed %s/%s in %s)\n' \
    "$got_rhash" "$got_rcount" "$want_rhash" "$want_rcount" "$renders" >&2
  exit 1
fi
if [ "$got_lines" != "$want_lines" ]; then
  printf 'check-corpus-hash: FAIL: these renders differ from %s:\n' "$renders" >&2
  printf '%s\n' "$got_lines" > "${TMPDIR:-/tmp}/check-corpus-hash.$$.got"
  printf '%s\n' "$want_lines" | diff - "${TMPDIR:-/tmp}/check-corpus-hash.$$.got" | sed -n 's/^> /  probe: /p; s/^< /  committed: /p' >&2
  rm -f "${TMPDIR:-/tmp}/check-corpus-hash.$$.got"
  exit 1
fi
if [ "$got_rhash" != "$want_rhash" ] || [ "$got_rcount" != "$want_rcount" ]; then
  printf 'check-corpus-hash: FAIL: renders %s over %s, committed %s over %s\n' \
    "$got_rhash" "$got_rcount" "$want_rhash" "$want_rcount" >&2
  exit 1
fi
printf 'check-corpus-hash: ok, renders %s over %s\n' "$got_rhash" "$got_rcount"
