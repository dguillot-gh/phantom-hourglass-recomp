"""Checks the OpenGL ES copies of melonDS's shaders (third_party/gles_shaders.cmake) with the Android NDK's glslc.

Makes the ES copies with CMake in script mode, then compiles each one (and the WBuffer variants of the 3D render shaders,
which the renderer makes by adding a #define after the first line) as GLSL ES 3.20. Nothing is built: this is the check
that can run anywhere the NDK is.

    python tools/check_gles_shaders.py <cmake.exe> <glslc.exe> [patched-shaders-dir]

The patched-shaders dir is a build's third_party/melonds_patched/OpenGL_shaders (our desktop patches); without it the
check uses melonDS's own shaders where we patch them.
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "third_party", "melonDS", "src", "OpenGL_shaders")


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    cmake, glslc = sys.argv[1], sys.argv[2]
    patched = sys.argv[3] if len(sys.argv) > 3 else os.path.join(tempfile.gettempdir(), "no-patched-shaders")
    out = tempfile.mkdtemp(prefix="gles-shaders-")
    r = subprocess.run([cmake, "-D", "GLES_SRC_DIR=" + SRC, "-D", "GLES_PATCHED_DIR=" + patched, "-D",
                        "GLES_OUT_DIR=" + out, "-P", os.path.join(ROOT, "third_party", "gles_shaders.cmake")],
                       capture_output=True, text=True)
    if r.returncode:
        print(r.stdout + r.stderr)
        return 1
    failed = 0
    names = sorted(f[:-5] for f in os.listdir(out) if f.endswith(".glsl"))  # (not the .glsl.tmp copies)
    for name in names:
        text = open(os.path.join(out, name + ".glsl"), encoding="utf-8").read()
        variants = [("", text)]
        if name.startswith("3DRender"):
            cut = text.index("\n") + 1
            variants.append((" (WBuffer)", text[:cut] + "#define WBuffer\n" + text[cut:]))
        for tag, src in variants:
            stage = "frag" if name.endswith("FS") else "vert"
            path = os.path.join(out, "%s%s.%s" % (name, "_w" if tag else "", stage))
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(src)
            # (Real ES drivers need no bindings for uniform blocks and samplers; SPIR-V does: let glslc assign them.)
            c = subprocess.run([glslc, "--target-env=opengl", "-fauto-map-locations", "-fauto-bind-uniforms", "-o",
                                os.devnull, path], capture_output=True, text=True)
            if c.returncode:
                failed += 1
                lines = src.split("\n")
                print("== %s%s" % (name, tag))
                if not re.search(r":(\d+): error: ", c.stderr):
                    print("  " + c.stderr.strip().replace("\n", "\n  "))
                for m in re.finditer(r":(\d+): error: (.*)", c.stderr):
                    n = int(m.group(1))
                    print("  %4d: %-70s %s" % (n, lines[n - 1].strip()[:70], m.group(2)[:90]))
    print("%d of %d shaders compile as GLSL ES 3.20" % (len(names) + 2 - failed, len(names) + 2))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
