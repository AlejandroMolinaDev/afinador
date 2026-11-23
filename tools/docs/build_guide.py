#!/usr/bin/env python3
"""Builds a standalone HTML user guide (images embedded) from docs/USER_GUIDE_es.md.

Usage: python tools/docs/build_guide.py [input.md] [output.html]
Needs the `markdown` package (pip install markdown).
"""
import base64
import pathlib
import re
import sys

import markdown

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "docs" / "USER_GUIDE_es.md"
DST = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "build" / "Guia_de_usuario.html"

CSS = """
:root { --bg:#101317; --panel:#181d23; --line:#2a323c; --text:#e8ecf1; --muted:#8a95a3; --accent:#ff6b4a; }
* { box-sizing:border-box; }
body { margin:0; background:var(--bg); color:var(--text); font:16px/1.55 -apple-system,"Segoe UI",Roboto,Helvetica,Arial,sans-serif; }
main { max-width:880px; margin:0 auto; padding:32px 16px 64px; }
h1 { font-size:2em; margin-top:0; } h1, h2, h3 { line-height:1.25; }
h2 { margin-top:32px; }
a { color:var(--accent); }
code { background:var(--panel); border:1px solid var(--line); border-radius:4px; padding:0 4px; font-size:.92em; }
table { border-collapse:collapse; width:100%; margin:12px 0; display:block; overflow-x:auto; }
th, td { border:1px solid var(--line); padding:6px 10px; text-align:left; vertical-align:top; }
th { background:var(--panel); }
img { max-width:100%; height:auto; border-radius:8px; border:1px solid var(--line); }
blockquote { margin:12px 0; padding:8px 16px; border-left:3px solid var(--accent); background:var(--panel); color:var(--text); }
hr { border:0; border-top:1px solid var(--line); }
"""


def embed_images(html: str, base: pathlib.Path) -> str:
    def repl(m):
        src = m.group(1)
        path = (base / src).resolve()
        if not path.is_file():
            return m.group(0)
        mime = "image/png" if path.suffix.lower() == ".png" else "image/jpeg"
        data = base64.b64encode(path.read_bytes()).decode("ascii")
        return f'src="data:{mime};base64,{data}"'
    return re.sub(r'src="([^"]+)"', repl, html)


def main() -> None:
    text = SRC.read_text(encoding="utf-8")
    body = markdown.markdown(text, extensions=["tables", "fenced_code", "sane_lists"])
    body = embed_images(body, SRC.parent)
    title = "NovaPitch — Guía de usuario"
    html = (f'<!doctype html>\n<html lang="es">\n<head>\n<meta charset="utf-8">\n'
            f'<meta name="viewport" content="width=device-width, initial-scale=1">\n<title>{title}</title>\n'
            f'<style>{CSS}</style>\n</head>\n<body>\n<main>\n{body}\n</main>\n</body>\n</html>\n')
    DST.parent.mkdir(parents=True, exist_ok=True)
    DST.write_text(html, encoding="utf-8")
    print(f"wrote {DST} ({DST.stat().st_size // 1024} KB)")


if __name__ == "__main__":
    main()
