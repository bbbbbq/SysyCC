#!/usr/bin/env python3

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class PassAggregate:
    total_ms: float = 0.0
    runs: int = 0
    changed_runs: int = 0
    blocks_delta: int = 0
    instructions_delta: int = 0


@dataclass
class FixedPointGroup:
    scope: str = ""
    passes: int = 0
    iterations: int = 0
    max_iterations: int = 0
    converged: str = ""
    changed_iterations: str = ""


@dataclass
class TimelineEntry:
    pass_name: str
    elapsed_ms: float
    changed: bool
    blocks_before: int
    blocks_after: int
    instructions_before: int
    instructions_after: int


@dataclass
class PassReport:
    path: Path
    input_file: str = "<unknown>"
    pipeline_ms: float = 0.0
    pass_invocations: int = 0
    passes: dict[str, PassAggregate] = field(default_factory=dict)
    fixed_point_groups: dict[int, FixedPointGroup] = field(default_factory=dict)
    timeline: list[TimelineEntry] = field(default_factory=list)

    @property
    def final_blocks(self) -> int:
        return self.timeline[-1].blocks_after if self.timeline else 0

    @property
    def final_instructions(self) -> int:
        return self.timeline[-1].instructions_after if self.timeline else 0


def parse_int(text: str) -> int:
    return int(text.replace("+", "").strip())


def parse_float(text: str) -> float:
    return float(text.strip())


def parse_size_transition(text: str) -> tuple[int, int]:
    match = re.search(r"(-?\d+)\s*->\s*(-?\d+)", text)
    if not match:
        return 0, 0
    return int(match.group(1)), int(match.group(2))


def split_row(line: str) -> list[str]:
    return [cell.strip() for cell in line.strip().strip("|").split("|")]


def clean_code_cell(text: str) -> str:
    text = text.strip()
    if text.startswith("`") and text.endswith("`"):
        return text[1:-1]
    return text


def parse_report(path: Path) -> PassReport:
    report = PassReport(path=path)
    section = ""

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line:
            continue

        input_match = re.match(r"- Input: `(.+)`", line)
        if input_match:
            report.input_file = input_match.group(1)
            continue

        time_match = re.match(
            r"- (?:Pipeline wall time|Total pass time): ([0-9.]+) ms", line
        )
        if time_match:
            report.pipeline_ms = parse_float(time_match.group(1))
            continue

        invocation_match = re.match(r"- Pass invocations: ([0-9]+)", line)
        if invocation_match:
            report.pass_invocations = int(invocation_match.group(1))
            continue

        if line.startswith("## "):
            section = line[3:].strip()
            continue

        if not line.startswith("|") or line.startswith("| ---"):
            continue

        cells = split_row(line)
        if cells and cells[0] in {"Rank", "Group", "#"}:
            continue

        if section == "Top 10 Slow Passes" and len(cells) >= 7:
            pass_name = clean_code_cell(cells[1])
            report.passes[pass_name] = PassAggregate(
                total_ms=parse_float(cells[2]),
                runs=int(cells[3]),
                changed_runs=int(cells[4]),
                blocks_delta=parse_int(cells[5]),
                instructions_delta=parse_int(cells[6]),
            )
            continue

        if section == "Fixed-Point Groups" and len(cells) >= 7:
            if cells[0] == "-":
                continue
            report.fixed_point_groups[int(cells[0])] = FixedPointGroup(
                scope=cells[1],
                passes=int(cells[2]),
                iterations=int(cells[3]),
                max_iterations=int(cells[4]),
                converged=cells[5],
                changed_iterations=cells[6],
            )
            continue

        if section == "Pass Timeline" and len(cells) >= 8:
            blocks_before, blocks_after = parse_size_transition(cells[6])
            instructions_before, instructions_after = parse_size_transition(cells[7])
            report.timeline.append(
                TimelineEntry(
                    pass_name=clean_code_cell(cells[1]),
                    elapsed_ms=parse_float(cells[3]),
                    changed=cells[4] == "1",
                    blocks_before=blocks_before,
                    blocks_after=blocks_after,
                    instructions_before=instructions_before,
                    instructions_after=instructions_after,
                )
            )

    if report.timeline:
        report.passes = {}
        for entry in report.timeline:
            aggregate = report.passes.setdefault(entry.pass_name, PassAggregate())
            aggregate.total_ms += entry.elapsed_ms
            aggregate.runs += 1
            aggregate.changed_runs += 1 if entry.changed else 0
            aggregate.blocks_delta += entry.blocks_after - entry.blocks_before
            aggregate.instructions_delta += (
                entry.instructions_after - entry.instructions_before
            )

    return report


def percent_delta(before: float, after: float) -> str:
    if before == 0:
        return "n/a"
    return f"{((after - before) / before) * 100:+.2f}%"


def signed_float(value: float) -> str:
    return f"{value:+.3f}"


