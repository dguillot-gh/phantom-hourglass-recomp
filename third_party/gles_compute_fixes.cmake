# GLSL ES 3.2 fixes for melonDS's compute renderer shaders (GPU3D_Compute_shaders.h), for the phone. Desktop GLSL
# converts int to uint by itself (an int beside a uint in +, <, ==, ...; an int assigned to a uint; an int argument for a
# uint parameter); GLSL ES does not, and the Pixel 8's Mali driver lacks GL_EXT_shader_implicit_conversions. Each fix
# below writes out the conversion desktop GLSL makes, so the shaders mean the same everywhere (the PC compiles the fixed
# ones too). Every occurrence of `from` becomes `to`; a `from` that is gone (melonDS changed) stops the build.
#
# Applied to the variable gpu3d_compute_sh by third_party/CMakeLists.txt; tools/check_gles_compute.py runs it on its own
# and compiles the 33 variants with the NDK's glslc as GLSL ES 3.20.

function(gles_compute_fix from to)
  string(FIND "${gpu3d_compute_sh}" "${from}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "gles_compute_fixes.cmake: GPU3D_Compute_shaders.h changed upstream: cannot find: ${from}")
  endif()
  string(REPLACE "${from}" "${to}" fixed "${gpu3d_compute_sh}")
  set(gpu3d_compute_sh "${fixed}" PARENT_SCOPE)
endfunction()

gles_compute_fix("const uint startTable[256] = uint[256]("
  "const int startTable[256] = int[256](")
gles_compute_fix("uint t = startTable[ty - 256] + 256;"
  "uint t = uint(startTable[ty - 256U]) + 256U;")
gles_compute_fix("uint k = 31 - findMSB(y);"
  "uint k = uint(31 - findMSB(y));")
gles_compute_fix("uint z = (t << (32 - 9)) >> (32 - k - 1);"
  "uint z = (t << (32 - 9)) >> (32U - k - 1U);")
gles_compute_fix("uint my = 0 - y;"
  "uint my = 0U - y;")
gles_compute_fix("q = q + 1;"
  "q = q + 1U;")
gles_compute_fix("uint shift = 31 - findMSB(den);"
  "uint shift = uint(31 - findMSB(den));")
gles_compute_fix("qhat -= (c1 - c2 > den) ? 2 : 1;"
  "qhat -= (c1 - c2 > den) ? 2U : 1U;")
gles_compute_fix("mulLo = uaddCarry(mulLo, 3<<24, carry);"
  "mulLo = uaddCarry(mulLo, 3U<<24, carry);")
gles_compute_fix("    if (z0 == z1)\n        return z0;"
  "    if (z0 == z1)\n        return uint(z0);")
gles_compute_fix("if (disp > 0x3FF)"
  "if (disp > 0x3FFU)")
gles_compute_fix("umulExtended(disp * factor, abs(irecip) >> 8, mulHi, mulLo);"
  "umulExtended(disp * factor, uint(abs(irecip) >> 8), mulHi, mulLo);")
gles_compute_fix("return uint(z0) + (((z1-z0) * ifactor) >> YFactorShift);"
  "return uint(z0) + uint(((z1-z0) * ifactor) >> YFactorShift);")
gles_compute_fix("return uint(z1) + (((z0-z1) * ((1<<YFactorShift)-ifactor)) >> YFactorShift);"
  "return uint(z1) + uint(((z0-z1) * ((1<<YFactorShift)-ifactor)) >> YFactorShift);")
gles_compute_fix("umulExtended(z1-z0, ifactor, mulHi, mulLo);"
  "umulExtended(uint(z1-z0), uint(ifactor), mulHi, mulLo);")
gles_compute_fix("umulExtended(z0-z1, (1<<YFactorShift)-ifactor, mulHi, mulLo);"
  "umulExtended(uint(z0-z1), uint((1<<YFactorShift)-ifactor), mulHi, mulLo);")
gles_compute_fix("numHi |= numLo >> (32U-YFactorShift);"
  "numHi |= numLo >> uint(32-YFactorShift);")
gles_compute_fix("if (den == 0)"
  "if (den == 0U)")
gles_compute_fix("+ uint(abs(span.I1 - span.I0 - i)) * span.W1d;"
  "+ uint(abs(span.I1 - span.I0 - i)) * uint(span.W1d);")
gles_compute_fix("const uint WorkDescsUnsortedStart = 0;"
  "const uint WorkDescsUnsortedStart = 0U;")
gles_compute_fix("const uint WorkDescsSortedStart = WorkDescsUnsortedStart+MaxWorkTiles;"
  "const uint WorkDescsSortedStart = WorkDescsUnsortedStart+uint(MaxWorkTiles);")
gles_compute_fix("const uint ResultColorStart = 0;"
  "const uint ResultColorStart = 0U;")
gles_compute_fix("ResultColorStart+ScreenWidth*ScreenHeight*2;"
  "ResultColorStart+uint(ScreenWidth*ScreenHeight*2);")
gles_compute_fix("ResultDepthStart+ScreenWidth*ScreenHeight*2;"
  "ResultDepthStart+uint(ScreenWidth*ScreenHeight*2);")
gles_compute_fix("bool fillAllEdges = polyalpha < 31 ||"
  "bool fillAllEdges = polyalpha < 31U ||")
gles_compute_fix("BinningMaskAndOffset[BinningCoarseMaskStart + gl_GlobalInvocationID.x*CoarseBinStride+0] = 0;"
  "BinningMaskAndOffset[uint(BinningCoarseMaskStart) + gl_GlobalInvocationID.x*uint(CoarseBinStride)+0U] = 0U;")
gles_compute_fix("BinningMaskAndOffset[BinningCoarseMaskStart + gl_GlobalInvocationID.x*CoarseBinStride+1] = 0;"
  "BinningMaskAndOffset[uint(BinningCoarseMaskStart) + gl_GlobalInvocationID.x*uint(CoarseBinStride)+1U] = 0U;")
gles_compute_fix("if (polygonIdx < NumPolygons)"
  "if (uint(polygonIdx) < NumPolygons)")
gles_compute_fix("uint tilePositionCombined = bitfieldInsert(fineTileTopLeft.x, fineTileTopLeft.y, 16, 16);"
  "uint tilePositionCombined = uint(bitfieldInsert(fineTileTopLeft.x, fineTileTopLeft.y, 16, 16));")
gles_compute_fix("atomicAdd(VariantWorkCount[variantIdx].z, 1)"
  "atomicAdd(VariantWorkCount[variantIdx].z, 1U)")
gles_compute_fix("WorkDescs[WorkDescsUnsortedStart + workOffset + idx]"
  "WorkDescs[WorkDescsUnsortedStart + workOffset + uint(idx)]")
gles_compute_fix("if (gl_GlobalInvocationID.x == 0)"
  "if (gl_GlobalInvocationID.x == 0U)")
gles_compute_fix("uvec4((VariantWorkCount[0].w + 31) / 32, 1, 1, 0)"
  "uvec4((VariantWorkCount[0].w + 31U) / 32U, 1, 1, 0)")
gles_compute_fix("WorkDescs[WorkDescsSortedStart + sortedIndex]"
  "WorkDescs[WorkDescsSortedStart + uint(sortedIndex)]")
gles_compute_fix("            uint attr = 0;\n"
  "            uint attr = 0U;\n")
gles_compute_fix("                z = xspan.Z0;\n"
  "                z = uint(xspan.Z0);\n")
gles_compute_fix("            a = int(polyalpha);\n"
  "            a = polyalpha;\n")
gles_compute_fix("            r = vr;\n            g = vg;\n            b = vb;\n"
  "            r = uint(vr);\n            g = uint(vg);\n            b = uint(vb);\n")
gles_compute_fix("if (texcolor.a == 31)"
  "if (texcolor.a == 31U)")
gles_compute_fix("                r = int(texcolor.r);\n                g = int(texcolor.g);\n                b = int(texcolor.b);\n"
  "                r = texcolor.r;\n                g = texcolor.g;\n                b = texcolor.b;\n")
gles_compute_fix("else if (texcolor.a > 0)"
  "else if (texcolor.a > 0U)")
gles_compute_fix("r = int((texcolor.r * texcolor.a) + (vr * (31-texcolor.a))) >> 5;"
  "r = uint(int((texcolor.r * texcolor.a) + (uint(vr) * (31U-texcolor.a))) >> 5);")
gles_compute_fix("g = int((texcolor.g * texcolor.a) + (vg * (31-texcolor.a))) >> 5;"
  "g = uint(int((texcolor.g * texcolor.a) + (uint(vg) * (31U-texcolor.a))) >> 5);")
gles_compute_fix("b = int((texcolor.b * texcolor.a) + (vb * (31-texcolor.a))) >> 5;"
  "b = uint(int((texcolor.b * texcolor.a) + (uint(vb) * (31U-texcolor.a))) >> 5);")
gles_compute_fix("r = int((texcolor.r+1) * (vr+1) - 1) >> 6;"
  "r = uint(int((texcolor.r+1U) * uint(vr+1) - 1U) >> 6);")
gles_compute_fix("g = int((texcolor.g+1) * (vg+1) - 1) >> 6;"
  "g = uint(int((texcolor.g+1U) * uint(vg+1) - 1U) >> 6);")
gles_compute_fix("b = int((texcolor.b+1) * (vb+1) - 1) >> 6;"
  "b = uint(int((texcolor.b+1U) * uint(vb+1) - 1U) >> 6);")
gles_compute_fix("a = int((texcolor.a+1) * (polyalpha+1) - 1) >> 5;"
  "a = uint(int((texcolor.a+1U) * (polyalpha+1U) - 1U) >> 5);")
gles_compute_fix("r = min(r + int(bitfieldExtract(tooncolor, 0, 8)), 63);"
  "r = min(r + bitfieldExtract(tooncolor, 0, 8), 63U);")
gles_compute_fix("g = min(g + int(bitfieldExtract(tooncolor, 8, 8)), 63);"
  "g = min(g + bitfieldExtract(tooncolor, 8, 8), 63U);")
gles_compute_fix("b = min(b + int(bitfieldExtract(tooncolor, 16, 8)), 63);"
  "b = min(b + bitfieldExtract(tooncolor, 16, 8), 63U);")
gles_compute_fix("            if (polyalpha == 0)\n                a = 31;\n"
  "            if (polyalpha == 0U)\n                a = 31U;\n")
gles_compute_fix("if (a > AlphaRef)"
  "if (a > uint(AlphaRef))")
gles_compute_fix("color = 0xFFFFFFFF; //"
  "color = 0xFFFFFFFFU; //")
gles_compute_fix("if ((attr & (1U<<15)) == 0)"
  "if ((attr & (1U<<15)) == 0U)")
gles_compute_fix("uint alpha = (srcA >> 24) + 1;"
  "uint alpha = (srcA >> 24) + 1U;")
gles_compute_fix("if (dstA != 0)"
  "if (dstA != 0U)")
gles_compute_fix("(32-alpha)"
  "(32U-alpha)")
gles_compute_fix("uint coarseBit = findLSB(coarseMask);"
  "uint coarseBit = uint(findLSB(coarseMask));")
gles_compute_fix("uint tileOffset = linearTile * BinStride + coarseBit + coarseOffset;"
  "uint tileOffset = uint(linearTile * BinStride) + coarseBit + coarseOffset;")
gles_compute_fix("BinningMaskAndOffset[BinningMaskStart + tileOffset]"
  "BinningMaskAndOffset[uint(BinningMaskStart) + tileOffset]")
gles_compute_fix("BinningMaskAndOffset[BinningWorkOffsetsStart + tileOffset]"
  "BinningMaskAndOffset[uint(BinningWorkOffsetsStart) + tileOffset]")
gles_compute_fix("uint fineIdx = findLSB(fineMask);"
  "uint fineIdx = uint(findLSB(fineMask));")
gles_compute_fix("uint pixelindex = tileInnerOffset + workIdx * TileSize * TileSize;"
  "uint pixelindex = uint(tileInnerOffset) + workIdx * uint(TileSize * TileSize);")
gles_compute_fix("uint polygonIdx = fineIdx + (coarseBit + coarseOffset) * 32;"
  "uint polygonIdx = fineIdx + (coarseBit + coarseOffset) * 32U;")
gles_compute_fix("+ 0x200 <= 0x400"
  "+ 0x200U <= 0x400U")
gles_compute_fix("+ 0xFFU <= 0x1FE"
  "+ 0xFFU <= 0x1FEU")
gles_compute_fix("bool writeDepth = (polygonAttr & (1U<<11)) != 0;"
  "bool writeDepth = (polygonAttr & (1U<<11)) != 0U;")
gles_compute_fix("stencil = 0;\n"
  "stencil = 0U;\n")
gles_compute_fix("int linearTile = int(gl_WorkGroupID.x + (gl_WorkGroupID.y * TilesPerLine));"
  "int linearTile = int(gl_WorkGroupID.x + (gl_WorkGroupID.y * uint(TilesPerLine)));")
gles_compute_fix("if ((DispCnt & (1<<14)) != 0U)"
  "if ((DispCnt & (1U<<14)) != 0U)")
gles_compute_fix("float scale = 1.0 / ScreenWidth;"
  "float scale = 1.0 / float(ScreenWidth);")
gles_compute_fix("ProcessCoarseMask(linearTile, coarseMaskLo, 0, color,"
  "ProcessCoarseMask(linearTile, coarseMaskLo, 0U, color,")
gles_compute_fix("ProcessCoarseMask(linearTile, coarseMaskHi, BinStride/2, color,"
  "ProcessCoarseMask(linearTile, coarseMaskHi, uint(BinStride/2), color,")
gles_compute_fix("+resultOffset+FramebufferStride]"
  "+uint(resultOffset+FramebufferStride)]")
gles_compute_fix("+resultOffset]"
  "+uint(resultOffset)]")
gles_compute_fix("uint densityid = 0, densityfrac = 0;"
  "uint densityid = 0U, densityfrac = 0U;")
gles_compute_fix("if (densityid >= 32)"
  "if (densityid >= 32U)")
gles_compute_fix("            densityid = 32;\n            densityfrac = 0;\n"
  "            densityid = 32U;\n            densityfrac = 0U;\n")
gles_compute_fix("ToonTable[densityid+1]"
  "ToonTable[densityid+1U]")
gles_compute_fix("(128-density)"
  "(128U-density)")
gles_compute_fix("return (DispCnt & (1U<<6)) != 0\n"
  "return (DispCnt & (1U<<6)) != 0U\n")
gles_compute_fix("[resultOffset+FramebufferStride+"
  "[uint(resultOffset+FramebufferStride)+")
gles_compute_fix("[resultOffset+Result"
  "[uint(resultOffset)+Result")
gles_compute_fix("[resultOffset-1+"
  "[uint(resultOffset-1)+")
gles_compute_fix("[resultOffset+1+"
  "[uint(resultOffset+1)+")
gles_compute_fix("[resultOffset-ScreenWidth+"
  "[uint(resultOffset-ScreenWidth)+")
gles_compute_fix("[resultOffset+ScreenWidth+"
  "[uint(resultOffset+ScreenWidth)+")
gles_compute_fix("if (srcX > 0U)"
  "if (srcX > 0)")
gles_compute_fix("if (gl_GlobalInvocationID.y < ScreenHeight-1)"
  "if (gl_GlobalInvocationID.y < uint(ScreenHeight-1))")
gles_compute_fix("if ((attr.x & 0xFU) != 0 &&"
  "if ((attr.x & 0xFU) != 0U &&")
gles_compute_fix("if ((attr.x & 0x3U) != 0)"
  "if ((attr.x & 0x3U) != 0U)")
gles_compute_fix("if (coverage != 0)"
  "if (coverage != 0U)")
gles_compute_fix("if (botA > 0)"
  "if (botA > 0U)")
gles_compute_fix("(32-coverage)"
  "(32U-coverage)")

# Mali (the Pixel 8's driver) will not pass a struct straight out of a readonly buffer to a function ("Function call
# discards 'readonly' access qualifier"): BinCombined copies the polygon first, as the other shaders already do.
gles_compute_fix("        binned = BinPolygon(Polygons[polygonIdx], coarseTopLeft, coarseBotRight);"
  "        Polygon polygon = Polygons[polygonIdx];\n        binned = BinPolygon(polygon, coarseTopLeft, coarseBotRight);")
gles_compute_fix("        if (BinPolygon(Polygons[polygonIdx], fineTileTopLeft, fineTileBotRight))"
  "        Polygon polygon = Polygons[polygonIdx];\n        if (BinPolygon(polygon, fineTileTopLeft, fineTileBotRight))")

# BinCombined shares mergedMaskShared between its invocations. In GLSL (ES too) barrier() only syncs execution: the
# writes to a shared variable are made visible to the others by memoryBarrierShared() first. Desktop drivers order them
# anyway; a tile-based phone GPU need not (the 3D came out in scrambled tiles on the Pixel 8).
gles_compute_fix("    barrier();\n"
  "    memoryBarrierShared();\n    barrier();\n")
