"""Integre le firmware pedale compile dans le firmware E85 (firmware/E85_Master/pedal_fw.h)."""
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
src = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "build/Pedale/Pedale.ino.bin"
data = src.read_bytes()
assert data[0] == 0xE9, "ce n'est pas une image d'application ESP32"
lines = []
for i in range(0, len(data), 32):
    lines.append(",".join(str(b) for b in data[i:i + 32]))
out = root / "firmware/E85_Master/pedal_fw.h"
out.write_text(
    "// GENERE AUTOMATIQUEMENT par tools/gen_pedal_fw.py - firmware du boitier pedale\n"
    "#pragma once\n#include <pgmspace.h>\n"
    f"#define PEDAL_FW_SIZE {len(data)}UL\n"
    "static const uint8_t PEDAL_FW[] PROGMEM = {\n" + ",\n".join(lines) + "\n};\n",
    encoding="ascii",
)
print("pedal_fw.h :", len(data), "octets de firmware pedale")
