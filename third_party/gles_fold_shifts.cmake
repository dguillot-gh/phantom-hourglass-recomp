# Folds constant unsigned shifts in GLSL source (`1u << 5`, `(1U<<6)`, `0x3FU << 8`) into their values (`32u`, ...).
# The 2018 Adreno 5xx driver (LG V30, Snapdragon 835: OpenGL ES 3.2 V@251.0) fails to compile a shader with any such
# constant shift, with an empty error log: melonDS's 2D compositor and 29 of its 33 compute shaders. Written out as
# numbers they compile there and mean exactly the same everywhere (desktop GLSL included). Shifts by a variable
# (`1u << bg`) are left alone: that driver compiles those. Found with nds-recomp-manager tools/phone-survey/shaderprobe.
#
# gles_fold_const_shifts(<variable>): rewrites the text in <variable> (in the caller's scope).

function(gles_fold_const_shifts var)
  set(t "${${var}}")
  # (a character before the number keeps identifiers like `x1u` out; the shift count is a plain number)
  set(re "([^A-Za-z0-9_])(0x[0-9A-Fa-f]+|[0-9]+)[uU] *<< *([0-9]+)")
  set(n 0)
  string(REGEX MATCH "${re}" m "${t}")
  while(m)  # (not while(TRUE): in script mode without a policy version that is never true)
    set(lead "${CMAKE_MATCH_1}")
    math(EXPR v "(${CMAKE_MATCH_2} << ${CMAKE_MATCH_3}) & 0xFFFFFFFF" OUTPUT_FORMAT DECIMAL)
    string(FIND "${t}" "${m}" at)
    string(LENGTH "${m}" len)
    string(SUBSTRING "${t}" 0 ${at} before)
    math(EXPR after_at "${at} + ${len}")
    string(SUBSTRING "${t}" ${after_at} -1 after)
    set(t "${before}${lead}${v}u${after}")
    math(EXPR n "${n} + 1")
    string(REGEX MATCH "${re}" m "${t}")
  endwhile()
  set(${var} "${t}" PARENT_SCOPE)
endfunction()
