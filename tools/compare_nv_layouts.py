"""Compare an LR200 admin slot with an R200-layout slot, offline only."""

import argparse
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
    parser.add_argument("lr200", type=Path)
    parser.add_argument("r200", type=Path)
    parser.add_argument("sector", type=lambda value: int(value, 0))
    args = parser.parse_args()

    if not 0 <= args.sector < 0x100:
        raise SystemExit("sector must be in [0, 0x100)")
    lr200 = read_image(args.lr200)
    r200 = read_image(args.r200)
    address = args.sector * SECTOR_SIZE
    old_payload = lr200[address + LR_PREFIX : address + SECTOR_SIZE]
    new_payload = r200[address : address + PAYLOAD_SIZE]
    indexes = [i for i, (old, new) in enumerate(zip(old_payload, new_payload)) if old != new]
    print(f"sector=0x{args.sector:02X}")
    print(f"payload_bytes={PAYLOAD_SIZE}")
    print(f"different_bytes={len(indexes)}")
    if indexes:
        print(f"first_difference=0x{indexes[0]:03X}")
        print(f"last_difference=0x{indexes[-1]:03X}")
    print(f"r200_trailer={r200[address+PAYLOAD_SIZE:address+SECTOR_SIZE].hex(' ').upper()}")


if __name__ == "__main__":
    main()
