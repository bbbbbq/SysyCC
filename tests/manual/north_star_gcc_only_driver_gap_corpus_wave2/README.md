# North-star Driver Compatibility Corpus Wave 2

This manual corpus contains another 20 small two-source C project shapes that a
host GCC/Clang-style compiler can compile and run. SysyCC is expected to compile,
link, and run these cases successfully as part of the "can act as CC for real
projects" track.

Run it manually with:

```bash
tests/manual/north_star_gcc_only_driver_gap_corpus_wave2/verify.sh
```

The fixture is intentionally simple so every case is attributable to driver
compatibility rather than C semantics. The expected runtime result for every
case is exit status `0`, which means `project_score(7) == 64`.

Current compatibility groups:

- Newer C standard spellings: `-std=c17`, `-std=c18`, `-std=gnu18`,
  `-std=c2x`, `-std=gnu2x`
- Optimization spellings: `-Oz`, `-Ofast`
- Hosted/freestanding and target/codegen toggles: `-ffreestanding`,
  `-fhosted`, `-fno-plt`, `-fno-pic`, `-fvisibility=default`,
  `-fno-asynchronous-unwind-tables`, `-funwind-tables`,
  `-fmerge-all-constants`, `-fno-ident`, `-fstrict-aliasing`,
  `-fno-math-errno`
- Warning flags: `-Wcast-align`, `-Wpointer-arith`

When this corpus becomes stable enough for daily use, promote a representative
subset into tier1 driver regression.
