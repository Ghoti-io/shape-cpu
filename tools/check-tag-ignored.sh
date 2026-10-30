#!/bin/sh
# SPDX-License-Identifier: LGPL-3.0-only
#
# Copyright (C) 2026 Corey Pennycuff
#
# Fail if the renderer reads a path resource's primitive tag.
#
# A path resource may carry a tag (a rect, a rounded rect or an ellipse, with the flips and the parameters it was built from) that a
# document writer or an analytic route reads.  This sink draws a path resource by its path, and the tag never changes coverage: a tagged
# path and its untagged copy render bit for bit alike.  The test (tests/unit/test_render_primitives.cpp) shows it over a matrix; this
# gate is the other half, that no source of the renderer so much as asks, so that the equality cannot be broken by a branch the matrix
# does not reach.  It refuses the accessor, the kind enum and the type in every file under the directory it is given.
#
# Usage: check-tag-ignored.sh [SRC_DIR]     (default: src, next to this directory)
#   The gate first shows itself failing on a planted call and passing on a control, so a gate that read nothing fails.
set -u

here="$(cd "$(dirname "$0")" && pwd)"
dir="${1:-$here/../src}"

PATTERN='gshscene_resource_primitive|GSHSCENE_Primitive|GSHSCENE_PRIMITIVE_|gshscene_store_intern_primitive'

# scan DIR -> prints each offending line; exit 1 if any, 2 if nothing was read.
scan() {
  files=$(find "$1" -type f \( -name '*.c' -o -name '*.h' -o -name '*.cpp' \) 2>/dev/null | sort)
  if [ -z "$files" ]; then
    printf 'check-tag-ignored: no source under %s: this would measure nothing\n' "$1" >&2
    return 2
  fi
  hits=$(printf '%s\n' "$files" | xargs grep -nE "$PATTERN" 2>/dev/null)
  if [ -n "$hits" ]; then
    printf '%s\n' "$hits"
    return 1
  fi
  return 0
}

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT INT TERM
mkdir -p "$work/control" "$work/call" "$work/kind" "$work/type" "$work/empty"
printf 'int f(void) { return 0; }\n' > "$work/control/a.c"
printf 'void f(const void * r) { GSHSCENE_Primitive p; gshscene_resource_primitive(r, &p); }\n' > "$work/call/a.c"
printf 'int f(int k) { return k == GSHSCENE_PRIMITIVE_ELLIPSE; }\n' > "$work/kind/a.c"
printf 'static GSHSCENE_Primitive p;\n' > "$work/type/a.h"
bad=0
scan "$work/control" > /dev/null 2>&1 || { printf 'check-tag-ignored: FAIL: the control was refused\n' >&2; bad=1; }
for planted in call kind type; do
  if scan "$work/$planted" > /dev/null 2>&1; then
    printf 'check-tag-ignored: FAIL: the planted %s passed\n' "$planted" >&2
    bad=1
  fi
done
scan "$work/empty" > /dev/null 2>&1
[ $? -eq 2 ] || { printf 'check-tag-ignored: FAIL: an empty directory did not fail\n' >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1

out="$(scan "$dir")"
rc=$?
if [ "$rc" -ne 0 ]; then
  printf 'check-tag-ignored: FAIL: the renderer reads a primitive tag (a tag never changes coverage):\n%s\n' "$out" >&2
  exit 1
fi
printf 'check-tag-ignored: ok, no source under %s reads a primitive tag; the gate fails on a planted call, kind and type\n' "$dir"
