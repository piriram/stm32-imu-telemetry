#!/usr/bin/env python3
"""Monitor and control the STM32 IMU CAN node through an SLCAN adapter."""

from __future__ import annotations

import argparse
import sys
import time
from dataclasses import dataclass

IMU_POSE_ID = 0x100
COMMAND_ID = 0x200
RESPONSE_ID = 0x201

COMMANDS = {
    "on": 0x01,
    "off": 0x02,
    "status": 0x03,
}

RESULTS = {
    0x00: "OK",
    0x01: "BAD_DLC",
    0x02: "BAD_COMMAND",
}

SENSOR_STATES = {
    0x00: "OK",
    0x01: "ERR_I2C",
    0x02: "SENSOR_OFFLINE",
    0x03: "RX_OVERFLOW",
}


@dataclass(frozen=True)
class PoseFrame:
    roll_cdeg: int
    pitch_cdeg: int
    sequence: int
    sensor_status: int
    protocol_version: int


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="CANable/SLCAN tool for the STM32 IMU CAN protocol"
    )
    parser.add_argument(
        "channel",
        help="SLCAN serial device, e.g. /dev/cu.usbmodem101",
    )
    parser.add_argument(
        "action",
        choices=("monitor", "on", "off", "status"),
        help="Monitor IMU frames or transmit one control command",
    )
    parser.add_argument("--bitrate", type=int, default=500_000)
    parser.add_argument("--tty-baudrate", type=int, default=115_200)
    parser.add_argument(
        "--duration",
        type=float,
        default=60.0,
        help="Monitor duration in seconds (default: 60)",
    )
    parser.add_argument(
        "--response-timeout",
        type=float,
        default=2.0,
        help="Command response timeout in seconds (default: 2)",
    )
    return parser.parse_args()


def decode_pose(data: bytes | bytearray) -> PoseFrame:
    if len(data) != 8:
        raise ValueError(f"IMU_POSE requires DLC 8, received {len(data)}")

    return PoseFrame(
        roll_cdeg=int.from_bytes(data[0:2], "little", signed=True),
        pitch_cdeg=int.from_bytes(data[2:4], "little", signed=True),
        sequence=int.from_bytes(data[4:6], "little", signed=False),
        sensor_status=data[6],
        protocol_version=data[7],
    )


def print_response(data: bytes | bytearray) -> None:
    if len(data) != 8:
        print(f"RESPONSE invalid DLC={len(data)} data={bytes(data).hex(' ')}")
        return

    command = data[0]
    result = RESULTS.get(data[1], f"UNKNOWN_{data[1]:02X}")
    stream = "ON" if data[2] else "OFF"
    sensor = SENSOR_STATES.get(data[3], f"UNKNOWN_{data[3]:02X}")
    tx_busy = int.from_bytes(data[4:6], "little")
    rx_overflow = int.from_bytes(data[6:8], "little")
    print(
        f"RESPONSE command=0x{command:02X} result={result} "
        f"stream={stream} sensor={sensor} tx_busy={tx_busy} "
        f"rx_overflow={rx_overflow}"
    )


def monitor(bus: object, duration: float) -> int:
    deadline = time.monotonic() + duration
    frame_count = 0
    gap_count = 0
    last_sequence: int | None = None

    while time.monotonic() < deadline:
        message = bus.recv(timeout=min(0.5, max(0.0, deadline - time.monotonic())))
        if message is None or message.is_extended_id:
            continue

        if message.arbitration_id == IMU_POSE_ID:
            try:
                pose = decode_pose(message.data)
            except ValueError as exc:
                print(f"IMU_POSE decode error: {exc}", file=sys.stderr)
                continue

            if last_sequence is not None:
                expected = (last_sequence + 1) & 0xFFFF
                if pose.sequence != expected:
                    gap_count += (pose.sequence - expected) & 0xFFFF
            last_sequence = pose.sequence
            frame_count += 1
            sensor = SENSOR_STATES.get(
                pose.sensor_status, f"UNKNOWN_{pose.sensor_status:02X}"
            )
            print(
                f"IMU seq={pose.sequence:5d} "
                f"roll={pose.roll_cdeg / 100:7.2f}deg "
                f"pitch={pose.pitch_cdeg / 100:7.2f}deg "
                f"sensor={sensor} protocol={pose.protocol_version}"
            )
        elif message.arbitration_id == RESPONSE_ID:
            print_response(message.data)

    print(f"SUMMARY duration={duration:.1f}s frames={frame_count} sequence_gaps={gap_count}")
    return 0 if frame_count > 0 and gap_count == 0 else 1


def send_command(bus: object, can_module: object, action: str, timeout: float) -> int:
    command = COMMANDS[action]
    message = can_module.Message(
        arbitration_id=COMMAND_ID,
        data=[command],
        is_extended_id=False,
    )
    bus.send(message, timeout=1.0)
    print(f"TX command={action.upper()} id=0x{COMMAND_ID:03X} data={command:02X}")

    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        response = bus.recv(timeout=min(0.2, deadline - time.monotonic()))
        if (
            response is not None
            and not response.is_extended_id
            and response.arbitration_id == RESPONSE_ID
        ):
            print_response(response.data)
            return 0

    print("ERROR response timeout", file=sys.stderr)
    return 1


def main() -> int:
    args = parse_args()

    try:
        import can
    except ImportError:
        print(
            "python-can is required: python3 -m pip install -r requirements-can.txt",
            file=sys.stderr,
        )
        return 2

    try:
        with can.Bus(
            interface="slcan",
            channel=args.channel,
            bitrate=args.bitrate,
            tty_baudrate=args.tty_baudrate,
        ) as bus:
            if args.action == "monitor":
                return monitor(bus, args.duration)
            return send_command(bus, can, args.action, args.response_timeout)
    except can.CanError as exc:
        print(f"CAN error: {exc}", file=sys.stderr)
        return 2
    except (OSError, ValueError) as exc:
        print(f"Adapter error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
