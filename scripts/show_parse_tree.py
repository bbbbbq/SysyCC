#!/usr/bin/env python3

from __future__ import annotations

import argparse
import html
import json
from dataclasses import dataclass, field
from pathlib import Path
import subprocess
import sys


@dataclass
class TreeNode:
    label: str
    children: list["TreeNode"] = field(default_factory=list)


@dataclass
class PositionedNode:
    node: TreeNode
    x: float
    y: float
    children: list["PositionedNode"] = field(default_factory=list)


def parse_parse_dump(path: Path) -> tuple[dict[str, str], TreeNode | None]:
    lines = path.read_text(encoding="utf-8").splitlines()

    metadata: dict[str, str] = {}
    tree_start = None
    for index, line in enumerate(lines):
        if line == "parse_tree:":
            tree_start = index + 1
            break

        if ": " in line:
            key, value = line.split(": ", 1)
            metadata[key] = value

    if tree_start is None:
        raise ValueError("missing 'parse_tree:' section")

    root: TreeNode | None = None
    stack: list[tuple[int, TreeNode]] = []

    for raw_line in lines[tree_start:]:
        if not raw_line.strip():
            continue

        indent = len(raw_line) - len(raw_line.lstrip(" "))
        depth = indent // 2
        node = TreeNode(raw_line.strip())

        while stack and stack[-1][0] >= depth:
            stack.pop()

        if not stack:
            root = node
        else:
            stack[-1][1].children.append(node)

        stack.append((depth, node))

    return metadata, root


def render_text_tree(
    node: TreeNode, prefix: str = "", is_last: bool = True
) -> list[str]:
    branch = "`-- " if is_last else "|-- "
    lines = [f"{prefix}{branch}{node.label}"]

    child_prefix = f"{prefix}{'    ' if is_last else '|   '}"
    for index, child in enumerate(node.children):
        child_is_last = index == len(node.children) - 1
        lines.extend(render_text_tree(child, child_prefix, child_is_last))

    return lines


def count_leaves(node: TreeNode) -> int:
    if not node.children:
        return 1
    return sum(count_leaves(child) for child in node.children)


def compute_depth(node: TreeNode) -> int:
    if not node.children:
        return 1
    return 1 + max(compute_depth(child) for child in node.children)


def estimate_label_width(label: str) -> int:
    return max(72, len(label) * 8 + 28)


def compute_subtree_widths(
    node: TreeNode, widths: dict[int, int], sibling_gap: int
) -> int:
    label_width = estimate_label_width(node.label)
    if not node.children:
        widths[id(node)] = label_width
        return label_width

    child_widths = [
        compute_subtree_widths(child, widths, sibling_gap) for child in node.children
    ]
    children_total_width = sum(child_widths) + sibling_gap * (len(child_widths) - 1)
    subtree_width = max(label_width, children_total_width)
    widths[id(node)] = subtree_width
    return subtree_width


def layout_tree(
    node: TreeNode,
    subtree_widths: dict[int, int],
    left: float,
    depth: int,
    sibling_gap: int,
    vertical_gap: int,
) -> PositionedNode:
    y = depth * vertical_gap
    subtree_width = subtree_widths[id(node)]
    x = left + subtree_width / 2.0

    if not node.children:
        return PositionedNode(node=node, x=x, y=y)

    children_total_width = (
        sum(subtree_widths[id(child)] for child in node.children)
        + sibling_gap * (len(node.children) - 1)
    )
    child_left = left + (subtree_width - children_total_width) / 2.0
    positioned_children: list[PositionedNode] = []
    for child in node.children:
        positioned_child = layout_tree(
            child,
            subtree_widths,
            child_left,
            depth + 1,
            sibling_gap,
            vertical_gap,
        )
        positioned_children.append(positioned_child)
        child_left += subtree_widths[id(child)] + sibling_gap

    return PositionedNode(node=node, x=x, y=y, children=positioned_children)


