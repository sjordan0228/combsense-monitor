#!/usr/bin/env python3
"""Provision a sensor-tag-wifi via its 3-second serial console window.

Waits for the C6's USB CDC to appear, then sends a keypress to enter the
console when it sees the prompt line, writes WiFi/MQTT creds, and exits
back to normal operation.
"""
import sys
import time
import glob
import serial


PORT_GLOB = "/dev/cu.usbmodem*"
BAUD = 115200
PROMPT_LINE = b"[CONSOLE]"
SHELL_PROMPT = b"> "

COMMANDS = [
    "set wifi_ssid IOT",
    "set wifi_pass 4696930759",
    "set mqtt_host 192.168.1.82",
    "set mqtt_port 1883",
    "set mqtt_user hivesense",
    "set mqtt_pass hivesense",
    "set tag_name bench-ds18b20",
    "set sample_int 30",
    "set upload_every 1",
    "list",
    "exit",
]


def find_port(timeout_s: float = 60.0) -> str:
    print(f"[prov] waiting up to {timeout_s:.0f}s for {PORT_GLOB} ...")
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        matches = glob.glob(PORT_GLOB)
        if matches:
            port = matches[0]
            print(f"[prov] found {port}")
            return port
        time.sleep(0.1)
    print("[prov] port never appeared — reconnect the C6 USB cable")
    sys.exit(1)


def open_serial(port: str, timeout_s: float = 5.0) -> serial.Serial:
    deadline = time.time() + timeout_s
    last_err = None
    while time.time() < deadline:
        try:
            return serial.Serial(port, BAUD, timeout=0.05)
        except (serial.SerialException, OSError) as err:
            last_err = err
            time.sleep(0.05)
    raise RuntimeError(f"could not open {port}: {last_err}")


def _read(ser: serial.Serial, n: int) -> bytes:
    try:
        return ser.read(n)
    except serial.SerialException:
        time.sleep(0.05)
        return b""


def wait_for(ser: serial.Serial, needle: bytes, timeout_s: float) -> bool:
    buf = bytearray()
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        chunk = _read(ser, 256)
        if chunk:
            buf.extend(chunk)
            sys.stdout.write(chunk.decode(errors="replace"))
            sys.stdout.flush()
            if needle in buf:
                return True
    return False


def drain(ser: serial.Serial, duration_s: float) -> None:
    end = time.time() + duration_s
    while time.time() < end:
        chunk = _read(ser, 256)
        if chunk:
            sys.stdout.write(chunk.decode(errors="replace"))
            sys.stdout.flush()


def main() -> None:
    port = find_port()
    with open_serial(port) as ser:
        print("[prov] opened — probing for console")
        # Send a harmless command to force the console to emit a fresh prompt.
        # We may be mid-readLine from a previous session, so a bare newline
        # wouldn't break out of the readLine skip-empty-lines path.
        ser.write(b"help\r")
        if not wait_for(ser, SHELL_PROMPT, timeout_s=5.0):
            print("\n[prov] no shell prompt — reconnect the C6 and retry")
            return

        for cmd in COMMANDS:
            print(f"\n[prov] >> {cmd}")
            ser.write(cmd.encode() + b"\r")
            wait_for(ser, SHELL_PROMPT, timeout_s=3.0)

        print("\n[prov] provisioned — tailing serial for 60s")
        drain(ser, 60.0)


if __name__ == "__main__":
    main()
