#!/usr/bin/env python3
"""Analyze STM32 IMU CSV telemetry without treating ACK/BOOT lines as parse failures."""

from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_LOG = PROJECT_ROOT / "docs" / "validation" / "uart_60s_session.txt"


@dataclass
class Analysis:
    sequences: list[int] = field(default_factory=list)
    timestamps: list[int] = field(default_factory=list)
    statuses: Counter[str] = field(default_factory=Counter)
    control_records: Counter[str] = field(default_factory=Counter)
    malformed_imu_lines: list[tuple[int, str]] = field(default_factory=list)
    sequence_gaps: list[tuple[int, int]] = field(default_factory=list)
    duplicate_or_reordered: list[tuple[int, int]] = field(default_factory=list)

    @property
    def valid_records(self) -> int:
        return len(self.sequences)

    @property
    def missing_records(self) -> int:
        return sum(current - previous - 1 for previous, current in self.sequence_gaps)

    @property
    def intervals(self) -> list[int]:
        return [current - previous for previous, current in zip(self.timestamps, self.timestamps[1:])]

    @property
    def observed_rate_hz(self) -> float:
        if len(self.timestamps) < 2:
            return 0.0
        duration_ms = self.timestamps[-1] - self.timestamps[0]
        return ((len(self.timestamps) - 1) * 1000.0 / duration_ms) if duration_ms > 0 else 0.0


def analyze_lines(lines: list[str]) -> Analysis:
    result = Analysis()

    for line_number, raw_line in enumerate(lines, start=1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue

        parts = line.split(",")
        record_type = parts[0]
        if record_type != "IMU":
            result.control_records[record_type] += 1
            continue

        if len(parts) != 12:
            result.malformed_imu_lines.append((line_number, line))
            continue

        try:
            sequence = int(parts[1])
            timestamp = int(parts[2])
            for value in parts[3:11]:
                int(value)
        except ValueError:
            result.malformed_imu_lines.append((line_number, line))
            continue

        result.sequences.append(sequence)
        result.timestamps.append(timestamp)
        result.statuses[parts[11]] += 1

    for previous, current in zip(result.sequences, result.sequences[1:]):
        if current > previous + 1:
            result.sequence_gaps.append((previous, current))
        elif current <= previous:
            result.duplicate_or_reordered.append((previous, current))

    return result


def format_markdown(source: Path, result: Analysis) -> str:
    intervals = result.intervals
    min_interval = min(intervals) if intervals else 0
    max_interval = max(intervals) if intervals else 0
    avg_interval = sum(intervals) / len(intervals) if intervals else 0.0
    continuity = "PASS" if not result.sequence_gaps and not result.duplicate_or_reordered else "REVIEW"
    parsing = "PASS" if not result.malformed_imu_lines else "REVIEW"

    lines = [
        "# UART 60초 Telemetry 분석 요약",
        "",
        f"- Source: `{source}`",
        f"- Valid IMU Records: **{result.valid_records}**",
        f"- First Sequence: **{result.sequences[0] if result.sequences else 'N/A'}**",
        f"- Last Sequence: **{result.sequences[-1] if result.sequences else 'N/A'}**",
        f"- Missing Records: **{result.missing_records}**",
        f"- Sequence Continuity: **{continuity}**",
        f"- Malformed IMU Lines: **{len(result.malformed_imu_lines)}**",
        f"- Parsing: **{parsing}**",
        f"- Interval Minimum: **{min_interval} ms**",
        f"- Interval Average: **{avg_interval:.2f} ms**",
        f"- Interval Maximum: **{max_interval} ms**",
        f"- Observed Publish Rate: **{result.observed_rate_hz:.3f} Hz**",
        "",
        "## Status 분포",
        "",
    ]

    if result.statuses:
        lines.extend(f"- `{status}`: {count}" for status, count in sorted(result.statuses.items()))
    else:
        lines.append("- No valid IMU status records")

    lines.extend(["", "## 제어 Record 분포", ""])
    if result.control_records:
        lines.extend(f"- `{record_type}`: {count}" for record_type, count in sorted(result.control_records.items()))
    else:
        lines.append("- None")

    if result.sequence_gaps:
        lines.extend(["", "## Sequence Gap", ""])
        lines.extend(f"- `{previous} -> {current}`" for previous, current in result.sequence_gaps)

    if result.duplicate_or_reordered:
        lines.extend(["", "## Duplicate 또는 역순 Sequence", ""])
        lines.extend(f"- `{previous} -> {current}`" for previous, current in result.duplicate_or_reordered)

    if result.malformed_imu_lines:
        lines.extend(["", "## Parsing 실패 IMU Line", ""])
        lines.extend(f"- Line {line_number}: `{line}`" for line_number, line in result.malformed_imu_lines[:20])

    lines.extend(
        [
            "",
            "## 판정 주의",
            "",
            "- 10Hz와 60초는 목표 조건이다. 최종 Portfolio에는 이 분석에서 나온 실측값만 사용한다.",
            "- `BOOT`, `ACK`, `ERR`, `STATUS`, `OK`는 제어 Record이며 IMU Parsing 실패로 계산하지 않는다.",
            "",
        ]
    )
    return "\n".join(lines)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Analyze an STM32 IMU UART CSV session")
    parser.add_argument("log", nargs="?", type=Path, default=DEFAULT_LOG)
    parser.add_argument("--output", type=Path, help="Write Markdown summary to this path")
    parser.add_argument("--strict", action="store_true", help="Exit 1 on sequence or parsing issues")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    source = args.log.expanduser().resolve()
    if not source.is_file():
        raise SystemExit(f"Log file not found: {source}")

    result = analyze_lines(source.read_text(encoding="utf-8", errors="replace").splitlines())
    summary = format_markdown(source, result)
    if args.output:
        output = args.output.expanduser().resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(summary, encoding="utf-8")
        print(f"Saved analysis -> {output}")
    else:
        print(summary)

    if result.valid_records == 0:
        return 2
    if args.strict and (result.sequence_gaps or result.duplicate_or_reordered or result.malformed_imu_lines):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
