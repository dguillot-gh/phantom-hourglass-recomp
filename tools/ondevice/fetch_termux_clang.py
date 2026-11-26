#!/usr/bin/env python3
"""Termux's arm64 clang + lld, packed as files an APK can carry in jniLibs/arm64-v8a (GameOptimizer.java).

Android runs an app's programs only from its native library folder, and packs only files named lib*.so there, so:
clang -> libclang_exe.so, lld -> liblld_exe.so, and the versioned library names they need (libz.so.1, ...) are
renamed in the files themselves (libz1.so, ...). The programs find their libraries through LD_LIBRARY_PATH.
Termux keeps only its newest packages: the versions used go to VERSION.txt. (The lean alternative, one clang+lld
binary for AArch64 only, is tools/ondevice/build_device_llvm.sh.)
  python3 tools/ondevice/fetch_termux_clang.py <out dir>
"""
import io, os, sys, tarfile, urllib.request

REPO = "https://packages.termux.dev/apt/termux-main/"
PKGS = ["clang", "lld", "libllvm", "libc++", "libffi", "libiconv", "libxml2", "zlib", "zstd"]
# file in the package (under data/data/com.termux/files/usr/) -> name in the APK
FILES = {
    "bin/clang-21": "libclang_exe.so", "bin/lld": "liblld_exe.so",
    "lib/libLLVM.so": "libLLVM.so", "lib/libclang-cpp.so": "libclang-cpp.so", "lib/libc++_shared.so": "libc++_shared.so",
    "lib/libffi.so": "libffi.so", "lib/libiconv.so": "libiconv.so",
    "lib/libz.so.1.4": "libz1.so", "lib/libzstd.so.1.6.0": "libzstd1.so", "lib/libxml2.so.16.1.4": "libxml2_16.so",
}
# library names inside the files (needed / soname): same length or shorter, padded with NULs
RENAME = {b"libz.so.1\0": b"libz1.so\0", b"libzstd.so.1\0": b"libzstd1.so\0", b"libxml2.so.16\0": b"libxml2_16.so\0"}
PREFIX = "data/data/com.termux/files/usr/"


def fetch(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "fetch-termux-clang"})) as r:
        return r.read()


def deb_data(deb):
    assert deb[:8] == b"!<arch>\n"
    p = 8
    while p < len(deb):
        name, size = deb[p:p + 16].decode().strip(), int(deb[p + 48:p + 58])
        if name.startswith("data.tar"):
            return deb[p + 60:p + 60 + size]
        p += 60 + size + (size & 1)
    raise RuntimeError("no data.tar")


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    index = fetch(REPO + "dists/stable/main/binary-aarch64/Packages").decode()
    pkgs = {}
    for block in index.split("\n\n"):
        d = dict(l.split(": ", 1) for l in block.splitlines() if ": " in l)
        if d.get("Package") in PKGS:
            pkgs[d["Package"]] = d
    missing = [p for p in PKGS if p not in pkgs]
    if missing:
        sys.exit(f"not in the Termux index: {missing}")
    found = {}
    for name in PKGS:
        data = deb_data(fetch(REPO + pkgs[name]["Filename"]))
        with tarfile.open(fileobj=io.BytesIO(data)) as t:
            for m in t.getmembers():
                rel = m.name.lstrip("./")
                rel = rel[len(PREFIX):] if rel.startswith(PREFIX) else None
                if rel in FILES and m.isfile():
                    found[rel] = t.extractfile(m).read()
    lost = [f for f in FILES if f not in found]
    if lost:
        sys.exit(f"missing from the packages (new versions?): {lost}")
    for rel, dst in FILES.items():
        b = found[rel]
        for old, new in RENAME.items():
            b = b.replace(old, new.ljust(len(old), b"\0"))
        with open(os.path.join(out, dst), "wb") as f:
            f.write(b)
    with open(os.path.join(out, "VERSION.txt"), "w") as f:
        f.write("Termux packages (aarch64): " + ", ".join(f"{p} {pkgs[p]['Version']}" for p in PKGS) + "\n")
    print(open(os.path.join(out, "VERSION.txt")).read().strip())


if __name__ == "__main__":
    main()
