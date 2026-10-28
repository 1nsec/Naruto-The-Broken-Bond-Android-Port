#!/usr/bin/env python3
"""Writes a stand-in for generated/ so the port compiles without the game.

The real generated/ comes from your own default.xex (scripts/build-game.ps1) and
never goes into the repository. CI has no game files, so this script renders
the SDK's codegen templates with placeholder image values and declares every
guest function the port's sources name (sub_XXXXXXXX). Each one is defined as
an empty function, so the build links; the result does not run the game.

Usage: make-stub-generated.py <repository root> [<output directory>]
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

PROJECT = "broken_bond"


def render(text: str, values: dict[str, str], functions: list[str], templates: Path,
           recomp_files: list[str] = ()) -> str:
    # {% include "codegen/x.inja" %}: relative to the templates folder.
    text = re.sub(r'\{%\s*include\s+"([^"]+)"\s*%\}',
                  lambda m: (templates / m.group(1)).read_text(encoding="utf-8"), text)
    # {% for file in recomp_files %} ... {% endfor %}
    text = re.sub(r"\{%\s*for file in recomp_files\s*%\}(.*?)\{%\s*endfor\s*%\}",
                  lambda m: "".join(m.group(1).replace("{{ file }}", f) for f in recomp_files),
                  text, flags=re.S)
    # {% for fn in functions %} ... {% endfor %}: the body is rendered per function.
    def expand_for(m: re.Match) -> str:
        body = m.group(1)
        out = []
        for index, name in enumerate(functions):
            address = "0x" + name.split("_", 1)[1].upper()
            item = body
            item = re.sub(r"\{%\s*if not fn\.below_code_base or fn\.is_import\s*%\}(.*?)\{%\s*endif\s*%\}",
                          r"\1", item, flags=re.S)
            item = item.replace("{{ fn.name }}", name).replace("{{ fn.address }}", address)
            out.append(item)
        return "".join(out)
    text = re.sub(r"\{%\s*for fn in functions\s*%\}(.*?)\{%\s*endfor\s*%\}", expand_for, text, flags=re.S)
    text = re.sub(r"\{%\s*for mod in dll_modules\s*%\}(.*?)\{%\s*endfor\s*%\}", "", text, flags=re.S)
    # {% if X %}A{% else %}B{% endif %} and {% if X %}A{% endif %}: every condition is false
    # here (no DLL modules, no config flags, no CRT heap).
    for _ in range(4):
        text = re.sub(r"\{%\s*if [^%]*%\}((?:(?!\{%\s*if).)*?)\{%\s*else\s*%\}(.*?)\{%\s*endif\s*%\}",
                      r"\2", text, flags=re.S)
        text = re.sub(r"\{%\s*if [^%]*%\}((?:(?!\{%\s*if).)*?)\{%\s*endif\s*%\}", "", text, flags=re.S)
    text = re.sub(r'\{\{\s*cmake_var\("([^"]+)"\)\s*\}\}', r"${\1}", text)
    # Codegen flags of the stand-in: all off.
    text = re.sub(r"\{\{\s*config_flags\.\w+\s*\}\}", "false", text)
    for key, value in values.items():
        text = re.sub(r"\{\{\s*" + re.escape(key) + r"\s*\}\}", value, text)
    leftover = re.search(r"\{\{|\{%", text)
    if leftover:
        raise SystemExit(f"unrendered template syntax near: {text[leftover.start():leftover.start() + 60]!r}")
    return text


def main() -> None:
    root = Path(sys.argv[1]).resolve()
    out = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else root / "generated" / "default"
    templates = root / "sdk" / "resources" / "templates"
    sources = [p for p in (root / "src").rglob("*") if p.suffix in (".cpp", ".h", ".inc")]
    names: set[str] = set()
    for path in sources:
        for match in re.finditer(r"\b(?:__imp__)?(sub_[0-9A-Fa-f]{8})\b", path.read_text(encoding="utf-8", errors="replace")):
            names.add("sub_" + match.group(1)[4:].upper())
    functions = sorted(names)

    values = {
        "project": PROJECT,
        # Placeholder layout of an Xbox 360 executable; nothing runs with it.
        "image_base": "0x82000000",
        "image_size": "0x01000000",
        "code_base": "0x82100000",
        "code_size": "0x00E00000",
        "thunk_reserve_size": "0x00001000",
        "names.snake_case": PROJECT,
    }
    # Never over real generated code (from your default.xex): only an empty
    # folder or an earlier stand-in is replaced.
    if (out / f"{PROJECT}_init.cpp").exists() and not (out / ".stub").exists():
        raise SystemExit(f"{out} holds generated code that is not a stand-in; refusing to overwrite it")
    out.mkdir(parents=True, exist_ok=True)
    (out / ".stub").write_text("Stand-in generated code (scripts/android/make-stub-generated.py)\n", encoding="utf-8")
    files = {
        f"{PROJECT}_pch.h": "pch_h.inja",
        f"{PROJECT}_funcs.h": "funcs_h.inja",
        f"{PROJECT}_init.h": "init_h.inja",
        f"{PROJECT}_init.cpp": "init_cpp.inja",
        f"{PROJECT}_register.cpp": "register_cpp.inja",
    }
    for name, template in files.items():
        text = render((templates / "codegen" / template).read_text(encoding="utf-8"), values, functions, templates)
        (out / name).write_text(text, encoding="utf-8", newline="\n")

    # The guest functions themselves: empty bodies under the same weak alias
    # scheme as real recompiled code, so the port's hooks still override them.
    body = [f"// Stand-in guest functions for a build without game files ({len(functions)}).",
            f'#include "{PROJECT}_init.h"', ""]
    for name in functions:
        body.append(f"DEFINE_REX_FUNC({name}) {{ (void)ctx; (void)base; }}")
    (out / f"{PROJECT}_recomp.0.cpp").write_text("\n".join(body) + "\n", encoding="utf-8", newline="\n")

    sources_cmake = render((templates / "codegen" / "sources_cmake.inja").read_text(encoding="utf-8"),
                           values, functions, templates, [f"{PROJECT}_recomp.0.cpp"])
    (out / "sources.cmake").write_text(sources_cmake, encoding="utf-8", newline="\n")
    # generated/rexglue.cmake (the SDK's init template), next to default/.
    values_cmake = dict(values, sdk_version="0.10.0", entrypoint_out_dir="generated/default")
    rexglue_cmake = render((templates / "init" / "rexglue_cmake.inja").read_text(encoding="utf-8"),
                           values_cmake, functions, templates)
    (out.parent / "rexglue.cmake").write_text(rexglue_cmake, encoding="utf-8", newline="\n")
    # The codegen step's stamp and depfile: present and empty, so a build does
    # not try to run codegen (there is no default.xex to run it on).
    (out / "codegen.build.stamp").write_text("stub\n", encoding="utf-8")
    (out / "codegen.d").write_text(f"{out / 'codegen.build.stamp'}:\n", encoding="utf-8")
    print(f"Stub generated code in {out}: {len(functions)} guest functions")


if __name__ == "__main__":
    main()
