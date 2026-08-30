#!/usr/bin/env python3
"""RexOS böngésző DOM-stressz teszt.

Ez a teszt annak a hibának az őre, amit a dobozfa-újraépítés bevezetése okozott:
az újraépítés szakaszosan fut, és amíg tart, a böngésző többi része (rajzolás,
egér, billentyűzet, kijelölés) tovább használja a `html->layout`-ot. Ha az
újraépítés az elején eldobná a régi fát, ez az időszak felszabadított vagy NULL
mutatókkal telne -- a tünet "assertion box failed" a html_redraw-ban, illetve
véletlenszerű page fault.

A teszt ezt provokálja ki: nagy dokumentum + sok kép + folyamatos DOM-módosítás,
és közben billentyűleütések (a felhasználó is gépelés közben találkozott vele).

Használat:  tools/stress-dom.py [oldal] [masodperc]
Sikeres, ha nincs CPU exception és nincs assert.
"""
import json, os, socket, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PAGE = sys.argv[1] if len(sys.argv) > 1 else "big"
SECS = int(sys.argv[2]) if len(sys.argv) > 2 else 35
PORT = 8098
SOCK = f"/tmp/rexos-domstress-{os.getpid()}.sock"
LOG = f"{ROOT}/build/test_domstress.log"

srv = subprocess.Popen([sys.executable, "-m", "http.server", str(PORT), "--bind", "0.0.0.0"],
                       cwd=f"{ROOT}/testbench",
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
qemu = None
try:
    time.sleep(0.5)
    url = PAGE if PAGE.startswith("http") else f"http://10.0.2.2:{PORT}/{PAGE}.html"
    subprocess.run(["rm", "-f", f"{ROOT}/build/initrd_bench.tar",
                    f"{ROOT}/build/rexos-test-bench.iso"], check=True)
    subprocess.run(["make", "-C", ROOT, f"BENCH_URL={url}",
                    "build/rexos-test-bench.iso"],
                   check=True, stdout=subprocess.DEVNULL)

    qemu = subprocess.Popen([
        "qemu-system-x86_64", "-M", "q35", "-m", "1G",
        "-cdrom", f"{ROOT}/build/rexos-test-bench.iso", "-boot", "d",
        "-drive", f"id=disk0,file={ROOT}/disk.img,format=raw,if=none,snapshot=on",
        "-device", "ide-hd,bus=ide.0,drive=disk0", "-display", "none",
        "-qmp", f"unix:{SOCK},server,nowait", "-serial", f"file:{LOG}",
        "-netdev", "user,id=net0", "-device", "e1000,netdev=net0", "-no-reboot"])

    deadline = time.time() + 30
    while not os.path.exists(SOCK):
        if time.time() > deadline:
            sys.exit("HIBA: a QMP socket nem jott letre")
        time.sleep(0.1)
    time.sleep(0.5)

    sk = socket.socket(socket.AF_UNIX); sk.connect(SOCK)
    f = sk.makefile("rw")

    def cmd(c, **a):
        f.write(json.dumps({"execute": c, "arguments": a} if a else {"execute": c}) + "\n")
        f.flush()
        while True:
            line = f.readline()
            if not line:
                return None
            m = json.loads(line)
            if "return" in m or "error" in m:
                return m

    f.readline()
    cmd("qmp_capabilities")
    time.sleep(18)                       # boot + oldalbetöltés

    keys = list("abcdefghijklmnopqrstuvwxyz0123456789") + \
           ["ret", "backspace", "tab", "spc", "down", "up", "pgdn", "pgup"]
    t0 = time.time(); i = 0
    while time.time() - t0 < SECS:
        k = keys[i % len(keys)]; i += 1
        cmd("input-send-event", events=[{"type": "key", "data": {
            "down": True, "key": {"type": "qcode", "data": k}}}])
        cmd("input-send-event", events=[{"type": "key", "data": {
            "down": False, "key": {"type": "qcode", "data": k}}}])
        time.sleep(0.05)
    time.sleep(2)
    cmd("quit")
    try:
        qemu.wait(timeout=20)
    except Exception:
        qemu.kill()
finally:
    srv.terminate()
    if qemu is not None and qemu.poll() is None:
        qemu.kill()
    if os.path.exists(SOCK):
        os.remove(SOCK)

out = open(LOG, errors="replace").read()
exc = out.count("CPU EXCEPTION")
asrt = out.lower().count("assertion")
print(f"  leutesek: {i}, CPU exception: {exc}, assert: {asrt}")
if exc or asrt:
    for line in out.splitlines():
        low = line.lower()
        if "assertion" in low or "cr2" in line or line.startswith("  rip"):
            print("   ", line)
    print("== TEST domstress FAIL ==")
    sys.exit(1)
print("== TEST domstress PASS ==")
