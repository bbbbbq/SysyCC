# Scripts Module

## Scope

The scripts module contains developer helper tools outside the compiler binary.

## Main Files

- [show_parse_tree.py](/Users/caojunze424/code/SysyCC/scripts/show_parse_tree.py)

## Responsibilities

- read parse dump files
- render parse trees in terminal form
- generate local HTML graph pages
- open the generated page in the default browser

## Example

```bash
python3 scripts/show_parse_tree.py build/intermediate_results/minimal.parse.txt --open
```

