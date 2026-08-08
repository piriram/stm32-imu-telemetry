#!/usr/bin/env python3
"""Collect reproducible UART validation evidence from the STM32 IMU node."""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

import serial
from serial.tools import list_ports


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT_DIR = PROJECT_ROOT / "docs" / "validation"
USB_KEYWORDS = ("usb", "serial", "uart", "cp210", "ch340", "ftdi")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Collect a 60-second telemetry log and exercise the UART command interface. "
            "Use --recovery-test when a person is present to disconnect/reconnect one I2C signal jumper."
        )
    )
    parser.add_argument("--port", help="Serial device, for example /dev/cu.usbserial-0001")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--duration", type=float, default=60.0, help="Telemetry capture duration in seconds")
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument(
        "--skip-telemetry",
        action="store_true",
        help="Keep the existing 60-second log and run only the requested command/recovery tests",
    )
    parser.add_argument("--skip-command-test", action="store_true")
    parser.add_argument(
        "--burst-test",
        action="store_true",
        help="Send a 512-byte burst and record whether the RX overflow counter increases",
    )
    parser.add_argument(
        "--recovery-test",
        action="store_true",
        help="Interactively capture SENSOR_OFFLINE and SENSOR_RECOVERED",
    )
    parser.add_argument("--recovery-timeout", type=float, default=12.0)
    return parser.parse_args()


def port_rows() -> list[tuple[str, str]]:
    return [(port.device, port.description or "unknown") for port in list_ports.comports()]


def select_port(requested: str | None) -> str:
    rows = port_rows()
    devices = {device for device, _ in rows}

    if requested:
        if requested not in devices:
            available = "\n".join(f"  {device}  ({description})" for device, description in rows)
            raise SystemExit(f"Serial port not found: {requested}\nAvailable ports:\n{available or '  none'}")
        return requested

    candidates = [
        device
        for device, description in rows
        if device.startswith("/dev/cu.")
        and any(keyword in f"{device} {description}".lower() for keyword in USB_KEYWORDS)
    ]
    if len(candidates) == 1:
        return candidates[0]

    available = "\n".join(f"  {device}  ({description})" for device, description in rows)
    reason = "No USB serial port was detected" if not candidates else "Multiple USB serial ports were detected"
    raise SystemExit(f"{reason}. Pass --port explicitly.\nAvailable ports:\n{available or '  none'}")


