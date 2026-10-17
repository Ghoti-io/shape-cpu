# Design

**Status:** scaffold. The library builds, installs a pkg-config file and
reports its version; nothing else is implemented.

The CPU rasterizer for the shape family, bit-identical on every host.

The dependency rules and the determinism gates that bind this library are in
the Makefile's header comment and in `tools/`: `check-fp-contract`,
`check-edges`, `check-libm` and `check-no-x87`, each shown to fail on a planted
violation by `make check-gates`. The plan is in the workspace, not in this
repository.
