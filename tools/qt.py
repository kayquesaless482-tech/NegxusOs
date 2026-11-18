#!/usr/bin/env python3
"""Helper de teste: executa o NegxusOS no QEMU, envia teclas/mouse e tira capturas."""
import subprocess, socket, time, os, sys
from PIL import Image

class QEMU:
    def __init__(self, img="build/negxusos.img", net=True, extra=None, mem=512):
        for f in ("/tmp/mon.sock", "/tmp/serial.log"):
            try: os.remove(f)
            except OSError: pass
        args = ["qemu-system-x86_64", "-m", str(mem), "-drive", "format=raw,file=" + img,
                "-display", "none", "-monitor", "unix:/tmp/mon.sock,server,nowait",
                "-serial", "file:/tmp/serial.log", "-no-reboot"]
        if net: args += ["-nic", "user,model=e1000"]
        args += (extra or [])
        self.p = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        for _ in range(50):
            if os.path.exists("/tmp/mon.sock"): break
            time.sleep(0.1)
        self.s = socket.socket(socket.AF_UNIX); self.s.connect("/tmp/mon.sock"); self.s.settimeout(5)
        time.sleep(0.3); self._drain()
    def _drain(self):
        self.s.settimeout(0.05)
        try:
            while self.s.recv(65536): pass
        except Exception: pass
        self.s.settimeout(5)
    def cmd(self, c, wait=0.15):
        self.s.sendall((c + "\n").encode()); time.sleep(wait); self._drain()
    def shot(self, name="/tmp/shot.png"):
        self.cmd("screendump /tmp/s.ppm", 0.6)
        Image.open("/tmp/s.ppm").convert("RGB").save(name)
        return name
    def key(self, k, wait=0.1): self.cmd("sendkey " + k, wait)
    def type(self, text, wait=0.05):
        for ch in text:
            m = {" ": "spc", ".": "dot", ",": "comma", "/": "slash", "-": "minus", ":": "shift-semicolon" if False else "shift-semicolon"}
            self.key(m.get(ch, ch), wait)
    def move(self, dx, dy): self.cmd("mouse_move %d %d" % (dx, dy), 0.05)
    def goto(self, x, y):
        # PS/2 relativo: vai ao canto e anda ate o ponto
        for _ in range(12): self.cmd("mouse_move -400 -400", 0.02)
        while x > 0 or y > 0:
            sx, sy = min(x, 100), min(y, 100)
            self.cmd("mouse_move %d %d" % (sx, sy), 0.03); x -= sx; y -= sy
    def btn(self, b=1): self.cmd("mouse_button %d" % b, 0.08); self.cmd("mouse_button 0", 0.08)
    def down(self, b=1): self.cmd("mouse_button %d" % b, 0.08)
    def up(self): self.cmd("mouse_button 0", 0.08)
    def serial(self): 
        try: return open("/tmp/serial.log", errors="replace").read()
        except OSError: return ""
    def stop(self):
        try: self.s.sendall(b"quit\n")
        except Exception: pass
        time.sleep(0.2); self.p.kill()

if __name__ == "__main__":
    q = QEMU()
    time.sleep(float(sys.argv[1]) if len(sys.argv) > 1 else 4)
    q.shot("/tmp/boot.png"); print(q.serial()); q.stop()
