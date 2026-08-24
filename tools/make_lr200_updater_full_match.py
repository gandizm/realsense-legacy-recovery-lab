"""Patch a verified Intel R200 updater so discovery strings match LR200.

This script modifies device matching only. It does not contain firmware and
does not bypass firmware validation. Always verify the source hash yourself.
"""

import argparse
from hashlib import sha256
from pathlib import Path


def replace_padded(data: bytes, old_text: str, new_text: str, encoding: str) -> tuple[bytes, int]:
    old = old_text.encode(encoding)
    new = new_text.encode(encoding)
    unit = 2 if encoding == "utf-16-le" else 1
    if len(new) != len(old) + unit:
        raise ValueError("replacement must consume exactly one padding unit")
    needle = old + b"\0" * (2 * unit)
    replacement = new + b"\0" * unit
    return data.replace(needle, replacement), data.count(needle)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--expected-sha256", required=True)
    args = parser.parse_args()

    original = args.source.read_bytes()
    actual_hash = sha256(original).hexdigest().upper()
    if actual_hash != args.expected_sha256.upper():
        raise SystemExit(f"source SHA-256 mismatch: {actual_hash}")
    data = original

    old_id = "VID_8086&PID_0A80".encode("utf-16-le")
    new_id = "VID_8086&PID_0ABF".encode("utf-16-le")
    if data.count(old_id) != 4:
        raise SystemExit(f"expected 4 PID strings, found {data.count(old_id)}")
    data = data.replace(old_id, new_id)

    names = (
        "Intel(R) RealSense(TM) 3D Camera (R200) Depth",
        "Intel(R) RealSense(TM) 3D Camera (R200) Left-Right",
        "Intel(R) RealSense(TM) 3D Camera (R200) RGB",
    )
    ascii_replacements = 0
    for old_name in names:
        data, count = replace_padded(data, old_name, old_name.replace("(R200)", "(LR200)"), "ascii")
        ascii_replacements += count
    if ascii_replacements != 3 or len(data) != len(original):
        raise SystemExit(
            f"unexpected updater layout: names={ascii_replacements}, size={len(data)}"
        )
    args.output.write_bytes(data)
    print(f"source_sha256={actual_hash}")
    print(f"output_sha256={sha256(data).hexdigest().upper()}")
    print(f"bytes={len(data)}")


if __name__ == "__main__":
    main()
