#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${SYSYCC_BUILD_DIR:-${PROJECT_ROOT}/build}"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
INPUT_FILE="${SCRIPT_DIR}/${TEST_NAME}.sy"
IR_FILE="${PROJECT_ROOT}/build/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-ir >/dev/null

assert_file_nonempty "${IR_FILE}"
python3 - "${IR_FILE}" <<'PY'
import re
import sys

path = sys.argv[1]
label_re = re.compile(r"^([A-Za-z$._][-A-Za-z$._0-9]*):")
define_re = re.compile(r"^define\b")
br_uncond = re.compile(r"\bbr\s+label\s+%([A-Za-z$._][-A-Za-z$._0-9]*)")
br_cond = re.compile(r"\bbr\s+i1\s+[^,]+,\s+label\s+%([A-Za-z$._][-A-Za-z$._0-9]*),\s+label\s+%([A-Za-z$._][-A-Za-z$._0-9]*)")
phi_in = re.compile(r"\[\s*.*?,\s+%([A-Za-z$._][-A-Za-z$._0-9]*)\s*\]")

def verify_function(blocks):
    preds = {name: set() for name in blocks}
    for name, info in blocks.items():
        term = info["term"]
        match = br_cond.search(term)
        if match:
            successors = [match.group(1), match.group(2)]
        else:
            match = br_uncond.search(term)
            successors = [match.group(1)] if match else []
        for successor in successors:
            if successor in preds:
                preds[successor].add(name)

    for name, info in blocks.items():
        for phi in info["phis"]:
            incoming = set(phi_in.findall(phi))
            if incoming != preds[name]:
                raise SystemExit(
                    f"phi predecessor mismatch in {name}: incoming={sorted(incoming)} preds={sorted(preds[name])}"
                )

blocks = None
block = None
for line in open(path, encoding="utf-8"):
    if blocks is None:
        if define_re.match(line):
            blocks = {"entry": {"phis": [], "term": ""}}
            block = "entry"
        continue
    if line.startswith("}"):
        verify_function(blocks)
        blocks = None
        block = None
        continue
    match = label_re.match(line)
    if match:
        block = match.group(1)
        blocks.setdefault(block, {"phis": [], "term": ""})
        continue
    text = line.strip()
    if " phi " in line or text.startswith("phi "):
        blocks[block]["phis"].append(text)
    if text.startswith(("br ", "ret ", "switch ", "unreachable")):
        blocks[block]["term"] = text
PY

echo "verified: va_arg lowering exposes helper tail labels to successor phis"