def signed_int(value: int) -> str:
    return f"{value:+d}"


def render_diff(before: PassReport, after: PassReport, top_n: int) -> str:
    lines: list[str] = []
    lines.append("# SysyCC Pass Report Diff")
    lines.append("")
    lines.append(f"- Before: `{before.path}`")
    lines.append(f"- After: `{after.path}`")
    lines.append(f"- Input before: `{before.input_file}`")
    lines.append(f"- Input after: `{after.input_file}`")
    lines.append("")

    lines.append("## Summary")
    lines.append("")
    lines.append("| Metric | Before | After | Delta | Delta % |")
    lines.append("| --- | ---: | ---: | ---: | ---: |")
    lines.append(
        f"| Pipeline wall ms | {before.pipeline_ms:.3f} | {after.pipeline_ms:.3f} | "
        f"{signed_float(after.pipeline_ms - before.pipeline_ms)} | "
        f"{percent_delta(before.pipeline_ms, after.pipeline_ms)} |"
    )
    lines.append(
        f"| Pass invocations | {before.pass_invocations} | {after.pass_invocations} | "
        f"{signed_int(after.pass_invocations - before.pass_invocations)} | "
        f"{percent_delta(before.pass_invocations, after.pass_invocations)} |"
    )
    lines.append(
        f"| Final basic blocks | {before.final_blocks} | {after.final_blocks} | "
        f"{signed_int(after.final_blocks - before.final_blocks)} | "
        f"{percent_delta(before.final_blocks, after.final_blocks)} |"
    )
    lines.append(
        f"| Final instructions | {before.final_instructions} | {after.final_instructions} | "
        f"{signed_int(after.final_instructions - before.final_instructions)} | "
        f"{percent_delta(before.final_instructions, after.final_instructions)} |"
    )
    lines.append("")

    pass_names = sorted(
        set(before.passes) | set(after.passes),
        key=lambda name: abs(
            after.passes.get(name, PassAggregate()).total_ms
            - before.passes.get(name, PassAggregate()).total_ms
        ),
        reverse=True,
    )
    lines.append(f"## Top {top_n} Pass Time Changes")
    lines.append("")
    lines.append(
        "| Pass | Before ms | After ms | Delta ms | Delta % | Runs before -> after | "
        "Changed before -> after | Blocks delta before -> after | "
        "Instructions delta before -> after |"
    )
    lines.append("| --- | ---: | ---: | ---: | ---: | --- | --- | --- | --- |")
    for pass_name in pass_names[:top_n]:
        before_pass = before.passes.get(pass_name, PassAggregate())
        after_pass = after.passes.get(pass_name, PassAggregate())
        lines.append(
            f"| `{pass_name}` | {before_pass.total_ms:.3f} | "
            f"{after_pass.total_ms:.3f} | "
            f"{signed_float(after_pass.total_ms - before_pass.total_ms)} | "
            f"{percent_delta(before_pass.total_ms, after_pass.total_ms)} | "
            f"{before_pass.runs} -> {after_pass.runs} | "
            f"{before_pass.changed_runs} -> {after_pass.changed_runs} | "
            f"{signed_int(before_pass.blocks_delta)} -> "
            f"{signed_int(after_pass.blocks_delta)} | "
            f"{signed_int(before_pass.instructions_delta)} -> "
            f"{signed_int(after_pass.instructions_delta)} |"
        )
    lines.append("")

    group_ids = sorted(set(before.fixed_point_groups) | set(after.fixed_point_groups))
    lines.append("## Fixed-Point Groups")
    lines.append("")
    lines.append(
        "| Group | Scope | Iterations before -> after | Converged before -> after | "
        "Changed iterations before -> after |"
    )
    lines.append("| ---: | --- | --- | --- | --- |")
    if not group_ids:
        lines.append("| - | - | - | - | - |")
    for group_id in group_ids:
        before_group = before.fixed_point_groups.get(group_id, FixedPointGroup())
        after_group = after.fixed_point_groups.get(group_id, FixedPointGroup())
        scope = after_group.scope or before_group.scope or "-"
        lines.append(
            f"| {group_id} | {scope} | "
            f"{before_group.iterations}/{before_group.max_iterations} -> "
            f"{after_group.iterations}/{after_group.max_iterations} | "
            f"{before_group.converged or '-'} -> {after_group.converged or '-'} | "
            f"{before_group.changed_iterations or '-'} -> "
            f"{after_group.changed_iterations or '-'} |"
        )
    lines.append("")

    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Diff two SysyCC per-TU pass trace Markdown reports."
    )
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("-o", "--output", type=Path)
    parser.add_argument("--top", type=int, default=15)
    args = parser.parse_args()

    before = parse_report(args.before)
    after = parse_report(args.after)
    diff = render_diff(before, after, max(1, args.top))

    if args.output is None:
        print(diff)
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(diff + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
