# Phantom Hourglass: the recompiled game (ph_game, from the player's ROM) and the program (PhantomHourglass).
set(PH_CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/config/azee)

# ---- Android: the game's C comes from a PC build (recomp_generated), compiled here for the phone ----
if(ANDROID)
  set(PH_GENERATED_DIR "" CACHE PATH "The recomp_generated folder of a PC build (2-BUILD.ps1)")
  if(NOT EXISTS "${PH_GENERATED_DIR}/func_table.c")
    message(FATAL_ERROR "PH_GENERATED_DIR must be a PC build's recomp_generated folder (run 2-BUILD.ps1 first)")
  endif()
  file(GLOB PH_GEN_SHARDS CONFIGURE_DEPENDS "${PH_GENERATED_DIR}/funcs_*.c")
  add_library(ph_game SHARED ${PH_GENERATED_DIR}/func_table.c ${PH_GEN_SHARDS})
  target_include_directories(ph_game PRIVATE ${PH_GENERATED_DIR} ${CMAKE_SOURCE_DIR}/runtime/include)
  target_compile_definitions(ph_game PRIVATE NTR_GAME_MODULE=1)
  set_target_properties(ph_game PROPERTIES C_VISIBILITY_PRESET hidden)
  target_compile_options(ph_game PRIVATE -w -ffreestanding -fno-stack-protector -O2)
  target_link_options(ph_game PRIVATE -nostdlib)

  # SDL's Android activity loads the program as libmain.so and calls its SDL_main.
  add_library(main SHARED src/main.cpp src/game_module.cpp src/ph_hooks.cpp src/png.cpp src/android_bridge.cpp
              src/gl_present.cpp src/gles_compat.cpp)
  target_include_directories(main PRIVATE src)
  target_compile_options(main PRIVATE -O2)
  target_link_libraries(main PRIVATE ntr_runtime ntr_common SDL3::SDL3-static android log
                        "$<LINK_GROUP:RESCAN,ntr_runtime,melonds_core,melonds_net>")
  add_dependencies(main ph_game)
  return()
endif()

# ---- the recompiled game: built only with PH_ROM (it is generated from the player's own ROM, never committed) ----
if(PH_ROM)
  set(PH_GEN_DIR ${CMAKE_BINARY_DIR}/recomp_generated)
  # ntr-recomp writes func_table.c and 64 shards funcs_000.c .. funcs_063.c (plus funcs.h).
  set(PH_GEN_SOURCES ${PH_GEN_DIR}/func_table.c)
  foreach(i RANGE 63)
    if(i LESS 10)
      set(n "00${i}")
    else()
      set(n "0${i}")
    endif()
    list(APPEND PH_GEN_SOURCES ${PH_GEN_DIR}/funcs_${n}.c)
  endforeach()

  add_custom_command(
    OUTPUT ${PH_GEN_SOURCES} ${PH_GEN_DIR}/funcs.h
    COMMAND ntr-recomp --rom "${PH_ROM}" --config "${PH_CONFIG}" --out "${PH_GEN_DIR}"
            --report "${CMAKE_BINARY_DIR}/recomp_report.txt"
    DEPENDS ntr-recomp ${PH_CONFIG}/functions.txt ${PH_CONFIG}/hooks.txt ${PH_CONFIG}/regions.txt "${PH_ROM}"
    COMMENT "Recompiling the game's code from your ROM"
    VERBATIM)

  # A freestanding shared library: the runtime's services reach it through a table when it is loaded (ntr/recomp.h).
  add_library(ph_game MODULE ${PH_GEN_SOURCES})
  target_include_directories(ph_game PRIVATE ${PH_GEN_DIR} ${CMAKE_SOURCE_DIR}/runtime/include)
  target_compile_definitions(ph_game PRIVATE NTR_GAME_MODULE=1)
  set_target_properties(ph_game PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    C_VISIBILITY_PRESET hidden)
  set(PH_GAME_OPT "" CACHE STRING "Optimization flag for the recompiled game (default /O1 or -O1)")
  if(MSVC)
    if(NOT PH_GAME_OPT)
      set(PH_GAME_OPT /O1)
    endif()
    target_compile_options(ph_game PRIVATE /w /GS- /Zl ${PH_GAME_OPT})
    target_link_options(ph_game PRIVATE /NODEFAULTLIB /NOENTRY)
  else()
    if(NOT PH_GAME_OPT)
      set(PH_GAME_OPT -O1)
    endif()
    target_compile_options(ph_game PRIVATE -w -ffreestanding -fno-stack-protector ${PH_GAME_OPT})
    target_link_options(ph_game PRIVATE -nostdlib)
  endif()
else()
  message(STATUS "PH_ROM not set: building the program only (it needs ph_game next to it to play)")
endif()

# ---- the program ----
add_executable(PhantomHourglass src/main.cpp src/game_module.cpp src/ph_hooks.cpp src/png.cpp src/gl_present.cpp src/menu.cpp)
target_include_directories(PhantomHourglass PRIVATE src)
set_target_properties(PhantomHourglass PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
if(PH_ROM)
  add_dependencies(PhantomHourglass ph_game)  # (built together, next to the program)
endif()
target_link_libraries(PhantomHourglass PRIVATE ntr_runtime ntr_common SDL3::SDL3-static)
if(PH_GLES_SHADERS)  # (the gles-test preset: the phone's renderer, its ES shaders and gles_compat, on the PC)
  target_sources(PhantomHourglass PRIVATE src/gles_compat.cpp)
  target_compile_definitions(PhantomHourglass PRIVATE PH_GLES_TEST=1)
endif()
if(NOT MSVC)
  # melonDS calls the Platform:: functions ntr_runtime defines and ntr_runtime calls melonDS: rescan them (GNU ld).
  target_link_libraries(PhantomHourglass PRIVATE "$<LINK_GROUP:RESCAN,ntr_runtime,melonds_core,melonds_net>")
endif()
if(WIN32)
  enable_language(RC)
  target_sources(PhantomHourglass PRIVATE app.rc)
  set_source_files_properties(app.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/res/app.ico)
  target_link_libraries(PhantomHourglass PRIVATE opengl32 user32 advapi32 shell32 ws2_32 iphlpapi)
  # A windowed program (no console window behind it); its output still goes to a redirected stdout/stderr.
  if(MSVC)
    target_link_options(PhantomHourglass PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
  else()
    target_link_options(PhantomHourglass PRIVATE -mwindows)
  endif()
endif()
