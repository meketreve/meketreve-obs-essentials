#!/usr/bin/env python3
"""Builds the site into an output folder (default: _site next to this file).

Fills the presets section of index.template.html from data/presets/*.json,
encoding each one as the plugin does: "MOE1:" + base64url(qCompress(json)),
where qCompress is a 4-byte big-endian length followed by zlib data.

Also builds the Texuguito command list from commands.json twice:
comandos.html opens in Portuguese and commands.html in English (the bot's
!comandos / !commands link to them).
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


WHO = {
    "all": ("todos", "everyone"),
    "mod": ("moderadores", "moderators"),
    "owner": ("dono do canal", "channel owner"),
}


def both(pt: str, en: str) -> str:
    """Markup already escaped (the JSON may carry <code>)."""
    return f'<span lang="pt-BR">{pt}</span><span lang="en">{en}</span>'


def who_badge(who: str) -> str:
    pt, en = WHO[who]
    cls = "who" if who == "all" else "who " + who
    return f'<span class="{cls}">{both(pt, en)}</span>'


def command_card(cmd: dict) -> str:
    pt, en = html.escape(cmd["pt"]), html.escape(cmd["en"])
    also = cmd.get("also", [])
    # Each language shows its own name first and the other one below.
    pt_also = ([] if en == pt else [en.split(" ")[0]]) + also
    en_also = ([] if en == pt else [pt.split(" ")[0]]) + also
    def also_line(label: str, names: list) -> str:
        return f'<div class="also">{label} {", ".join(f"<code>{html.escape(n)}</code>" for n in names)}</div>' if names else ""
    name = both(f"<code>{pt}</code>{also_line('também:', pt_also)}", f"<code>{en}</code>{also_line('also:', en_also)}")
    return (
        '  <div class="cmd">\n'
        f'    <div class="name">{name}</div>\n'
        f'    <div class="desc">{both(cmd["desc"]["pt"], cmd["desc"]["en"])}</div>\n'
        f'    <div>{who_badge(cmd["who"])}</div>\n'
        "  </div>"
    )


def commands_page(lang: str) -> str:
    data = json.loads((HERE / "commands.json").read_text(encoding="utf-8"))
    sections = []
    for sec in data["sections"]:
        title = sec["title"]
        intro = sec.get("intro")
        sections.append(
            "<section>\n"
            f'<h2 lang="pt-BR">{title["pt"]}</h2><h2 lang="en">{title["en"]}</h2>\n'
            + (f'<p class="intro" lang="pt-BR">{intro["pt"]}</p><p class="intro" lang="en">{intro["en"]}</p>\n' if intro else "")
            + '<div class="cmds">\n'
            + "\n".join(command_card(c) for c in sec["commands"])
            + "\n</div>\n</section>"
        )
    index = (HERE / "index.template.html").read_text(encoding="utf-8")
    style = index[index.index("<style>") + len("<style>") : index.index("</style>")].strip("\n")
    page = (HERE / "commands.template.html").read_text(encoding="utf-8")
    for key, value in {
        "%%STYLE%%": style,
        "%%SECTIONS%%": "\n\n".join(sections),
        "%%WHO_ALL%%": both(*WHO["all"]),
        "%%WHO_MOD%%": both(*WHO["mod"]),
        "%%WHO_OWNER%%": both(*WHO["owner"]),
        "%%TITLE%%": "Texuguito commands" if lang == "en" else "Comandos do Texuguito",
        "%%HTMLLANG%%": "en" if lang == "en" else "pt-BR",
        "%%LANG%%": lang,
    }.items():
        page = page.replace(key, value)
    return page


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
    (out / "comandos.html").write_text(commands_page("pt"), encoding="utf-8")
    (out / "commands.html").write_text(commands_page("en"), encoding="utf-8")
    (out / ".nojekyll").write_text("")
    print(f"site written to {out}")


if __name__ == "__main__":
    main()
