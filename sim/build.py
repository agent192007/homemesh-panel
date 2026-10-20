#!/usr/bin/env python3
"""Compile the panel's real UI to WebAssembly for the designer's simulator.

    python sim/build.py            # incremental
    python sim/build.py --clean

Sources are the firmware's own: ui/dashboard.cpp, ui/panel_icons.cpp, the
icon fonts, panel_theme.cpp and layout_store.cpp, against the firmware's
include/lv_conf.h, with LVGL 8.3.11 and ArduinoJson taken from the copies
PlatformIO already downloaded (run `pio run -e ha` once if they are
missing). Nothing in src/ is modified or forked; sim/ only adds a
transport, an entry point and two tiny Arduino shims.

Output: designer/src/sim/panel_sim.mjs - a single ES module with the
WebAssembly embedded, committed so the designer builds without
Emscripten. Only rebuilding the simulator needs the Emscripten SDK
(found via $EMSDK, else ../emsdk beside the project).
"""
import concurrent.futures as cf
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SIM = ROOT / "sim"
CACHE = SIM / "build"
OUT = ROOT / "designer" / "src" / "sim" / "panel_sim.mjs"
LIBS = ROOT / ".pio" / "libdeps" / "ha"
LVGL = LIBS / "lvgl"
AJSON = LIBS / "ArduinoJson" / "src"

EXPORTS = ["sim_boot", "sim_step", "sim_framebuffer", "sim_pointer", "sim_event",
           "sim_store_layout", "sim_layout_from_nvs", "sim_pool_free", "sim_fit_label", "sim_text_width", "malloc", "free"]
RUNTIME = ["ccall", "cwrap", "HEAPU16", "UTF8ToString", "stringToUTF8", "lengthBytesUTF8"]

INCLUDES = [SIM / "shim", ROOT / "include", ROOT / "src", LVGL, AJSON]
DEFINES = ["LV_CONF_INCLUDE_SIMPLE", "LV_LVGL_H_INCLUDE_SIMPLE"]
CFLAGS = ["-O2"] + [f"-I{p}" for p in INCLUDES] + [f"-D{d}" for d in DEFINES]
CXXFLAGS = CFLAGS + ["-std=gnu++17", "-fno-exceptions", "-fno-rtti"]


EMSDK = Path(os.environ.get("EMSDK") or ROOT.parent / "emsdk")
EMBIN = EMSDK / "upstream" / "emscripten"


def tool(name):
    """emcc / em++ by absolute path. Current emsdk ships .exe launchers on
    Windows (older ones had .bat), so take whichever exists."""
    for ext in (".exe", ".bat", ""):
        t = EMBIN / (name + ext)
        if t.exists():
            return str(t)
    sys.exit(f"{name} not found under {EMBIN}. Install the Emscripten SDK there or set EMSDK.")


def emsdk_env():
    """emcc finds its node, python and LLVM through the .emscripten config
    that `emsdk activate` writes; pointing EM_CONFIG at it is enough."""
    env = dict(os.environ)
    env["EMSDK"] = str(EMSDK)
    env["EM_CONFIG"] = str(EMSDK / ".emscripten")
    return env


def sources():
    if not LVGL.exists() or not AJSON.exists():
        sys.exit(f"LVGL/ArduinoJson not found under {LIBS}. Run `pio run -e ha` once.")
    srcs = sorted((LVGL / "src").rglob("*.c"))
    srcs += [ROOT / "src" / "ui" / "dashboard.cpp", ROOT / "src" / "ui" / "panel_icons.cpp",
             ROOT / "src" / "panel_theme.cpp", ROOT / "src" / "layout_store.cpp"]
    srcs += sorted((ROOT / "src" / "ui" / "fonts").glob("*.c"))
    srcs += [SIM / "sim_main.cpp", SIM / "sim_transport.cpp", SIM / "shim" / "shims.cpp"]
    return srcs


def obj_for(src, flags):
    # Cache key: the source path and the flags. Headers are not tracked,
    # so --clean after editing lv_conf.h or a shared header.
    h = hashlib.sha1((str(src) + " ".join(flags)).encode()).hexdigest()[:12]
    return CACHE / f"{src.stem}-{h}.o"


def compile_one(env, src):
    cpp = src.suffix == ".cpp"
    flags = CXXFLAGS if cpp else CFLAGS
    obj = obj_for(src, flags)
    if obj.exists() and obj.stat().st_mtime >= src.stat().st_mtime:
        return obj, None
    cmd = [tool("em++" if cpp else "emcc"), "-c", str(src), "-o", str(obj)] + flags
    r = subprocess.run(cmd, env=env, capture_output=True, text=True)
    return obj, (r.stderr if r.returncode else None)


def main():
    if "--clean" in sys.argv:
        for f in CACHE.glob("*.o"):
            f.unlink()
    CACHE.mkdir(parents=True, exist_ok=True)
    env = emsdk_env()
    srcs = sources()
    print(f"compiling {len(srcs)} sources")
    objs, errors = [], []
    with cf.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        for obj, err in ex.map(lambda s: compile_one(env, s), srcs):
            objs.append(obj)
            if err:
                errors.append(err)
    if errors:
        print("\n".join(errors[:5]))
        sys.exit(f"{len(errors)} file(s) failed to compile")

    OUT.parent.mkdir(parents=True, exist_ok=True)
    link = [tool("em++"), *map(str, objs), "-o", str(OUT), "-O2",
            "-sMODULARIZE=1", "-sEXPORT_ES6=1", "-sEXPORT_NAME=createPanelSim",
            "-sENVIRONMENT=web,node", "-sSINGLE_FILE=1", "-sINITIAL_MEMORY=16777216",
            "-sEXIT_RUNTIME=0", "-sFILESYSTEM=0",
            f"-sEXPORTED_FUNCTIONS={json.dumps(['_' + e for e in EXPORTS], separators=(',', ':'))}",
            f"-sEXPORTED_RUNTIME_METHODS={json.dumps(RUNTIME, separators=(',', ':'))}"]
    # The link line is long; write it to a response file.
    rsp = CACHE / "link.rsp"
    rsp.write_text("\n".join(a.replace("\\", "/") for a in link[1:]), encoding="utf-8")
    r = subprocess.run([link[0], f"@{rsp}"], env=env, capture_output=True, text=True)
    if r.returncode:
        print(r.stderr[-4000:])
        sys.exit("link failed")
    print(f"wrote {OUT} ({OUT.stat().st_size // 1024} KB)")


if __name__ == "__main__":
    main()
