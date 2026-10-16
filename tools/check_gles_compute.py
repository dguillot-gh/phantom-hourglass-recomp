"""Checks melonDS's compute renderer shaders as OpenGL ES 3.2 (the phone's compute renderer) with the NDK's glslc.
GLSL ES lacks desktop GLSL's implicit int-to-uint conversions, which these shaders use everywhere: the phone compiles them
with GL_EXT_shader_implicit_conversions (the runtime uses this renderer only when the driver has it), and so does this.

Reads the shaders from GPU3D_Compute_shaders.h (ours where third_party/CMakeLists.txt patches it, else melonDS's),
puts each of the 33 variants together the way ComputeRenderer3D::CompileShader does (version line, its #defines, the
tile constants for a scale, the Common part, the shader), with the ES header the renderer uses on the phone, and
compiles each one as GLSL ES 3.20. Nothing is built: this is the check that can run anywhere the NDK is.

    python tools/check_gles_compute.py <glslc.exe> [patched GPU3D_Compute_shaders.h] [scale]
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = os.path.join(ROOT, "third_party", "melonDS", "src", "GPU3D_Compute_shaders.h")

# The ES header (third_party/CMakeLists.txt puts the same in GPU3D_Compute.cpp on the phone).
ES_HEAD = """#version 320 es
#extension GL_EXT_shader_implicit_conversions : require
precision highp float;
precision highp int;
precision highp usampler2D;
precision highp usampler2DArray;
precision highp sampler2DArray;
precision highp uimageBuffer;
precision highp image2D;
"""

VARIANTS = [
    ("InterpSpans", ["InterpSpans", "ZBuffer"]), ("InterpSpans", ["InterpSpans", "WBuffer"]),
    ("BinCombined", ["BinCombined"]),
    ("DepthBlend", ["DepthBlend", "ZBuffer"]), ("DepthBlend", ["DepthBlend", "WBuffer"]),
] + [("Rasterise", ["Rasterise", buf] + extra)
     for extra in (["NoTexture"], ["NoTexture", "Toon"], ["NoTexture", "Highlight"], ["UseTexture", "Decal"],
                   ["UseTexture", "Modulate"], ["UseTexture", "Toon"], ["UseTexture", "Highlight"], ["ShadowMask"])
     for buf in ("ZBuffer", "WBuffer")] + [
    ("ClearCoarseBinMask", ["ClearCoarseBinMask"]), ("ClearIndirectWorkCount", ["ClearIndirectWorkCount"]),
    ("CalcOffsets", ["CalculateWorkOffsets"]), ("SortWork", ["SortWork"]),
] + [("FinalPass", ["FinalPass"] + extra)
     for extra in ([], ["EdgeMarking"], ["Fog"], ["EdgeMarking", "Fog"], ["AntiAliasing"],
                   ["AntiAliasing", "EdgeMarking"], ["AntiAliasing", "Fog"], ["AntiAliasing", "EdgeMarking", "Fog"])]


def parse(text):
    """The header's string constants: `const std::string X{R"(...)"};`, `const char* X = R"(...)";` and sums of them."""
    out = {}
    pos = 0
    decl = re.compile(r"const\s+(?:std::string|char\s*\*)\s+(\w+)\s*(=|\{)")
    while True:
        m = decl.search(text, pos)
        if not m:
            return out
        name, pos = m.group(1), m.end()
        value = ""
        while True:  # terms joined by +, up to the closing ; (or };)
            while text[pos] in " \t\r\n+":
                pos += 1
            if text.startswith('R"(', pos):
                end = text.index(')"', pos + 3)
                value += text[pos + 3:end]
                pos = end + 2
            elif text[pos] in ";}":
                pos = text.index(";", pos) + 1
                break
            else:
                word = re.match(r"\w+", text[pos:]).group(0)
                value += out[word]
                pos += len(word)
        out[name] = value


def constants(scale):
    """ComputeRenderer3D::SetRenderSettings's tile sizes for a scale."""
    tile_scale = 2 * scale // 9
    tile_scale = 1 << (tile_scale.bit_length() - 1) if tile_scale else 0
    tile_scale <<= 1
    tile_scale += tile_scale == 0
    tile = min(8 * tile_scale, 32)
    count_y = 4 if tile < 32 else 6
    w, h = 256 * scale, 192 * scale
    return {"ScreenWidth": w, "ScreenHeight": h, "MaxWorkTiles": (w // tile) * (h // tile) * 16, "TileSize": tile,
            "CoarseTileCountY": count_y, "CoarseTileArea": 8 * count_y,
            "ClearCoarseBinMaskLocalSize": 64 if tile < 32 else 48}


def assemble(parts, name, defines, c):
    s = ES_HEAD
    for d in defines:
        s += "#define %s\n" % d
    s += "#define ScreenWidth %d\n#define ScreenHeight %d\n#define MaxWorkTiles %d\n#define TileSize %d\n" % (
        c["ScreenWidth"], c["ScreenHeight"], c["MaxWorkTiles"], c["TileSize"])
    s += "const int CoarseTileCountY = %d;\n#define CoarseTileArea %d\n#define ClearCoarseBinMaskLocalSize %d" % (
        c["CoarseTileCountY"], c["CoarseTileArea"], c["ClearCoarseBinMaskLocalSize"])
    return s + parts["Common"] + parts[name]


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    glslc = sys.argv[1]
    header = sys.argv[2] if len(sys.argv) > 2 and sys.argv[2] != "-" else HEADER
    scale = int(sys.argv[3]) if len(sys.argv) > 3 else 4
    text = open(header, encoding="utf-8").read().replace("\r\n", "\n")
    for i, ch in enumerate("rgb"):  # (third_party/CMakeLists.txt's fixes, when checking melonDS's own header)
        text = text.replace("%s = min(%s + int(bitfieldExtract(tooncolor, %d, 8)), 63);" % (ch, ch, 8 * i),
                            "%s = min(%s + bitfieldExtract(tooncolor, %d, 8), 63U);" % (ch, ch, 8 * i))
    parts = parse(text)
    out = tempfile.mkdtemp(prefix="gles-compute-")
    failed = 0
    seen = {}
    for name, defines in VARIANTS:
        src = assemble(parts, name, defines, constants(scale))
        tag = "_".join(defines)
        path = os.path.join(out, tag + ".comp")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(src)
        c = subprocess.run([glslc, "--target-env=opengl", "-fauto-map-locations", "-fauto-bind-uniforms", "-o",
                            os.devnull, path], capture_output=True, text=True)
        if c.returncode:
            failed += 1
            lines = src.split("\n")
            print("== " + tag)
            if not re.search(r":(\d+): error: ", c.stderr):
                print("  " + c.stderr.strip().replace("\n", "\n  "))
            for m in re.finditer(r":(\d+): error: (.*)", c.stderr):
                n = int(m.group(1))
                key = (lines[n - 1].strip(), m.group(2))
                if key in seen:  # (the same line in the Common part fails in every variant: once is enough)
                    continue
                seen[key] = True
                print("  %4d: %-80s %s" % (n, lines[n - 1].strip()[:80], m.group(2)[:110]))
    print("%d of %d compute shaders compile as GLSL ES 3.20 (scale %d)" % (len(VARIANTS) - failed, len(VARIANTS), scale))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
