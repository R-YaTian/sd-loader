import argparse
import re
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description="Save unsigned char arrays from a C header as .bin files")
    parser.add_argument("header_file", type=Path)
    args = parser.parse_args()

    text = args.header_file.read_text(encoding="utf-8-sig")
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.DOTALL)
    arrays = re.findall(
        r"\bunsigned\s+char\s+(\w+)\s*\[[^\]]*\]\s*=\s*\{([^}]*)\}\s*;",
        text,
        flags=re.DOTALL,
    )
    if not arrays:
        parser.error("No unsigned char arrays found")

    for name, body in arrays:
        values = []
        for token in body.split(","):
            token = token.strip()
            if not token:
                continue
            if not re.fullmatch(r"0[xX][0-9a-fA-F]+|[0-9]+", token):
                parser.error(f"Array {name} contains an unsupported value: {token}")
            value = int(token, 16 if token.lower().startswith("0x") else 10)
            if not 0 <= value <= 255:
                parser.error(f"Value in array {name} is out of byte range: {token}")
            values.append(value)

        output = args.header_file.with_name(f"{name}.bin")
        output.write_bytes(bytes(values))
        print(f"{output}: {len(values)} bytes")


if __name__ == "__main__":
    main()
