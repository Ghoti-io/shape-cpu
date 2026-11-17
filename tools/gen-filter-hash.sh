#!/bin/sh
# SPDX-License-Identifier: LGPL-3.0-only
#
# Write tests/corpus/filter.hash and tests/corpus/filter-fast.hash from what a probe prints.
#
# Usage: gen-filter-hash.sh PROBE
#
# PROBE is tools/corpus-hash.c built for this host.  The committed files are a record of what the filter engine drew
# on the day the scenes were frozen: regenerate them only for a change that is meant to change a pixel of a filter
# scene (a fixed defect in a primitive, a scene added), and say in the commit which scenes moved and why.  The
# cross-target gates compare every target with these files, so a value taken from a target that disagrees with the
# others would only hide the disagreement: take it from x86-64, and run suite/tools/xarch/shape.sh after.
set -e
probe="${1:?usage: gen-filter-hash.sh PROBE}"
dir="$(dirname "$0")/../tests/corpus"
out="$("$probe")"
for tier in filter filter-fast; do
  if [ "$tier" = filter ]; then
    what='the default coverages'
    extra=''
  else
    what='the fast coverages (GSHCPU_FILL_COVERAGE_FAST and GSHCPU_STROKE_COVERAGE_FAST)'
    extra=' Integer cells and byte layers change the source graphic the filter reads, and the filter engine itself is the same code.'
  fi
  {
    printf '# The committed renders of the filter scenes (tests/filter_fixtures.h) under %s, one line per scene and format:\n' "$what"
    printf '# gshcpu_render_hash of the whole raster.  The scenes cover every primitive of the filter engine, both color spaces, 8-bit\n'
    printf '# and float targets, maps that turn, shear, scale unevenly, mirror and turn by a quarter, backdrop filters and slots of the picture of a filter.%s Every target\n' "$extra"
    printf '# must print these (make check-corpus-hash; suite/tools/xarch/shape.sh; suite/tools/xwin/shape.sh).  tools/gen-filter-hash.sh writes this file.\n'
    printf '%s\n' "$out" | awk -v t="shape-cpu.$tier" '$1==t{print "render", $2, $3, $4}'
    printf '%s\n' "$out" | awk -v t="shape-cpu.$tier-hash" '$1==t{print "hash", $2}'
    printf '%s\n' "$out" | awk -v t="shape-cpu.$tier-count" '$1==t{print "count", $2}'
  } > "$dir/$tier.hash"
done
