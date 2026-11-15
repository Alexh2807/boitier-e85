"""Copie l'interface (android/app/src/main/assets/index.html) dans le firmware E85 (web_ui.h).
A relancer apres chaque modification de index.html :  python tools/gen_web_ui.py
"""
from pathlib import Path

root = Path(__file__).resolve().parent.parent
html = (root / "android/app/src/main/assets/index.html").read_text(encoding="utf-8")
assert ")WEBUI\"" not in html
out = root / "firmware/E85_Master/web_ui.h"
out.write_text(
    "// GENERE AUTOMATIQUEMENT par tools/gen_web_ui.py - ne pas modifier a la main\n"
    "#pragma once\n#include <pgmspace.h>\n"
    'static const char WEB_UI[] PROGMEM = R"WEBUI(' + html + ')WEBUI";\n',
    encoding="utf-8",
)
print("web_ui.h :", out.stat().st_size, "octets")
