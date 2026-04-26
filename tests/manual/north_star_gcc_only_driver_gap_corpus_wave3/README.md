# North-star Driver Compatibility Corpus Wave 3

This manual corpus contains 20 additional small two-source C project shapes that
a host GCC/Clang-style compiler can compile and run. SysyCC is expected to
compile, link, and run these cases successfully as part of the "can act as CC
for real projects" track.

Run it manually with:

```bash
tests/manual/north_star_gcc_only_driver_gap_corpus_wave3/verify.sh
```

The shared fixture is intentionally C90-compatible so `-ansi` and
`-pedantic-errors` test driver compatibility rather than source-language
violations. The expected runtime result for every case is exit status `0`, which
means `project_score(8) == 33`.

Current compatibility groups:

- ISO standard aliases and newer standards: `-std=iso9899:1999`,
  `-std=iso9899:2011`, `-std=iso9899:2017`, `-std=iso9899:2018`,
  `-std=c23`, `-std=gnu23`
- Strictness modes: `-pedantic`, `-pedantic-errors`, `-ansi`
- Warning flags: `-Wsystem-headers`, `-Wextra-semi`, `-Wformat`,
  `-Wformat-security`, `-Werror=format`, `-Wstrict-overflow`, `-Wlogical-op`,
  `-Walloca`, `-Warray-bounds`
- Code-generation / target toggles: `-fno-lto`, `-mno-red-zone`

When this corpus becomes stable enough for daily use, promote a representative
subset into tier1 driver regression.
