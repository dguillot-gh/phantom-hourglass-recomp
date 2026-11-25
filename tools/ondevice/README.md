# On-device build spike (TinyCC)

Proves the phone can build the game module from the recompiler's C output, with no PC.

- tcc (TinyCC, repo.or.cz/tinycc mob e9e9e7f) cross-built with the NDK:
  configure --cpu=aarch64 --targetos=Android; CONFIG_TCC_PREDEFS 0; clang --target=aarch64-linux-android26 -DONE_SOURCE=1 tcc.c
- stdint.h / string.h here: the only libc headers the generated code needs (plus tcc's own include/).
- On the phone: tcc -c -w -DNTR_GAME_MODULE=1 -fPIC -Igen -Irt -Iinclude each file;
  tcc -shared -nostdlib -o libph_game.so obj/*.o /system/lib64/libc.so (memcpy etc. from libc).

Pixel 8 (2026-11-24): 65 files / 102 MB C in 10.8 s single-threaded, link 0.65 s, module 89 MB; game runs 59.8 fps.
