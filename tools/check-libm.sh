#!/bin/sh
#
# Fail if a library references or defines a libm transcendental (AD-22).
#
# glibc, musl, UCRT and Apple's libm differ in the last bit of the functions C
# and IEEE 754 do not require to be correctly rounded, so a shape library whose
# output is bit-identical across hosts takes sin, cos, tan, atan2, exp, log, pow
# and their kin from `math`, which is written for the purpose. The functions the
# standard does specify exactly (sqrt, fma, floor, ceil, rint, fabs, fmod and
# the like) may come from libm and are not on the list below.
#
# What is read is the symbol table, so the gate sees the link, not the source: a
# call that was inlined away is not a reference, and one a header hid behind a
# macro is. For a shared object the dynamic table is read (`nm -D`, where a
# glibc symbol is `sin@GLIBC_2.2.5` and the version suffix is stripped);
# for an object or an archive, the ordinary one. A name is refused in its double
# form, its `f` (float) form and its `l` (long double) form, undefined or
# defined: a library that defines its own `sin` has done the thing the gate
# exists to stop by another route.
#
# --exempt is for `math` alone. Its own objects are the one place these names
# may appear; the gate still reads them and says how many, and refuses nothing.
#
# Usage: check-libm.sh [--exempt] <file>...
#   a file is an object, an archive or a shared object. An empty list, a file
#   with no symbol table, or a population that yields no symbol at all fails: a
#   gate that read nothing would pass any planted reference.
#
# The same text is in math, shape-geometry, shape-scene and shape-cpu;
# suite/tools/xarch/shape.sh fails when the four copies differ.

set -u

exempt=no
if [ "${1:-}" = --exempt ]; then
  exempt=yes
  shift
fi
if [ $# -eq 0 ]; then
  printf 'check-libm: no files given; this gate is measuring nothing\n' >&2
  exit 1
fi

# The base names. Each is also refused with an f and an l suffix.
BASES='sin cos tan asin acos atan atan2 sinh cosh tanh asinh acosh atanh
exp exp2 exp10 expm1 log log2 log10 log1p logb pow hypot cbrt sincos
erf erfc tgamma lgamma'

names=""
for b in $BASES; do
  names="$names $b ${b}f ${b}l"
done

NM="${NM:-nm}"
status=0
nfiles=0
nsyms=0
nhits=0

for f in "$@"; do
  nfiles=$((nfiles + 1))
  if [ ! -f "$f" ]; then
    printf 'check-libm: %s is not a file\n' "$f" >&2
    exit 1
  fi
  case "$f" in
    *.so | *.so.* | *.dll | *.dylib) syms="$("$NM" -D "$f" 2>/dev/null)" ;;
    *) syms="$("$NM" "$f" 2>/dev/null)" ;;
  esac
  rc=$?
  if [ "$rc" -ne 0 ] || [ -z "$syms" ]; then
    # An object with no symbols at all is not a library; refuse it rather than
    # certify it. (A shared object always has at least its imports.)
    printf 'check-libm: read no symbols from %s; the gate cannot see this file\n' "$f" >&2
    exit 1
  fi
  # "<type> <name>" per line; an archive adds blank lines and "member.o:" headers.
  list="$(printf '%s\n' "$syms" | awk '
    NF >= 2 && $(NF-1) ~ /^[A-Za-z?]$/ { n = $NF; sub(/@.*/, "", n); print $(NF-1), n }' | sort -u)"
  nsyms=$((nsyms + $(printf '%s\n' "$list" | sed '/^$/d' | wc -l)))
  for n in $names; do
    hit="$(printf '%s\n' "$list" | awk -v n="$n" '$2 == n { print $1 }' | tr '\n' ' ')"
    [ -n "$hit" ] || continue
    nhits=$((nhits + 1))
    kind=referenced
    case "$hit" in
      *[TtDdBbRrWV]*) kind="defined" ;;
    esac
    if [ "$exempt" = yes ]; then
      continue
    fi
    printf 'check-libm: %s: libm transcendental %s is %s (AD-22: take it from math)\n' \
      "$f" "$n" "$kind" >&2
    status=1
  done
done

if [ "$nsyms" -eq 0 ]; then
  printf 'check-libm: read no symbols from %d files; this gate is measuring nothing\n' "$nfiles" >&2
  exit 1
fi
if [ "$status" -ne 0 ]; then
  exit 1
fi
if [ "$exempt" = yes ]; then
  printf 'check-libm: %d files, %d symbols read, %d libm names present; exempt (the definer, AD-22)\n' \
    "$nfiles" "$nsyms" "$nhits"
else
  printf 'check-libm: %d files, %d symbols read, none a libm transcendental\n' "$nfiles" "$nsyms"
fi
