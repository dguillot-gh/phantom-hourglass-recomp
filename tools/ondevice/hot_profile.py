#!/usr/bin/env python3
"""Makes frontend/config/<code>/hot.txt: the game functions that take most of the CPU time, from a simpleperf profile
of the running game on a phone (a debuggable build, e.g. the test app). Only function names (fn_main_02000078 ...)
are written, like functions.txt: nothing from the game. The on-device build puts them in hot_*.c and compiles just
those again with clang -O2 (GameOptimizer.java).

  python tools/ondevice/hot_profile.py <adb exe> <serial> <package> <seconds> <out hot.txt> [coverage 0.99]

Play (or run a benchmark) while it records. Several runs over different scenes can be merged by passing an existing
hot.txt as <out>: names already there are kept.
"""
import os, re, subprocess, sys

adb, serial, pkg, secs, out = sys.argv[1:6]
coverage = float(sys.argv[6]) if len(sys.argv) > 6 else 0.99
env = dict(os.environ, MSYS_NO_PATHCONV="1")


def sh(cmd):
    return subprocess.run([adb, "-s", serial, "shell", cmd], capture_output=True, text=True, env=env).stdout


data = "/data/local/tmp/ph-hot.data"
print(sh(f"simpleperf record --app {pkg} -e cpu-clock -f 2000 --duration {secs} -o {data} 2>&1 | tail -2"))
rep = sh(f"simpleperf report -i {data} --sort dso,symbol --percent-limit 0 2>/dev/null")
rows, game_total = [], 0.0
for line in rep.splitlines():
    m = re.match(r"\s*([\d.]+)%\s+\S*\s+(\S+)\s+(fn_\w+)", line)
    if m and "libph_game" in m.group(2):
        rows.append((float(m.group(1)), m.group(3)))
        game_total += float(m.group(1))
total_line = [l for l in rep.splitlines() if l.startswith("Event count") or l.startswith("Samples")]
rows.sort(reverse=True)
picked, acc = [], 0.0
for pct, name in rows:
    if acc >= coverage * game_total:
        break
    picked.append(name)
    acc += pct
old = set()
if os.path.exists(out):
    old = {l.strip() for l in open(out) if l.strip() and not l.startswith("#")}
names = sorted(old | set(picked))
with open(out, "w", newline="\n") as f:
    f.write("# Hot functions (C names): compiled again with clang -O2 on the device. From simpleperf profiles\n"
            "# (tools/ondevice/hot_profile.py). Facts only: no game data.\n")
    f.write("".join(n + "\n" for n in names))
print(f"game module: {game_total:.1f}% of the app's CPU samples; {len(picked)} functions cover {acc:.1f}% "
      f"({coverage:.0%} of it); {len(names)} names in {out}")
print("\n".join(total_line))