def write_lines(path: Path, lines: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    text = "\n".join(lines)
    path.write_text(f"{text}\n" if text else "", encoding="utf-8")
    print(f"Saved {len(lines)} lines -> {path}")


class UARTSession:
    def __init__(self, port: str, baud: int):
        self.serial = serial.Serial(port, baud, timeout=0.1)

    def close(self) -> None:
        self.serial.close()

    def drain(self, duration: float = 0.4) -> list[str]:
        return self.read_for(duration, echo=False)

    def read_for(self, duration: float, *, echo: bool = True) -> list[str]:
        lines: list[str] = []
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            raw = self.serial.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").strip()
            if not line:
                continue
            lines.append(line)
            if echo:
                print(line)
        return lines

    def send_command(self, command: str, read_seconds: float = 0.8) -> list[str]:
        print(f"> {command}")
        self.serial.write(f"{command}\r\n".encode("ascii"))
        self.serial.flush()
        return self.read_for(read_seconds)


def contains(lines: list[str], expected: str) -> bool:
    return any(expected in line for line in lines)


def collect_telemetry(session: UARTSession, duration: float, output_dir: Path) -> list[str]:
    print(f"\n=== Telemetry capture: {duration:.1f}s ===")
    session.drain()
    session.send_command("stream on", 0.5)
    session.drain(0.2)

    lines: list[str] = []
    start = time.monotonic()
    last_report = start
    while time.monotonic() - start < duration:
        chunk = session.read_for(0.25, echo=False)
        lines.extend(chunk)
        now = time.monotonic()
        if now - last_report >= 5.0:
            imu_count = sum(line.startswith("IMU,") for line in lines)
            print(f"  {now - start:5.1f}s / {duration:.1f}s, IMU records: {imu_count}")
            last_report = now

    for line in lines[-8:]:
        print(line)
    write_lines(output_dir / "uart_60s_session.txt", lines)
    return lines


def run_command_test(session: UARTSession, output_dir: Path, burst_test: bool) -> tuple[list[str], list[str]]:
    print("\n=== Command interface test ===")
    log: list[str] = []
    failures: list[str] = []

    def exercise(command: str, expected: str, read_seconds: float = 0.8) -> list[str]:
        log.append(f"> {command}")
        response = session.send_command(command, read_seconds)
        log.extend(response)
        if not contains(response, expected):
            failures.append(f"{command!r}: expected {expected}")
        return response

    exercise("status", "STATUS,")
    off_response = exercise("stream off", "ACK,STREAM,OFF", 1.2)
    imu_after_off = sum(line.startswith("IMU,") for line in off_response)
    log.append(f"# IMU records observed during stream-off response window: {imu_after_off}")
    exercise("hello", "ERR,INVALID_COMMAND")
    exercise("1234567890123456789012345678901234567890", "ERR,COMMAND_TOO_LONG")
    on_response = exercise("stream on", "ACK,STREAM,ON", 1.2)
    if not any(line.startswith("IMU,") for line in on_response):
        failures.append("stream on: no IMU record observed after ACK")
    exercise("status", "STATUS,")

    if burst_test:
        print("> [512-byte burst]")
        log.append("> [512-byte burst]")
        session.serial.write(b"x" * 512 + b"\r\n")
        session.serial.flush()
        log.extend(session.read_for(1.5))
        status_lines = exercise("status", "STATUS,", 1.0)
        log.append("# Inspect STATUS overflow=<n>; do not claim overflow unless n increased.")
        if not any("overflow=" in line for line in status_lines):
            failures.append("burst test: status response missing overflow counter")

    write_lines(output_dir / "uart_command_session.txt", log)
    return log, failures


def run_recovery_test(session: UARTSession, output_dir: Path, timeout: float) -> tuple[list[str], list[str]]:
    print("\n=== Interactive sensor recovery test ===")
    print("Keep VCC and GND connected. Only handle one I2C signal jumper such as SDA.")
    input("Hold the SDA jumper so it can be removed immediately, then press Enter... ")
    print(f"DISCONNECT SDA NOW. Waiting up to {timeout:.0f}s for ERR,SENSOR_OFFLINE...")
    offline_lines: list[str] = []
    offline_deadline = time.monotonic() + timeout
    while time.monotonic() < offline_deadline:
        offline_lines.extend(session.read_for(0.25))
        if contains(offline_lines, "ERR,SENSOR_OFFLINE"):
            break

    input("Reconnect SDA now, then press Enter... ")
    print(f"Waiting up to {timeout:.0f}s for OK,SENSOR_RECOVERED and resumed IMU records...")
    recovered_lines: list[str] = []
    recovered_deadline = time.monotonic() + timeout
    while time.monotonic() < recovered_deadline:
        recovered_lines.extend(session.read_for(0.25))
        recovered_seen = contains(recovered_lines, "OK,SENSOR_RECOVERED")
        imu_after_recovery = sum(line.startswith("IMU,") for line in recovered_lines)
        if recovered_seen and imu_after_recovery >= 3:
            break
    lines = ["# ACTION: SDA disconnected", *offline_lines, "# ACTION: SDA reconnected", *recovered_lines]
    write_lines(output_dir / "uart_error_recovery.txt", lines)

    failures: list[str] = []
    if not contains(offline_lines, "ERR,SENSOR_OFFLINE"):
        failures.append("recovery test: ERR,SENSOR_OFFLINE not observed")
    if not contains(recovered_lines, "OK,SENSOR_RECOVERED"):
        failures.append("recovery test: OK,SENSOR_RECOVERED not observed")
    resumed_imu_count = sum(line.startswith("IMU,") for line in recovered_lines)
    if resumed_imu_count < 3:
        failures.append(
            f"recovery test: expected at least 3 resumed IMU records, observed {resumed_imu_count}"
        )
    return lines, failures


def main() -> int:
    args = parse_args()
    port = select_port(args.port)
    output_dir = args.output_dir.expanduser().resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    print("=== UART validation configuration ===")
    print(f"Port: {port}")
    print(f"Baud: {args.baud}")
    print(f"Output: {output_dir}")

    failures: list[str] = []
    session = UARTSession(port, args.baud)
    try:
        if not args.skip_telemetry:
            telemetry_lines = collect_telemetry(session, args.duration, output_dir)
            if not any(line.startswith("IMU,") for line in telemetry_lines):
                failures.append("telemetry test: no IMU records collected")

        if not args.skip_command_test:
            _, command_failures = run_command_test(session, output_dir, args.burst_test)
            failures.extend(command_failures)

        if args.recovery_test:
            session.send_command("stream on", 0.5)
            _, recovery_failures = run_recovery_test(session, output_dir, args.recovery_timeout)
            failures.extend(recovery_failures)
    finally:
        session.close()

    print("\n=== Capture result ===")
    if failures:
        for failure in failures:
            print(f"REVIEW: {failure}")
        return 1

    print("PASS: requested UART evidence was captured.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\nCancelled by user.", file=sys.stderr)
        raise SystemExit(130)
