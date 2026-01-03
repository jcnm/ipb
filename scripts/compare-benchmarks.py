#!/usr/bin/env python3
"""
Compare benchmark results between baseline and current run.
Generates a markdown report with regression detection.
"""

import argparse
import json
import sys
from pathlib import Path
from typing import Dict, List, Any, Optional


def load_results(path: str) -> Dict[str, Any]:
    """Load benchmark results from JSON file."""
    with open(path, 'r') as f:
        return json.load(f)


def calculate_change(baseline: float, current: float) -> float:
    """Calculate percentage change (positive = slower/worse)."""
    if baseline == 0:
        return 0
    return ((current - baseline) / baseline) * 100


def format_duration(ns: float) -> str:
    """Format nanoseconds to human readable."""
    if ns >= 1e9:
        return f"{ns/1e9:.2f}s"
    elif ns >= 1e6:
        return f"{ns/1e6:.2f}ms"
    elif ns >= 1e3:
        return f"{ns/1e3:.2f}µs"
    else:
        return f"{ns:.0f}ns"


def format_throughput(ops: float) -> str:
    """Format operations per second."""
    if ops >= 1e9:
        return f"{ops/1e9:.2f}G ops/s"
    elif ops >= 1e6:
        return f"{ops/1e6:.2f}M ops/s"
    elif ops >= 1e3:
        return f"{ops/1e3:.2f}K ops/s"
    else:
        return f"{ops:.0f} ops/s"


def compare_benchmarks(
    baseline: Dict[str, Any],
    current: Dict[str, Any],
    threshold: float = 10.0
) -> List[Dict[str, Any]]:
    """Compare benchmark results and detect regressions."""

    comparisons = []

    baseline_benchmarks = {b['name']: b for b in baseline.get('benchmarks', [])}
    current_benchmarks = {b['name']: b for b in current.get('benchmarks', [])}

    all_names = set(baseline_benchmarks.keys()) | set(current_benchmarks.keys())

    for name in sorted(all_names):
        base = baseline_benchmarks.get(name)
        curr = current_benchmarks.get(name)

        comparison = {
            'name': name,
            'status': 'unchanged',
            'baseline': None,
            'current': None,
            'change_percent': 0,
        }

        if base is None:
            comparison['status'] = 'new'
            comparison['current'] = curr
        elif curr is None:
            comparison['status'] = 'removed'
            comparison['baseline'] = base
        else:
            # Compare mean execution time
            base_mean = base.get('mean_ns', 0)
            curr_mean = curr.get('mean_ns', 0)

            change = calculate_change(base_mean, curr_mean)
            comparison['baseline'] = base
            comparison['current'] = curr
            comparison['change_percent'] = change

            if change > threshold:
                comparison['status'] = 'regression'
            elif change < -threshold:
                comparison['status'] = 'improvement'
            else:
                comparison['status'] = 'unchanged'

        comparisons.append(comparison)

    return comparisons


def generate_markdown(
    comparisons: List[Dict[str, Any]],
    baseline_info: Dict[str, Any],
    current_info: Dict[str, Any]
) -> str:
    """Generate markdown report."""

    lines = []

    # Summary
    regressions = [c for c in comparisons if c['status'] == 'regression']
    improvements = [c for c in comparisons if c['status'] == 'improvement']
    unchanged = [c for c in comparisons if c['status'] == 'unchanged']
    new_benchmarks = [c for c in comparisons if c['status'] == 'new']

    lines.append("### Summary")
    lines.append("")

    if regressions:
        lines.append(f"🔴 **{len(regressions)} regressions detected**")
    if improvements:
        lines.append(f"🟢 **{len(improvements)} improvements**")
    lines.append(f"⚪ {len(unchanged)} unchanged")
    if new_benchmarks:
        lines.append(f"🆕 {len(new_benchmarks)} new benchmarks")
    lines.append("")

    # Detailed table
    lines.append("### Details")
    lines.append("")
    lines.append("| Benchmark | Baseline | Current | Change |")
    lines.append("|-----------|----------|---------|--------|")

    for c in comparisons:
        name = c['name']

        if c['status'] == 'new':
            curr_mean = c['current'].get('mean_ns', 0)
            lines.append(f"| {name} | - | {format_duration(curr_mean)} | 🆕 NEW |")
        elif c['status'] == 'removed':
            base_mean = c['baseline'].get('mean_ns', 0)
            lines.append(f"| {name} | {format_duration(base_mean)} | - | ❌ REMOVED |")
        else:
            base_mean = c['baseline'].get('mean_ns', 0)
            curr_mean = c['current'].get('mean_ns', 0)
            change = c['change_percent']

            if c['status'] == 'regression':
                icon = "🔴 REGRESSION"
                change_str = f"+{change:.1f}%"
            elif c['status'] == 'improvement':
                icon = "🟢"
                change_str = f"{change:.1f}%"
            else:
                icon = ""
                change_str = f"{change:+.1f}%"

            lines.append(
                f"| {name} | {format_duration(base_mean)} | "
                f"{format_duration(curr_mean)} | {change_str} {icon} |"
            )

    lines.append("")

    # Metadata
    lines.append("<details>")
    lines.append("<summary>Run Information</summary>")
    lines.append("")
    lines.append(f"- Baseline: {baseline_info.get('version', 'unknown')} @ {baseline_info.get('timestamp', 'unknown')}")
    lines.append(f"- Current: {current_info.get('version', 'unknown')} @ {current_info.get('timestamp', 'unknown')}")
    lines.append("")
    lines.append("</details>")

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description='Compare benchmark results')
    parser.add_argument('--baseline', required=True, help='Baseline results JSON')
    parser.add_argument('--current', required=True, help='Current results JSON')
    parser.add_argument('--output', default='comparison.md', help='Output markdown file')
    parser.add_argument('--threshold', type=float, default=10.0,
                        help='Regression threshold percentage (default: 10)')
    parser.add_argument('--fail-on-regression', action='store_true',
                        help='Exit with error code if regression detected')

    args = parser.parse_args()

    baseline = load_results(args.baseline)
    current = load_results(args.current)

    comparisons = compare_benchmarks(baseline, current, args.threshold)

    report = generate_markdown(comparisons, baseline, current)

    with open(args.output, 'w') as f:
        f.write(report)

    print(f"Comparison report written to {args.output}")

    # Check for regressions
    regressions = [c for c in comparisons if c['status'] == 'regression']
    if regressions and args.fail_on_regression:
        print(f"\n⚠️  {len(regressions)} regressions detected!")
        for r in regressions:
            print(f"  - {r['name']}: +{r['change_percent']:.1f}%")
        sys.exit(1)


if __name__ == '__main__':
    main()
