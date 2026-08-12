"""Configure a TAKO-EPAPER device over encrypted BLE GATT."""

import argparse
import asyncio
import getpass

from bleak import BleakClient, BleakScanner


DEVICE_NAME = "TAKO-EPAPER"
UUID_BASE = "7b1e00{:02x}-b5a3-f393-e0a9-e50e24dcca9e"
SSID_UUID = UUID_BASE.format(1)
PASSWORD_UUID = UUID_BASE.format(2)
API_KEY_UUID = UUID_BASE.format(3)
INTERVAL_UUID = UUID_BASE.format(4)
COMMAND_UUID = UUID_BASE.format(5)
STATUS_UUID = UUID_BASE.format(6)


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ssid", required=True, help="Wi-Fi SSID")
    parser.add_argument("--password", help="Wi-Fi password; prompts when omitted")
    parser.add_argument("--api-key", help="Tako API key; prompts when omitted")
    parser.add_argument(
        "--interval",
        type=int,
        default=60,
        help="refresh interval in minutes (1-10080, default: 60)",
    )
    return parser.parse_args()


async def find_device():
    print(f"Scanning for {DEVICE_NAME}...")
    device = await BleakScanner.find_device_by_filter(
        lambda found, advertisement: advertisement.local_name == DEVICE_NAME,
        timeout=20,
    )
    if device is None:
        raise RuntimeError(
            f"{DEVICE_NAME} not found; press BOOT while the device is sleeping"
        )
    return device


async def configure(args):
    if not 1 <= args.interval <= 10080:
        raise ValueError("interval must be between 1 and 10080 minutes")

    password = args.password
    if password is None:
        password = getpass.getpass("Wi-Fi password (empty for open network): ")
    api_key = args.api_key or getpass.getpass("Tako API key: ")
    if not api_key:
        raise ValueError("Tako API key cannot be empty")

    device = await find_device()
    async with BleakClient(device) as client:
        print(f"Connected to {device.name}; approve pairing if prompted")
        await client.pair()
        await client.write_gatt_char(SSID_UUID, args.ssid.encode(), response=True)
        await client.write_gatt_char(PASSWORD_UUID, password.encode(), response=True)
        await client.write_gatt_char(API_KEY_UUID, api_key.encode(), response=True)
        await client.write_gatt_char(
            INTERVAL_UUID, str(args.interval).encode(), response=True
        )
        await client.write_gatt_char(COMMAND_UUID, b"save", response=True)
        status = (await client.read_gatt_char(STATUS_UUID)).decode(errors="replace")
        if status != "SAVED":
            raise RuntimeError(f"device rejected configuration: {status}")
        print("Configuration saved. The device will refresh and enter deep sleep.")


if __name__ == "__main__":
    asyncio.run(configure(parse_args()))
