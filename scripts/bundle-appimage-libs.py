#!/usr/bin/env python3
"""Copy the shared-library closure of an AppDir into usr/lib.

Does not rewrite ELF headers. Fedora 44 ships RELR binaries, and the patchelf
inside linuxdeploy corrupts them. The launcher sets LD_LIBRARY_PATH instead.
Host libc and the GPU driver stay on the system.
"""
import os
import shutil
import subprocess
import sys

SKIP_PREFIXES = (
    "libc.so",
    "libm.so",
    "libdl.so",
    "libpthread.so",
    "librt.so",
    "libresolv.so",
    "ld-linux",
    "libGL.so",
    "libGLX.so",
    "libGLdispatch.so",
    "libEGL.so",
    "libOpenGL.so",
    "libdrm.so",
    "libgbm.so",
    "libgallium",
    "libnvidia",
)


def is_elf(path: str) -> bool:
    try:
        with open(path, "rb") as fh:
            return fh.read(4) == b"\x7fELF"
    except OSError:
        return False


def needed(path: str) -> list[str]:
    try:
        out = subprocess.check_output(["ldd", path], text=True, stderr=subprocess.DEVNULL)
    except subprocess.CalledProcessError:
        return []
    libs = []
    for line in out.splitlines():
        if "=>" not in line:
            continue
        soname = line.split("=>", 1)[0].strip().split()[0]
        if any(soname.startswith(prefix) for prefix in SKIP_PREFIXES):
            continue
        rest = line.split("=>", 1)[1].strip().split()
        if not rest or not rest[0].startswith("/"):
            continue
        libs.append(rest[0])
    return libs


def main() -> int:
    appdir = sys.argv[1]
    libdir = os.path.join(appdir, "usr", "lib")
    os.makedirs(libdir, exist_ok=True)
    queue: list[str] = []
    seen: set[str] = set()
    for dirpath, _, files in os.walk(os.path.join(appdir, "usr")):
        for name in files:
            path = os.path.join(dirpath, name)
            if is_elf(path) and path not in seen:
                seen.add(path)
                queue.append(path)
    while queue:
        elf = queue.pop()
        for src in needed(elf):
            dest = os.path.join(libdir, os.path.basename(src))
            if os.path.exists(dest):
                continue
            shutil.copy2(src, dest)
            if dest not in seen:
                seen.add(dest)
                queue.append(dest)
    print(f"bundled {len(os.listdir(libdir))} libraries into {libdir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
