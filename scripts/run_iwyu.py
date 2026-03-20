#!/usr/bin/env python3

import json
import os
import shlex
import subprocess
import sys
from pathlib import Path


def load_compile_commands(path: Path):
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def get_arguments(entry):
    if "arguments" in entry:
        return list(entry["arguments"])
    return shlex.split(entry["command"])


def is_generated_source(file_path: Path) -> bool:
    return file_path.name in {"parser_generated.cpp", "lexer_scanner.cpp"}


def build_iwyu_command(entry):
    arguments = get_arguments(entry)
    arguments[0] = "include-what-you-use"
    return arguments


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: run_iwyu.py <compile_commands.json>", file=sys.stderr)
        return 1

    compile_commands_path = Path(sys.argv[1]).resolve()
    if not compile_commands_path.is_file():
        print(f"missing compile commands: {compile_commands_path}", file=sys.stderr)
        return 1

    compile_commands = load_compile_commands(compile_commands_path)
    project_root = compile_commands_path.parent.parent.resolve()
    failures = 0

    for entry in compile_commands:
        file_path = Path(entry["file"]).resolve()
        if file_path.suffix != ".cpp":
            continue
        if is_generated_source(file_path):
            continue
        if project_root not in file_path.parents:
            continue

        command = build_iwyu_command(entry)
        print(f"[iwyu] {file_path}")
        result = subprocess.run(command, cwd=entry["directory"], check=False)
        if result.returncode != 0:
            failures += 1

    return 1 if failures > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
