#!/usr/bin/env python3

import serial
import sys
import time


BAUDRATE = 115200


def build_message(length):
    """Build a valid workshop IPC message with a chosen length."""

    magic = 0xB5
    command = 0x10
    sensor_id = 1001

    temperature = 72
    pressure = 101

    payload = (
        temperature.to_bytes(4, "little") +
        pressure.to_bytes(4, "little") +
        bytes(24)
    )

    return (
        bytes([magic]) +
        bytes([command]) +
        length.to_bytes(2, "little") +
        sensor_id.to_bytes(4, "little") +
        payload
    )


def open_uart(path):
    print(f"[+] Opening UART: {path}")

    ser = serial.Serial(
        path,
        baudrate=BAUDRATE,
        timeout=0.2,
    )

    time.sleep(0.5)

    # Clear anything already waiting in the UART.
    ser.reset_input_buffer()

    print("[+] UART connected")
    return ser


def interactive_shell(ser):
    print()
    print("=== Interactive UART Shell ===")
    print("Type Zephyr shell commands.")
    print("Ctrl+C to exit.")
    print()

    try:
        while True:
            command = input("uart> ")

            if not command:
                continue

            ser.write(command.encode() + b"\r")
            ser.flush()

            # Give Zephyr a moment to process the command.
            time.sleep(0.1)

            # Print everything currently available.
            while ser.in_waiting:
                data = ser.read(ser.in_waiting)
                sys.stdout.write(data.decode(errors="replace"))
                sys.stdout.flush()

    except KeyboardInterrupt:
        print("\n[+] Leaving interactive shell")


def send_command(ser, command, delay=0.15):
    """Send a Zephyr shell command and return available output."""

    ser.write(command.encode() + b"\r")
    ser.flush()

    time.sleep(delay)

    output = b""

    while ser.in_waiting:
        output += ser.read(ser.in_waiting)

    return output.decode(errors="replace")


def fuzz(ser):
    print()
    print("=== IPC Length Fuzzer ===")
    print()
    print("[+] Stopping periodic sensor traffic")

    response = send_command(ser, "sensor stop")
    print(response, end="")

    print()
    print("[+] Testing length values 1..32")
    print()

    for length in range(1, 33):

        message = build_message(length)
        hex_message = message.hex()

        print(f"[>] length={length:2d}  {hex_message}")

        response = send_command(
            ser,
            f"ipc send {hex_message}",
            delay=0.2,
        )

        if response.strip():
            print(response.strip())

        # Give the controller thread time to process the message.
        time.sleep(0.2)

    print()
    print("[+] Fuzzing complete")


def usage():
    print(
        f"""
Usage:

  {sys.argv[0]} <UART> shell
  {sys.argv[0]} <UART> fuzz

Examples:

  {sys.argv[0]} /dev/pts/7 shell
  {sys.argv[0]} /dev/pts/7 fuzz
"""
    )


def main():

    if len(sys.argv) != 3:
        usage()
        sys.exit(1)

    uart = sys.argv[1]
    mode = sys.argv[2].lower()

    if mode not in ("shell", "fuzz"):
        print(f"Unknown mode: {mode}")
        usage()
        sys.exit(1)

    ser = open_uart(uart)

    try:
        if mode == "shell":
            interactive_shell(ser)

        elif mode == "fuzz":
            fuzz(ser)

    finally:
        ser.close()
        print("[+] UART closed")


if __name__ == "__main__":
    main()