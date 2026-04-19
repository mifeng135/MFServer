#!/usr/bin/env python3
"""
Compile root-level protoFile/*.proto (no subfolders) to generate/*.pb via protoc,
then scan those .proto sources for message-level option (msgId) and emit CoreConfig/ProtoMsg.lua.

Windows: 仓库里的 tools/protoc 为 macOS 可执行文件，需在 Windows 上自备 protoc：
  - 将官方 win64 包中的 protoc.exe 放到 tools/protoc.exe，或
  - 设置环境变量 MF_PROTOC（或 PROTOC）为 protoc.exe 的完整路径。
"""
from __future__ import annotations

import os
import platform
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PROTO_DIR = ROOT / "protoFile"
GENERATE_DIR = ROOT / "generProto"
OUT_LUA = ROOT / "script" / "Common" / "ProtoMsg.lua"

def resolve_protoc(root: Path) -> Path:
    for key in ("MF_PROTOC", "PROTOC"):
        raw = os.environ.get(key)
        if raw:
            p = Path(raw).expanduser()
            if p.is_file():
                return p.resolve()
            print(f"error: {key} is set but not a file: {p}", file=sys.stderr)
            sys.exit(1)
    if platform.system() == "Windows":
        candidates = [root / "tools" / "protoc.exe", root / "tools" / "protoc"]
    else:
        candidates = [root / "tools" / "protoc"]
    for c in candidates:
        if c.is_file():
            return c.resolve()
    hint = (
        "On Windows, download protoc from https://github.com/protocolbuffers/protobuf/releases "
        "and place protoc.exe in tools/, or set MF_PROTOC to its path."
        if platform.system() == "Windows"
        else "Place tools/protoc (macOS/Linux) or set MF_PROTOC."
    )
    print(f"error: protoc not found under {root / 'tools'}. {hint}", file=sys.stderr)
    sys.exit(1)

MSG_START = re.compile(r"\bmessage\s+(\w+)\s*\{")
MSG_ID_OPT = re.compile(r"option\s*\(\s*msgId\s*\)\s*=\s*(\d+)\s*;")


def strip_proto_comments(text: str) -> str:
    out: list[str] = []
    i, n = 0, len(text)
    while i < n:
        if i + 1 < n and text[i : i + 2] == "//":
            while i < n and text[i] != "\n":
                i += 1
            continue
        if i + 1 < n and text[i : i + 2] == "/*":
            end = text.find("*/", i + 2)
            if end == -1:
                break
            i = end + 2
            continue
        out.append(text[i])
        i += 1
    return "".join(out)


def extract_message_blocks(text: str) -> list[tuple[str, str]]:
    """Return (message_name, inner_text) for each `message Name { ... }` using brace depth."""
    s = strip_proto_comments(text)
    results: list[tuple[str, str]] = []
    pos = 0
    while pos < len(s):
        m = MSG_START.search(s, pos)
        if not m:
            break
        name = m.group(1)
        brace_open = m.end() - 1
        assert s[brace_open] == "{"
        depth = 0
        j = brace_open
        while j < len(s):
            c = s[j]
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    inner = s[brace_open + 1 : j]
                    results.append((name, inner))
                    pos = j + 1
                    break
            j += 1
        else:
            break
    return results


def collect_msg_ids(proto_path: Path) -> list[tuple[str, int]]:
    text = proto_path.read_text(encoding="utf-8")
    pairs: list[tuple[str, int]] = []
    for msg_name, inner in extract_message_blocks(text):
        om = MSG_ID_OPT.search(inner)
        if om:
            pairs.append((msg_name, int(om.group(1))))
    return pairs


def compile_pb(proto_path: Path, protoc_exe: Path) -> None:
    stem = proto_path.stem
    out_pb = GENERATE_DIR / f"{stem}.pb"
    cmd = [
        str(protoc_exe),
        f"--proto_path={PROTO_DIR}",
        "--include_imports",
        f"--descriptor_set_out={out_pb}",
        str(proto_path),
    ]
    r = subprocess.run(cmd, cwd=str(ROOT), capture_output=True, text=True)
    if r.returncode != 0:
        print(f"error: protoc failed for {proto_path.name}\n{r.stderr or r.stdout}", file=sys.stderr)
        sys.exit(r.returncode or 1)


def write_proto_msg_lua(entries: list[tuple[str, int]]) -> None:
    lines = [
        "msgDefine = {}",
    ]
    for name, mid in entries:
        lines.append(f"msgDefine.{name} = {mid}")
    lines.append("")
    lines.append("msgFactory = {}")
    for name, mid in entries:
        lines.append(f'msgFactory[{mid}] = "{name}"')
    lines.append("")
    OUT_LUA.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    if not PROTO_DIR.is_dir():
        print(f"error: missing {PROTO_DIR}", file=sys.stderr)
        sys.exit(1)
    protoc_exe = resolve_protoc(ROOT)
    GENERATE_DIR.mkdir(parents=True, exist_ok=True)

    root_protos = sorted(p for p in PROTO_DIR.glob("*.proto") if p.is_file())
    if not root_protos:
        print(f"error: no .proto under {PROTO_DIR} (root only)", file=sys.stderr)
        sys.exit(1)

    all_entries: list[tuple[str, int]] = []
    seen_names: set[str] = set()
    seen_ids: dict[int, str] = {}

    for proto in root_protos:
        compile_pb(proto, protoc_exe)
        for name, mid in collect_msg_ids(proto):
            if name in seen_names:
                print(f"error: duplicate message name {name!r}", file=sys.stderr)
                sys.exit(1)
            if mid in seen_ids:
                print(
                    f"error: duplicate msgId {mid} ({seen_ids[mid]} vs {name})",
                    file=sys.stderr,
                )
                sys.exit(1)
            seen_names.add(name)
            seen_ids[mid] = name
            all_entries.append((name, mid))

    if not all_entries:
        print("error: no message with option (msgId) in root .proto files", file=sys.stderr)
        sys.exit(1)

    write_proto_msg_lua(all_entries)
    print(f"Wrote {OUT_LUA} ({len(all_entries)} messages)")
    for p in sorted(GENERATE_DIR.glob("*.pb")):
        print(f"  pb: {p.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
