# North-star Real-project Compatibility Corpus

This manual corpus contains small real-project shaped C programs that a host
GCC/Clang-style compiler can compile and run. SysyCC is expected to compile,
link, and run these cases successfully as part of the "can act as CC for real
projects" track.

Run it manually with:

```bash
tests/manual/north_star_gcc_only_gap_corpus/verify.sh
```

The cases are not part of tier1/tier2 regression yet. They are kept as a manual
north-star probe for GNU/C compatibility features that appear in small real C
projects, including GNU case ranges, statement expressions, `typeof`, compound
literals, designated initializers, local array initializers, VLA syntax, and
C11 `_Generic`.
