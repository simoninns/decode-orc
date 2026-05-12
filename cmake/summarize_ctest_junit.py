#!/usr/bin/env python3
"""Summarize CTest JUnit XML results for CI timing baselines.

Outputs a Markdown report with:
- Totals (tests, failures, skipped, pass rate)
- Duration stats (wall, sum, p50/p95/p99)
- Time by CMake label
- Top slow tests
- Suite fan-out by test-name prefix
"""

from __future__ import annotations

import argparse
import statistics
import sys
import xml.etree.ElementTree as ET
from collections import Counter, defaultdict
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Summarize CTest JUnit XML results")
    parser.add_argument("--input", required=True, help="Path to ctest --output-junit XML")
    parser.add_argument("--output", required=True, help="Path to output Markdown report")
    parser.add_argument("--top", type=int, default=20, help="Number of slow tests to include")
    return parser.parse_args()


def to_float(value: str | None, default: float = 0.0) -> float:
    if value is None:
        return default
    try:
        return float(value)
    except ValueError:
        return default


def percentile(sorted_values: list[float], pct: float) -> float:
    if not sorted_values:
        return 0.0
    if len(sorted_values) == 1:
        return sorted_values[0]
    index = (len(sorted_values) - 1) * pct
    low = int(index)
    high = min(low + 1, len(sorted_values) - 1)
    frac = index - low
    return sorted_values[low] + (sorted_values[high] - sorted_values[low]) * frac


def suite_name(test_name: str) -> str:
    # Prefer the fixture/suite prefix before the first '.'
    if "." in test_name:
        return test_name.split(".", 1)[0]
    # Parameterized tests often include '/' separators
    if "/" in test_name:
        return test_name.split("/", 1)[0]
    return test_name


def main() -> int:
    args = parse_args()
    input_path = Path(args.input)
    output_path = Path(args.output)

    if not input_path.exists():
        print(f"Input file not found: {input_path}", file=sys.stderr)
        return 2

    root = ET.parse(input_path).getroot()

    tests = int(root.attrib.get("tests", "0"))
    failures = int(root.attrib.get("failures", "0"))
    skipped_suite_attr = int(root.attrib.get("skipped", root.attrib.get("disabled", "0")))
    wall_time = to_float(root.attrib.get("time", "0"))

    testcase_rows: list[tuple[str, float, str]] = []
    executed_rows: list[tuple[str, float, str]] = []
    durations: list[float] = []
    suite_counts: Counter[str] = Counter()
    label_time: defaultdict[str, float] = defaultdict(float)
    label_count: Counter[str] = Counter()

    skipped_count = 0
    run_count = 0

    for tc in root.findall("testcase"):
        name = tc.attrib.get("name", "(unnamed)")
        duration = to_float(tc.attrib.get("time", "0"))
        status = tc.attrib.get("status", "run")

        was_skipped = tc.find("skipped") is not None or status == "notrun"
        if was_skipped:
            skipped_count += 1
        else:
            run_count += 1
            durations.append(duration)
            executed_rows.append((name, duration, "run"))

        testcase_rows.append((name, duration, "skipped" if was_skipped else "run"))
        suite_counts[suite_name(name)] += 1

        prop_parent = tc.find("properties")
        labels_value = ""
        if prop_parent is not None:
            for prop in prop_parent.findall("property"):
                if prop.attrib.get("name") == "cmake_labels":
                    labels_value = prop.attrib.get("value", "")
                    break

        labels = [l for l in labels_value.split(";") if l]
        if not labels:
            labels = ["(unlabeled)"]

        for label in labels:
            label_time[label] += duration
            label_count[label] += 1

    effective_skipped = skipped_count if skipped_count > 0 else skipped_suite_attr
    passed = max(run_count - failures, 0)
    pass_rate = (passed / run_count * 100.0) if run_count else 0.0

    durations_sorted = sorted(durations)
    p50 = percentile(durations_sorted, 0.50)
    p95 = percentile(durations_sorted, 0.95)
    p99 = percentile(durations_sorted, 0.99)
    mean = statistics.fmean(durations_sorted) if durations_sorted else 0.0
    sum_time = sum(durations_sorted)

    slow_rows = sorted(executed_rows, key=lambda row: row[1], reverse=True)[: args.top]
    suite_rows = suite_counts.most_common(20)
    label_rows = sorted(label_time.items(), key=lambda item: item[1], reverse=True)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", encoding="utf-8") as out:
        out.write("# CTest JUnit Summary\n\n")
        out.write(f"Source XML: `{input_path}`\n\n")

        out.write("## Totals\n\n")
        out.write(f"- Declared tests: {tests}\n")
        out.write(f"- Executed tests: {run_count}\n")
        out.write(f"- Passed tests: {passed}\n")
        out.write(f"- Failed tests: {failures}\n")
        out.write(f"- Skipped tests: {effective_skipped}\n")
        out.write(f"- Pass rate (executed): {pass_rate:.2f}%\n")
        out.write(f"- Wall clock (suite attr): {wall_time:.3f}s\n")

        out.write("\n## Timing Stats (Executed Tests)\n\n")
        out.write(f"- Sum of test durations: {sum_time:.3f}s\n")
        out.write(f"- Mean: {mean:.4f}s\n")
        out.write(f"- p50: {p50:.4f}s\n")
        out.write(f"- p95: {p95:.4f}s\n")
        out.write(f"- p99: {p99:.4f}s\n")

        out.write("\n## Label Time Totals\n\n")
        out.write("| Label | Tests | Total Time (s) |\n")
        out.write("|---|---:|---:|\n")
        for label, total in label_rows:
            out.write(f"| {label} | {label_count[label]} | {total:.3f} |\n")

        out.write("\n## Top Slow Tests\n\n")
        out.write("| Rank | Test | Time (s) | Status |\n")
        out.write("|---:|---|---:|---|\n")
        for i, (name, duration, status) in enumerate(slow_rows, start=1):
            out.write(f"| {i} | {name} | {duration:.4f} | {status} |\n")

        out.write("\n## Largest Suite Prefixes\n\n")
        out.write("| Suite Prefix | Test Count |\n")
        out.write("|---|---:|\n")
        for suite, count in suite_rows:
            out.write(f"| {suite} | {count} |\n")

    print(f"Wrote summary: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