def build_svg(positioned_root: PositionedNode, width: int, height: int) -> str:
    edges: list[str] = []
    nodes: list[str] = []

    def walk(current: PositionedNode) -> None:
        for child in current.children:
            edges.append(
                f'<line class="edge" x1="{current.x:.2f}" y1="{current.y:.2f}" '
                f'x2="{child.x:.2f}" y2="{child.y:.2f}" />'
            )
            walk(child)

        label = html.escape(current.node.label)
        nodes.append(
            f'<g transform="translate({current.x:.2f},{current.y:.2f})">'
            '<circle class="node-circle" r="16" />'
            f'<text class="node-label" x="0" y="-26" text-anchor="middle">{label}</text>'
            "</g>"
        )

    walk(positioned_root)

    return (
        f'<svg class="tree-svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" '
        'xmlns="http://www.w3.org/2000/svg">'
        + "".join(edges)
        + "".join(nodes)
        + "</svg>"
    )


def build_html(metadata: dict[str, str], root: TreeNode | None, input_path: Path) -> str:
    metadata_rows: list[str] = []
    for key in ("input_file", "parse_success", "parse_message"):
        if key in metadata:
            metadata_rows.append(
                "<tr>"
                f"<th>{html.escape(key)}</th>"
                f"<td>{html.escape(metadata[key])}</td>"
                "</tr>"
            )

    if root is None:
        graph_html = '<p class="empty">parse_tree is empty.</p>'
        graph_meta = {"depth": 0, "leaf_count": 0, "width": 0, "height": 0}
    else:
        sibling_gap = 28
        vertical_gap = 108
        leaf_count = count_leaves(root)
        depth = compute_depth(root)
        subtree_widths: dict[int, int] = {}
        root_width = compute_subtree_widths(root, subtree_widths, sibling_gap)
        width = max(1200, root_width + 160)
        height = max(680, depth * vertical_gap + 160)
        positioned_root = layout_tree(root, subtree_widths, 80, 1, sibling_gap, vertical_gap)
        graph_html = build_svg(positioned_root, width, height)
        graph_meta = {
            "depth": depth,
            "leaf_count": leaf_count,
            "width": width,
            "height": height,
        }

    title = f"Parse Tree - {input_path.name}"
    graph_meta_json = json.dumps(graph_meta)
    metadata_html = "".join(metadata_rows)

    return f"""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{html.escape(title)}</title>
    <style>
        :root {{
            color-scheme: light;
            --bg: #f6f0e4;
            --panel: #fffdf8;
            --line: #ceb998;
            --node: #ffffff;
            --text: #2c241b;
            --muted: #6f604f;
            --accent: #7f5d2b;
        }}

        * {{
            box-sizing: border-box;
        }}

        body {{
            margin: 0;
            padding: 28px;
            color: var(--text);
            font-family: Menlo, Monaco, "Courier New", monospace;
            background:
                radial-gradient(circle at top left, #fff8e6 0%, transparent 28%),
                linear-gradient(180deg, #f7f0e3 0%, #f2eadb 100%);
        }}

        .layout {{
            max-width: 1360px;
            margin: 0 auto;
            display: grid;
            gap: 20px;
        }}

        .panel {{
            background: var(--panel);
            border: 1px solid #ddceb6;
            border-radius: 18px;
            padding: 20px 24px;
            box-shadow: 0 14px 34px rgba(83, 61, 33, 0.08);
        }}

        h1 {{
            margin: 0 0 8px;
            font-size: 26px;
        }}

        p {{
            margin: 0;
            color: var(--muted);
        }}

        table {{
            width: 100%;
            border-collapse: collapse;
            margin-top: 14px;
        }}

        th,
        td {{
            border-top: 1px solid #eadfcd;
            padding: 10px 12px;
            text-align: left;
            vertical-align: top;
        }}

        th {{
            width: 180px;
            color: var(--accent);
        }}

        .toolbar {{
            display: flex;
            gap: 10px;
            margin-bottom: 14px;
        }}

        .toolbar button {{
            border: 1px solid #d5c09c;
            border-radius: 999px;
            background: #fff1d5;
            padding: 8px 14px;
            cursor: pointer;
            font-family: inherit;
            color: var(--text);
        }}

        .graph-meta {{
            color: var(--muted);
            margin-bottom: 10px;
        }}

        .viewport {{
            overflow: auto;
            border: 1px solid #e2d4bc;
            border-radius: 16px;
            padding: 16px;
            min-height: 540px;
            background:
                radial-gradient(circle at top center, #fffaf0 0%, transparent 26%),
                linear-gradient(180deg, #fffdf8 0%, #fbf5e9 100%);
        }}

        #graph-stage {{
            width: max-content;
            transform-origin: top left;
        }}

        .tree-svg {{
            display: block;
        }}

        .edge {{
            stroke: #b89a6f;
            stroke-width: 2.4;
        }}

        .node-circle {{
            fill: var(--node);
            stroke: #6f532b;
            stroke-width: 2.4;
        }}

        .node-label {{
            fill: var(--text);
            font-family: Menlo, Monaco, "Courier New", monospace;
            font-size: 12px;
        }}

        .empty {{
            color: var(--muted);
            font-style: italic;
        }}
    </style>
</head>
<body>
    <main class="layout">
        <section class="panel">
            <h1>{html.escape(title)}</h1>
            <p>Graph view of the SysyCC parse tree.</p>
            <table>{metadata_html}</table>
        </section>
        <section class="panel">
            <h1>Tree Graph</h1>
            <div class="toolbar">
                <button type="button" onclick="zoomOut()">Zoom Out</button>
                <button type="button" onclick="resetZoom()">Reset</button>
                <button type="button" onclick="zoomIn()">Zoom In</button>
            </div>
            <div class="graph-meta" id="graph-meta"></div>
            <div class="viewport">
                <div id="graph-stage">{graph_html}</div>
            </div>
        </section>
    </main>
    <script>
        const graphMeta = {graph_meta_json};
        const graphMetaElement = document.getElementById("graph-meta");
        const graphStage = document.getElementById("graph-stage");
        let currentScale = 1.0;

        if (graphMetaElement) {{
            graphMetaElement.textContent =
                "depth: " + graphMeta.depth +
                ", leaves: " + graphMeta.leaf_count +
                ", canvas: " + graphMeta.width + "x" + graphMeta.height;
        }}

        function applyScale() {{
            if (!graphStage) {{
                return;
            }}
            graphStage.style.transform = "scale(" + currentScale + ")";
        }}

        function zoomIn() {{
            currentScale = Math.min(2.5, currentScale * 1.12);
            applyScale();
        }}

        function zoomOut() {{
            currentScale = Math.max(0.35, currentScale / 1.12);
            applyScale();
        }}

        function resetZoom() {{
            currentScale = 1.0;
            applyScale();
        }}

        applyScale();
    </script>
</body>
</html>
"""


