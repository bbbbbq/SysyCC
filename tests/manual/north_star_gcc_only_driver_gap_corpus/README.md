# North-star Driver Compatibility Corpus

This manual corpus contains 20 small two-source C project shapes that a host
GCC/Clang-style compiler can compile and run. SysyCC is expected to compile,
link, and run these cases successfully as part of the "can act as CC for real
projects" track.

Run it manually with:

```bash
tests/manual/north_star_gcc_only_driver_gap_corpus/verify.sh
```

These cases intentionally focus on driver compatibility, not C language
semantics. The shared fixture is deliberately simple so each case points to a
specific command-line surface area that real Make/Ninja/CMake projects often
emit.

Current compatibility groups:

- C standard modes: `-std=c11`, `-std=gnu17`
- Optimization modes: `-O2`, `-O3`, `-Os`, `-Og`
- Code-generation toggles: `-funsigned-char`, `-fsigned-char`,
  `-fno-strict-overflow`, `-fno-delete-null-pointer-checks`,
  `-fno-tree-vectorize`, `-fno-inline`, `-fPIE`, `-fpie`, `-fno-pie`
- Warning modes: `-Wshadow`, `-Wundef`, `-Wformat=2`,
  `-Wstrict-prototypes`, `-Wmissing-prototypes`

When this corpus becomes stable enough for daily use, promote a representative
subset into tier1 driver regression.
