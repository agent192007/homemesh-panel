# PlatformIO post-build step for env:ha: write the whole-board image the
# designer flashes onto a panel (firmware/homemesh_panel.bin), with a
# small description beside it (firmware/homemesh_panel.json).
#
# Whole board means bootloader, partition table, boot_app0 and the app,
# merged from 0x0, so it brings up a display that has never run this
# project. merge_bin fills the gaps between parts with 0xFF and NVS is
# one of them, so it also erases a stored layout: the panel comes back on
# the compiled ha_layout.h until a layout is pushed.
#
# firmware/ is gitignored. The image has include/secrets.h compiled in -
# the Wi-Fi password and the Home Assistant token - which is also why the
# designer reads it from the project folder instead of carrying it.
import hashlib, json, re, subprocess, time
from pathlib import Path

Import("env")  # noqa: F821 - provided by PlatformIO


def secret(text, name):
    m = re.search(rf'#define\s+{name}\s+"([^"]*)"', text)
    return m.group(1) if m else ""


def write_image(source, target, env):
    project = Path(env.subst("$PROJECT_DIR"))
    build = Path(env.subst("$BUILD_DIR"))
    out = project / "firmware"
    out.mkdir(exist_ok=True)
    image = out / "homemesh_panel.bin"

    parts = []
    for offset, path in env.get("FLASH_EXTRA_IMAGES", []):
        parts += [env.subst(offset), env.subst(path)]
    parts += [env.subst("$ESP32_APP_OFFSET") or "0x10000", str(build / "firmware.bin")]

    esptool = Path(env.PioPlatform().get_package_dir("tool-esptoolpy")) / "esptool.py"
    subprocess.run([env.subst("$PYTHONEXE"), str(esptool), "--chip", "esp32", "merge_bin",
                    "-o", str(image), "--flash_mode", env.subst("$BOARD_FLASH_MODE") or "dio",
                    "--flash_freq", "40m", "--flash_size", "4MB", *parts], check=True)

    data = image.read_bytes()
    secrets = (project / "include" / "secrets.h").read_text(encoding="utf-8", errors="replace")
    try:
        commit = subprocess.run(["git", "-C", str(project), "describe", "--always", "--dirty"],
                                capture_output=True, text=True).stdout.strip()
    except OSError:
        commit = ""
    meta = {
        "built": time.strftime("%Y-%m-%d %H:%M"),
        "commit": commit,
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "md5": hashlib.md5(data).hexdigest(),
        # What the image will join and talk to - never the password or token.
        "wifi": secret(secrets, "WIFI_SSID"),
        "ha_host": secret(secrets, "HA_HOST"),
        "offset": 0,
    }
    (out / "homemesh_panel.json").write_text(json.dumps(meta, indent=1) + "\n", encoding="utf-8")
    print(f"[flash_image] {image.relative_to(project)}: {len(data)} bytes, for '{meta['wifi']}'")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", write_image)  # noqa: F821
