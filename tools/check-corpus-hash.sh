#!/bin/sh
# SPDX-License-Identifier: LGPL-3.0-only
#
# Fail unless the hashes a probe prints are the committed ones (CAP-13).
#
# Usage: check-corpus-hash.sh PROBE HASH_FILE [RENDER_FILE [DENSE_FILE [FAST_FILE [FILTER_FILE [FILTER_FAST_FILE]]]]]
#
# PROBE is tools/corpus-hash.c built for this host (or a planted variant);
# HASH_FILE is tests/corpus/coverage.hash, whose `hash` and `count` lines are
# the committed values of the coverage part.  RENDER_FILE, when given, is
# tests/corpus/render.hash: one `render FIXTURE FORMAT HASH` line per render
# and the `hash` and `count` of their fold, each of which the probe must print
# (story 13b).  DENSE_FILE, when given, is tests/corpus/dense.hash: the `hash` and `count` of the fold of the dense cells of
# tests/dense_vectors.h through the exact step (strokes story S4c).  FAST_FILE, when given, is tests/corpus/render-fast.hash: the
# renders of the same fixtures under the fast coverages, in the form of RENDER_FILE.  FILTER_FILE, when given, is
# tests/corpus/filter.hash: the renders of the filter scenes of tests/filter_fixtures.h, in the same form, and FILTER_FAST_FILE
# tests/corpus/filter-fast.hash: the same scenes under the fast coverages.  No value may be empty, so a probe that crashed, or a hash file
# that was emptied, fails rather than comparing nothing with nothing.
probe="${1:?usage: check-corpus-hash.sh PROBE HASH_FILE [RENDER_FILE [DENSE_FILE [FAST_FILE [FILTER_FILE [FILTER_FAST_FILE]]]]]}"
file="${2:?usage: check-corpus-hash.sh PROBE HASH_FILE [RENDER_FILE [DENSE_FILE [FAST_FILE [FILTER_FILE [FILTER_FAST_FILE]]]]]}"
renders="${3:-}"
dense="${4:-}"
fast="${5:-}"
filters="${6:-}"
filters_fast="${7:-}"

got="$("$probe" ${PROBE_ARGS:-})" || { printf 'check-corpus-hash: the probe failed\n' >&2; exit 1; }
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

# check_lines TAG WHAT FILE: the probe's `shape-cpu.TAG NAME FORMAT HASH` lines, `shape-cpu.TAG-hash` and `shape-cpu.TAG-count` against the
# `render`, `hash` and `count` lines of FILE.  WHAT names them in a message.  Exits 1 on a difference; no value may be empty.
check_lines() {
  tag="$1"; what="$2"; file="$3"
  got_h=$(printf '%s\n' "$got" | awk -v t="shape-cpu.$tag-hash" '$1==t{print $2}')
  got_c=$(printf '%s\n' "$got" | awk -v t="shape-cpu.$tag-count" '$1==t{print $2}')
  want_h=$(awk '$1=="hash"{print $2}' "$file" 2>/dev/null)
  want_c=$(awk '$1=="count"{print $2}' "$file" 2>/dev/null)
  got_l=$(printf '%s\n' "$got" | awk -v t="shape-cpu.$tag" '$1==t{print $2, $3, $4}')
  want_l=$(awk '$1=="render"{print $2, $3, $4}' "$file" 2>/dev/null)
  if [ -z "$want_h" ] || [ -z "$want_c" ] || [ -z "$got_h" ] || [ -z "$got_c" ] || [ -z "$want_l" ] || [ -z "$got_l" ]; then
    printf 'check-corpus-hash: no %s to compare (probe %s/%s, committed %s/%s in %s)\n' \
      "$what" "$got_h" "$got_c" "$want_h" "$want_c" "$file" >&2
    exit 1
  fi
  if [ "$got_l" != "$want_l" ]; then
    printf 'check-corpus-hash: FAIL: these %s differ from %s:\n' "$what" "$file" >&2
    printf '%s\n' "$got_l" > "${TMPDIR:-/tmp}/check-corpus-hash.$$.got"
    printf '%s\n' "$want_l" | diff - "${TMPDIR:-/tmp}/check-corpus-hash.$$.got" | sed -n 's/^> /  probe: /p; s/^< /  committed: /p' >&2
    rm -f "${TMPDIR:-/tmp}/check-corpus-hash.$$.got"
    exit 1
  fi
  if [ "$got_h" != "$want_h" ] || [ "$got_c" != "$want_c" ]; then
    printf 'check-corpus-hash: FAIL: %s %s over %s, committed %s over %s\n' \
      "$what" "$got_h" "$got_c" "$want_h" "$want_c" >&2
    exit 1
  fi
  printf 'check-corpus-hash: ok, %s %s over %s\n' "$what" "$got_h" "$got_c"
}

check_lines render renders "$renders"
[ -n "$dense" ] || exit 0

got_dhash=$(printf '%s\n' "$got" | awk '$1=="shape-cpu.dense-hash"{print $2}')
got_dcount=$(printf '%s\n' "$got" | awk '$1=="shape-cpu.dense-count"{print $2}')
want_dhash=$(awk '$1=="hash"{print $2}' "$dense" 2>/dev/null)
want_dcount=$(awk '$1=="count"{print $2}' "$dense" 2>/dev/null)
if [ -z "$want_dhash" ] || [ -z "$want_dcount" ] || [ -z "$got_dhash" ] || [ -z "$got_dcount" ]; then
  printf 'check-corpus-hash: no dense cells to compare (probe %s/%s, committed %s/%s in %s)\n' \
    "$got_dhash" "$got_dcount" "$want_dhash" "$want_dcount" "$dense" >&2
  exit 1
fi
if [ "$got_dhash" != "$want_dhash" ] || [ "$got_dcount" != "$want_dcount" ]; then
  printf 'check-corpus-hash: FAIL: dense cells %s over %s, committed %s over %s\n' \
    "$got_dhash" "$got_dcount" "$want_dhash" "$want_dcount" >&2
  exit 1
fi
printf 'check-corpus-hash: ok, dense cells %s over %s\n' "$got_dhash" "$got_dcount"
[ -n "$fast" ] || exit 0

check_lines render-fast "fast renders" "$fast"
[ -n "$filters" ] || exit 0

check_lines filter "filter renders" "$filters"
[ -n "$filters_fast" ] || exit 0

check_lines filter-fast "fast filter renders" "$filters_fast"
