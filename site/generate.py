#!/usr/bin/env python3
"""Builds the site into an output folder (default: _site next to this file).

Fills the presets section of index.template.html from data/presets/*.json,
encoding each one as the plugin does: "MOE1:" + base64url(qCompress(json)),
where qCompress is a 4-byte big-endian length followed by zlib data.
"""
import base64
import html
import json
import pathlib
import shutil
import struct
import sys
import zlib

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent


def encode(bundle: dict) -> str:
    data = json.dumps(bundle, ensure_ascii=False, separators=(",", ":"), sort_keys=True).encode()
    packed = struct.pack(">I", len(data)) + zlib.compress(data, 9)
    return "MOE1:" + base64.urlsafe_b64encode(packed).decode().rstrip("=")


def preset_card(key: str, bundle: dict) -> str:
    name, desc = bundle["name"], bundle["description"]
    pid = "p-" + key
    return (
        '<div class="preset card">\n'
        f'    <h3><span lang="pt-BR">{html.escape(name["pt-BR"])}</span>'
        f'<span lang="en">{html.escape(name["en-US"])}</span></h3>\n'
        f'    <p lang="pt-BR">{html.escape(desc["pt-BR"])}</p><p lang="en">{html.escape(desc["en-US"])}</p>\n'
        f'    <pre id="{pid}">{encode(bundle)}</pre>\n'
        f'    <button data-target="{pid}"><span lang="pt-BR">Copiar</span><span lang="en">Copy</span></button>\n'
        "  </div>"
    )


def main() -> None:
    out = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else HERE / "_site"
    cards = [
        preset_card(p.stem, json.loads(p.read_text(encoding="utf-8")))
        for p in sorted((ROOT / "data" / "presets").glob("*.json"))
    ]
    page = (HERE / "index.template.html").read_text(encoding="utf-8").replace("%%PRESETS%%", "\n  ".join(cards))
    if out.exists():
        shutil.rmtree(out)
    shutil.copytree(HERE / "img", out / "img")
    (out / "index.html").write_text(page, encoding="utf-8")
    (out / ".nojekyll").write_text("")
    print(f"site written to {out}")


if __name__ == "__main__":
    main()
