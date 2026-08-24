"""Build a one-slot LR200-payload/R200-trailer hybrid image, offline only."""

import argparse
from hashlib import sha256
from pathlib import Path


FLASH_SIZE = 0x100000
SECTOR_SIZE = 0x1000
LR_PREFIX = 0x2D
PAYLOAD_SIZE = SECTOR_SIZE - LR_PREFIX


def read_image(path: Path) -> bytes:
    data = path.read_bytes()
    if len(data) != FLASH_SIZE:
        raise SystemExit(f"{path}: expected {FLASH_SIZE} bytes, got {len(data)}")
    return data


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--lr200", type=Path, required=True)
    parser.add_argument("--sector", type=lambda value: int(value, 0), required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    if not 0xA0 <= args.sector < 0x100:
        raise SystemExit("refusing: admin/NV sector must be in [0xA0, 0x100)")
    base_bytes = read_image(args.base)
    lr200 = read_image(args.lr200)
    output = bytearray(base_bytes)
    address = args.sector * SECTOR_SIZE
    trailer = bytes(output[address + PAYLOAD_SIZE : address + SECTOR_SIZE])
    output[address : address + PAYLOAD_SIZE] = lr200[
        address + LR_PREFIX : address + SECTOR_SIZE
    ]
    if bytes(output[address + PAYLOAD_SIZE : address + SECTOR_SIZE]) != trailer:
        raise SystemExit("R200 trailer changed")

    changed = [
        sector
        for sector in range(0x100)
        if output[sector * SECTOR_SIZE : (sector + 1) * SECTOR_SIZE]
        != base_bytes[sector * SECTOR_SIZE : (sector + 1) * SECTOR_SIZE]
    ]
    if changed not in ([], [args.sector]):
        raise SystemExit(f"unexpected changed sectors: {changed}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    print(f"output={args.output}")
    print(f"sha256={sha256(output).hexdigest().upper()}")
    print("changed_sectors=" + ("none" if not changed else f"0x{args.sector:02X}"))


if __name__ == "__main__":
    main()
