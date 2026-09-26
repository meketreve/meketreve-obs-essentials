#!/usr/bin/env python3
"""Works out the next release from the commits since the last version tag.

Conventional commit subjects decide it: "feat" bumps the minor version,
"fix" the patch, and "!" before the colon (feat!:, fix(x)!:) the major one.
"refactor" and "perf" go into the notes under "Other changes" but do not
warrant a release on their own; chore, docs, test, style and ci stay out.

Prints key=value lines for $GITHUB_OUTPUT (version, previous, release) and
writes the release notes (Markdown) to the path given as the first argument.
Run it locally to preview: python3 .github/scripts/next_release.py notes.md
"""
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SUBJECT = re.compile(r"^(?P<type>feat|fix|refactor|perf)(?:\((?P<scope>[^)]*)\))?(?P<bang>!)?:\s*(?P<text>.+)$")


def git(*args: str) -> str:
    return subprocess.run(["git", *args], cwd=ROOT, check=True, capture_output=True, text=True).stdout


def last_tag() -> str | None:
    tags = git("tag", "--list", "[0-9]*.[0-9]*.[0-9]*", "--sort=-v:refname").split()
    tags = [t for t in tags if re.fullmatch(r"\d+\.\d+\.\d+", t)]
    return tags[0] if tags else None


def bump(version: str, level: str) -> str:
    major, minor, patch = (int(p) for p in version.split("."))
    if level == "major":
        return f"{major + 1}.0.0"
    if level == "minor":
        return f"{major}.{minor + 1}.0"
    return f"{major}.{minor}.{patch + 1}"


def main() -> None:
    notes_path = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else None
    previous = last_tag()
    if previous is None:
        previous = json.loads((ROOT / "buildspec.json").read_text(encoding="utf-8"))["version"]
        log = git("log", "--format=%H%x1f%s%x1f%b%x1e")
    else:
        log = git("log", "--format=%H%x1f%s%x1f%b%x1e", f"{previous}..HEAD")

    features, fixes, breaking, others = [], [], [], []
    for entry in filter(None, (e.strip() for e in log.split("\x1e"))):
        sha, subject, body = (entry.split("\x1f") + ["", ""])[:3]
        m = SUBJECT.match(subject.strip())
        if not m:
            continue
        scope = f"**{m['scope']}:** " if m["scope"] else ""
        line = f"- {scope}{m['text']} ({sha[:7]})"
        if m["bang"] or "BREAKING CHANGE" in body:
            breaking.append(line)
        elif m["type"] == "feat":
            features.append(line)
        elif m["type"] == "fix":
            fixes.append(line)
        else:
            others.append(line)

    level = "major" if breaking else "minor" if features else "patch" if fixes else None
    version = bump(previous, level) if level else ""

    sections = []
    if breaking:
        sections.append("## ⚠️ Mudanças que quebram compatibilidade / Breaking changes\n" + "\n".join(breaking))
    if features:
        sections.append("## ✨ Novidades / New\n" + "\n".join(features))
    if fixes:
        sections.append("## 🐛 Correções / Fixes\n" + "\n".join(fixes))
    if others and level:
        sections.append("## 🔧 Outras mudanças / Other changes\n" + "\n".join(others))
    notes = "\n\n".join(sections) + "\n"
    if notes_path:
        notes_path.write_text(notes, encoding="utf-8")

    print(f"previous={previous}")
    print(f"version={version}")
    print(f"release={'true' if level else 'false'}")
    print(f"{previous} -> {version or '(no release)'}: {len(features)} feat, {len(fixes)} fix, "
          f"{len(breaking)} breaking, {len(others)} other", file=sys.stderr)


if __name__ == "__main__":
    main()