def write_html_file(
    metadata: dict[str, str], root: TreeNode | None, input_path: Path, output_path: Path
) -> Path:
    output_path.write_text(build_html(metadata, root, input_path), encoding="utf-8")
    return output_path


def open_file_in_browser(path: Path) -> None:
    if sys.platform == "darwin":
        subprocess.run(["open", str(path)], check=True)
        return

    if sys.platform.startswith("linux"):
        subprocess.run(["xdg-open", str(path)], check=True)
        return

    if sys.platform == "win32":
        subprocess.run(["cmd", "/c", "start", "", str(path)], check=True)
        return

    raise RuntimeError(f"unsupported platform: {sys.platform}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Render a SysyCC parse dump as text or as a graph page."
    )
    parser.add_argument("input", help="Path to a .parse.txt file")
    parser.add_argument(
        "--html",
        action="store_true",
        help="Generate an HTML graph page next to the input file",
    )
    parser.add_argument(
        "--open",
        action="store_true",
        help="Generate the HTML graph page and open it in the default browser",
    )
    parser.add_argument(
        "--output",
        help="Custom HTML output path. Defaults to <input basename>.html",
    )
    args = parser.parse_args()

    input_path = Path(args.input)
    metadata, root = parse_parse_dump(input_path)

    if args.html or args.open:
        output_path = (
            Path(args.output)
            if args.output
            else input_path.with_suffix("").with_suffix(".html")
        )
        write_html_file(metadata, root, input_path, output_path)
        print(f"HTML written to: {output_path}")

        if args.open:
            open_file_in_browser(output_path.resolve())
        return 0

    print(f"Parse Dump: {input_path}")
    for key in ("input_file", "parse_success", "parse_message"):
        if key in metadata:
            print(f"{key}: {metadata[key]}")

    if root is None:
        print("parse_tree: <empty>")
        return 0

    print("parse_tree:")
    print(root.label)
    for index, child in enumerate(root.children):
        is_last = index == len(root.children) - 1
        for line in render_text_tree(child, "", is_last):
            print(line)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
