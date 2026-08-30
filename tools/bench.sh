#!/usr/bin/env bash
# =============================================================================
#  RexOS böngésző-mérőpad
#
#  A testbench/ könyvtárat kiszolgálja egy helyi HTTP szerver, a RexOS-t
#  headless QEMU-ban felbootolja a megadott tesztoldallal, majd a megadott
#  idő után képernyőképet ment.  Használat:  tools/bench.sh images [varakozas]
# =============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PAGE="${1:-index}"
WAIT="${2:-45}"
PORT=8099
OUT="$ROOT/build/bench_$PAGE.png"

cd "$ROOT/testbench"
python3 -m http.server "$PORT" --bind 0.0.0.0 >/dev/null 2>&1 &
SRV=$!
trap 'kill $SRV 2>/dev/null || true' EXIT
sleep 0.5

cd "$ROOT"
# a BENCH_URL nem függősége a tarnak, ezért kézzel dobjuk el
rm -f build/initrd_bench.tar build/rexos-test-bench.iso
make BENCH_URL="http://10.0.2.2:$PORT/$PAGE.html" build/rexos-test-bench.iso >/dev/null

python3 - "$ROOT" "$PAGE" "$WAIT" "$OUT" <<'PYEOF'
import json, os, socket, subprocess, sys, time
ROOT, PAGE, WAIT, OUT = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
SOCK = f"/tmp/rexos-bench-{os.getpid()}.sock"
q = subprocess.Popen([
    "qemu-system-x86_64", "-M", "q35", "-m", "1G",
    "-cdrom", f"{ROOT}/build/rexos-test-bench.iso", "-boot", "d",
    "-drive", f"id=disk0,file={ROOT}/disk.img,format=raw,if=none,snapshot=on",
    "-device", "ide-hd,bus=ide.0,drive=disk0",
    "-display", "none", "-qmp", f"unix:{SOCK},server,nowait",
    "-serial", f"file:{ROOT}/build/bench_{PAGE}.log",
    "-netdev", "user,id=net0", "-device", "e1000,netdev=net0", "-no-reboot"])
while not os.path.exists(SOCK): time.sleep(0.1)
time.sleep(0.5)
s = socket.socket(socket.AF_UNIX); s.connect(SOCK); f = s.makefile("rw")
def cmd(c, **a):
    f.write(json.dumps({"execute": c, "arguments": a} if a else {"execute": c}) + "\n"); f.flush()
    while True:
        l = f.readline()
        if not l: return None
        m = json.loads(l)
        if "return" in m or "error" in m: return m
f.readline(); cmd("qmp_capabilities")
time.sleep(WAIT)
ppm = OUT.replace(".png", ".ppm")
cmd("screendump", filename=ppm); time.sleep(1.5)
cmd("quit")
try: q.wait(timeout=20)
except Exception: q.kill()
try: os.remove(SOCK)
except OSError: pass
subprocess.run(["magick", ppm, OUT], check=True)
os.remove(ppm)
print("kep:", OUT)
PYEOF
