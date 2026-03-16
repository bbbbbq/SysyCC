# Tests Module

## Scope

The tests module stores SysY22 input files and helper scripts used for quick
local verification.

## Main Files

- [array_init.sy](/Users/caojunze424/code/SysyCC/tests/array_init.sy)
- [array_init.sh](/Users/caojunze424/code/SysyCC/tests/array_init.sh)
- [control_flow.sy](/Users/caojunze424/code/SysyCC/tests/control_flow.sy)
- [control_flow.sh](/Users/caojunze424/code/SysyCC/tests/control_flow.sh)
- [function_call.sy](/Users/caojunze424/code/SysyCC/tests/function_call.sy)
- [function_call.sh](/Users/caojunze424/code/SysyCC/tests/function_call.sh)
- [literal_formats.sy](/Users/caojunze424/code/SysyCC/tests/literal_formats.sy)
- [literal_formats.sh](/Users/caojunze424/code/SysyCC/tests/literal_formats.sh)
- [minimal.sy](/Users/caojunze424/code/SysyCC/tests/minimal.sy)
- [minimal.sh](/Users/caojunze424/code/SysyCC/tests/minimal.sh)
- [preprocess_define.sy](/Users/caojunze424/code/SysyCC/tests/preprocess_define.sy)
- [preprocess_define.sh](/Users/caojunze424/code/SysyCC/tests/preprocess_define.sh)
- [preprocess_undef.sy](/Users/caojunze424/code/SysyCC/tests/preprocess_undef.sy)
- [preprocess_undef.sh](/Users/caojunze424/code/SysyCC/tests/preprocess_undef.sh)
- [run_all.sh](/Users/caojunze424/code/SysyCC/tests/run_all.sh)

## Responsibilities

- provide minimal valid SysY22 source examples
- cover functions, control flow, arrays, literal formats, and preprocessing
- support one-click local runs
- serve as smoke tests during development

## Current Test Flow

Each `*.sh` file will:

- configure the project
- build the project
- run `SysyCC` on its matching `*.sy` file

`run_all.sh` will execute every test script in the directory except itself.

It also validates that each test produced:

- `build/intermediate_results/<test_name>.preprocessed.sy`
- `build/intermediate_results/<test_name>.tokens.txt`
- `build/intermediate_results/<test_name>.parse.txt`
