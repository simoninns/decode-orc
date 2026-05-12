#!/usr/bin/env python3
"""Check CTest JUnit metrics against a simple performance budget.

Exits non-zero when a budget threshold is violated.
"""

from __future__ import annotations

import argparse
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Validate CTest JUnit performance budget")
    parser.add_argument("--input", required=True, help="Path to ctest --output-junit XML")
    parser.add_argument("--max-wall-seconds", type=float, required=True, help="Maximum allowed XML wall time")
    parser.add_argument("--min-pass-rate", type=float, required=True, help="Minimum allowed pass rate percentage")
    parser.add_argument("--max-p95-seconds", type=float, required=True, help="Maximum allowed p95 test duration")
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


def main() -> int:
    args = parse_args()
    input_path = Path(args.input)
    if not input_path.exists():
        print(f"[budget] junit file missing: {input_path}", file=sys.stderr)
        return 2

    root = ET.parse(input_path).getroot()

    failures = int(root.attrib.get("failures", "0"))
    wall_time = to_float(root.attrib.get("time", "0"))

    durations: list[float] = []
    run_count = 0
    skipped_count = 0

    for tc in root.findall("testcase"):
        status = tc.attrib.get("status", "run")
        was_skipped = tc.find("skipped") is not None or status == "notrun"
        if was_skipped:
            skipped_count += 1
            continue
        run_count += 1
        durations.append(to_float(tc.attrib.get("time", "0")))

    passed = max(run_count - failures, 0)
    pass_rate = (passed / run_count * 100.0) if run_count else 0.0
    p95 = percentile(sorted(durations), 0.95)

    print(f"[budget] file={input_path}")
    print(f"[budget] wall={wall_time:.3f}s (max {args.max_wall_seconds:.3f}s)")
    print(f"[budget] pass_rate={pass_rate:.2f}% (min {args.min_pass_rate:.2f}%)")
    print(f"[budget] p95={p95:.4f}s (max {args.max_p95_seconds:.4f}s)")
    print(f"[budget] executed={run_count} skipped={skipped_count} failures={failures}")

    violations: list[str] = []
    if wall_time > args.max_wall_seconds:
        violations.append(
            f"wall time {wall_time:.3f}s exceeds budget {args.max_wall_seconds:.3f}s"
        )
    if pass_rate < args.min_pass_rate:
        violations.append(
            f"pass rate {pass_rate:.2f}% below budget {args.min_pass_rate:.2f}%"
        )
    if p95 > args.max_p95_seconds:
        violations.append(
            f"p95 {p95:.4f}s exceeds budget {args.max_p95_seconds:.4f}s"
        )

    if violations:
        print("[budget] FAIL", file=sys.stderr)
        for violation in violations:
            print(f"[budget] - {violation}", file=sys.stderr)
        return 1

    print("[budget] PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
