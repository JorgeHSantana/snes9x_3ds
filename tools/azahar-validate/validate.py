#!/usr/bin/env python3
"""Azahar screenshot harness (issue #69).

Boots a .3dsx in Azahar with a declared scene (ROM + savestate + 3D
settings + config overrides), captures the top screen and compares it -
against a golden, or against a second scene (A/B). Zero key presses: the
scene is loaded through the updater's resume marker (update-resume.txt +
<rom>.update.frz), which the emulator consumes on boot.

Python 3 stdlib only. macOS only (screencapture, osascript, sips, open).

    tools/azahar-validate/validate.py run   SCENE [--golden PNG] [--update-golden]
    tools/azahar-validate/validate.py ab    SCENE_A SCENE_B
    tools/azahar-validate/validate.py list

Scenes live in tools/azahar-validate/scenes/*.json; see README.md there.
3D scenes need a build with -DPROBE_FORCE_SLIDER (Azahar never delivers a
real slider value). Flag flips need `make clean`.
"""
import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
DEFAULT_SDMC = os.path.expanduser("~/Library/Application Support/Azahar/sdmc")
APP_DIR = "3ds/snes9x_3ds"
WINDOW = (2145, 459, 1008, 539)          # canonical Azahar window (x, y, w, h)
TOP_SCREEN = (303, 33, 400, 236)         # top screen inside that window
DIFF_THRESHOLD = 40                      # sum of |dR|+|dG|+|dB| that counts as changed


def log(msg):
    print("[validate] " + msg, flush=True)


def sh(cmd, check=True, capture=False):
    r = subprocess.run(cmd, shell=isinstance(cmd, str), check=check,
                       stdout=subprocess.PIPE if capture else None,
                       stderr=subprocess.STDOUT if capture else None, text=True)
    return r.stdout if capture else None


def osa(script):
    return subprocess.run(["osascript", "-e", script], stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True).stdout.strip()


# ---------------------------------------------------------------- scenes
def load_scene(name):
    path = name if name.endswith(".json") else os.path.join(HERE, "scenes", name + ".json")
    with open(path) as f:
        sc = json.load(f)
    sc["_name"] = os.path.splitext(os.path.basename(path))[0]
    sc.setdefault("wait", 32)
    sc.setdefault("regions", {})
    return sc


def rom_base(rom_sdmc_path):
    base = rom_sdmc_path.rsplit("/", 1)[-1]
    return os.path.splitext(base)[0]


class Sdmc:
    """Edits the Azahar SD image for a scene and restores it afterwards."""

    def __init__(self, root):
        self.root = root
        self.app = os.path.join(root, APP_DIR)
        self.backups = {}
        self.created = []

    def path(self, rel):
        return os.path.join(self.app, rel)

    def _backup(self, p):
        if p in self.backups:
            return
        self.backups[p] = open(p, "rb").read() if os.path.exists(p) else None

    def write(self, p, data):
        self._backup(p)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "wb") as f:
            f.write(data if isinstance(data, bytes) else data.encode())

    def copy(self, src, p):
        self._backup(p)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        shutil.copyfile(src, p)

    def edit_cfg(self, overrides):
        """settings.cfg is parsed SEQUENTIALLY per version block: a key
        that is not already there derails everything after it. Only
        existing keys are touched; a missing one is an error."""
        p = self.path("settings.cfg")
        self._backup(p)
        lines = open(p).read().split("\n")
        for key, value in overrides.items():
            hit = [i for i, l in enumerate(lines) if l.startswith(key + "=")]
            if not hit:
                raise SystemExit(f"settings.cfg has no '{key}=' line - let the emulator "
                                 f"write the current version first (boot it once), never insert keys by hand")
            lines[hit[0]] = f"{key}={value}"
        open(p, "w").write("\n".join(lines))

    def restore(self):
        for p, data in self.backups.items():
            if data is None:
                if os.path.exists(p):
                    os.remove(p)
            else:
                with open(p, "wb") as f:
                    f.write(data)
        self.backups.clear()


def arm_scene(sd, sc):
    rom = sc["rom"]                                   # sdmc:/... path
    base = rom_base(rom)
    sd.write(os.path.join(sd.root, "autoboot.txt"), rom + "\n")
    if sc.get("state"):
        state = os.path.join(HERE, sc["state"]) if not os.path.isabs(sc["state"]) else sc["state"]
        sd.copy(state, sd.path(f"savestates/{base}.update.frz"))
        sd.write(sd.path("update-resume.txt"), rom + "\n")
    for f3d in sc.get("stereo3d", []):
        sd.write(sd.path(f"stereo3d/{f3d['file']}.3d"), f3d["content"].rstrip("\n") + "\n")
    if sc.get("settings"):
        sd.edit_cfg(sc["settings"])


# ---------------------------------------------------------------- azahar
def kill_azahar():
    subprocess.run(["pkill", "-9", "-x", "azahar"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.0)


def launch(dsx, wait_s):
    sh(["open", "-a", "Azahar", dsx])
    log(f"waiting {wait_s}s for boot + scene load")
    time.sleep(wait_s)
    osa('tell application "System Events" to tell process "azahar" to set frontmost to true')
    x, y, w, h = WINDOW
    osa(f'tell application "System Events" to tell process "azahar"\n'
        f'  set position of window 1 to {{{x}, {y}}}\n  set size of window 1 to {{{w}, {h}}}\nend tell')
    time.sleep(1.0)


def capture_top(out_png):
    x, y, w, h = WINDOW
    tx, ty, tw, th = TOP_SCREEN
    r = subprocess.run(["screencapture", "-x", "-R", f"{x + tx},{y + ty},{tw},{th}", out_png],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if r.returncode != 0 or not os.path.exists(out_png):
        raise SystemExit("screencapture failed - is the display locked/asleep? "
                         "(run `caffeinate -d` and unlock the screen)")


def session_log(sd):
    logs = sorted((os.path.join(sd.app, f) for f in os.listdir(sd.app)
                   if f.startswith("debug_") and f.endswith("_session.log")), key=os.path.getmtime)
    return open(logs[-1]).read() if logs else ""


# ---------------------------------------------------------------- images
def png_to_pixels(png):
    bmp = png + ".bmp"
    sh(["sips", "-s", "format", "bmp", png, "--out", bmp], capture=True)
    d = open(bmp, "rb").read()
    os.remove(bmp)
    off = struct.unpack_from("<I", d, 10)[0]
    w, h = struct.unpack_from("<ii", d, 18)
    bpp = struct.unpack_from("<H", d, 28)[0]
    row = ((w * bpp // 8) + 3) // 4 * 4
    px = []
    for y in range(abs(h)):
        yy = (abs(h) - 1 - y) if h > 0 else y
        b = off + yy * row
        px.append([tuple(d[b + x * (bpp // 8): b + x * (bpp // 8) + 3]) for x in range(w)])
    return w, abs(h), px


def diff_images(a_png, b_png, regions, out_mask=None):
    w, h, a = png_to_pixels(a_png)
    w2, h2, b = png_to_pixels(b_png)
    if (w, h) != (w2, h2):
        raise SystemExit(f"size mismatch {w}x{h} vs {w2}x{h2}")
    changed = [[(sum(abs(a[y][x][k] - b[y][x][k]) for k in range(3)) > DIFF_THRESHOLD)
                for x in range(w)] for y in range(h)]

    def frac(x0, y0, x1, y1):
        n = t = 0
        for y in range(max(0, y0), min(h, y1)):
            for x in range(max(0, x0), min(w, x1)):
                t += 1
                n += changed[y][x]
        return n / t if t else 0.0

    result = {"total": frac(0, 0, w, h)}
    for name, (x0, y0, x1, y1) in regions.items():
        result[name] = frac(x0, y0, x1, y1)
    if out_mask:
        # a PGM is trivial to write and QuickLook opens it
        with open(out_mask, "wb") as f:
            f.write(f"P5\n{w} {h}\n255\n".encode())
            f.write(bytes(255 if changed[y][x] else 0 for y in range(h) for x in range(w)))
    return result


# ---------------------------------------------------------------- runs
def run_scene(sd, sc, dsx, out_png):
    kill_azahar()
    arm_scene(sd, sc)
    try:
        launch(dsx, sc["wait"])
        capture_top(out_png)
        # temporal stability: extra frames 0.3s apart, diffed pairwise -
        # a static region that changes between consecutive frames is a
        # flicker/wobble (the Light blur once alternated its ghost side)
        frames = int(sc.get("frames", 1))
        sc["_temporal"] = None
        if frames > 1:
            paths = [out_png]
            for i in range(1, frames):
                time.sleep(0.3)
                pi = out_png.replace(".png", f".f{i}.png")
                capture_top(pi)
                paths.append(pi)
            worst = {}
            for a, b in zip(paths, paths[1:]):
                st = diff_images(a, b, sc["regions"])
                for k, v in st.items():
                    worst[k] = max(worst.get(k, 0.0), v)
            sc["_temporal"] = worst
        text = session_log(sd)
    finally:
        kill_azahar()
        sd.restore()
        # the resume marker is consumed on boot; the parked state is not
        # restored by design (it was ours) - remove any leftover
        for p in (sd.path("update-resume.txt"), sd.path(f"savestates/{rom_base(sc['rom'])}.update.frz")):
            if os.path.exists(p):
                os.remove(p)
    for pat in sc.get("log_expect", []):
        if pat not in text:
            raise SystemExit(f"session log lacks expected text: {pat!r}")
    for pat in sc.get("log_forbid", []):
        if pat in text:
            raise SystemExit(f"session log contains forbidden text: {pat!r}")
    return out_png


def report(name, stats, regions_expect):
    ok = True
    print(f"\n== {name}")
    for k, v in stats.items():
        exp = regions_expect.get(k)
        verdict = ""
        if exp is not None:
            lo, hi = exp
            good = lo <= v * 100 <= hi
            ok &= good
            verdict = "  OK" if good else f"  FAIL (expected {lo}..{hi}%)"
        print(f"  {k:<16} {v * 100:6.2f}%{verdict}")
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=["run", "ab", "list"])
    ap.add_argument("scenes", nargs="*")
    ap.add_argument("--dsx", default=os.path.join(REPO, "output", "snes9x_3ds.3dsx"))
    ap.add_argument("--sdmc", default=DEFAULT_SDMC)
    ap.add_argument("--golden", help="run: compare against this PNG")
    ap.add_argument("--update-golden", action="store_true", help="run: write the capture as the golden")
    ap.add_argument("--out", default=os.path.join(HERE, "out"))
    args = ap.parse_args()

    if args.mode == "list":
        for f in sorted(os.listdir(os.path.join(HERE, "scenes"))):
            if f.endswith(".json"):
                sc = load_scene(os.path.join(HERE, "scenes", f))
                print(f"{sc['_name']:<28} {sc.get('title', '')}")
        return 0

    os.makedirs(args.out, exist_ok=True)
    sd = Sdmc(args.sdmc)
    if not os.path.isdir(sd.app):
        raise SystemExit(f"no emulator dir at {sd.app}")
    caff = subprocess.Popen(["caffeinate", "-d"])
    try:
        if args.mode == "run":
            sc = load_scene(args.scenes[0])
            png = run_scene(sd, sc, args.dsx, os.path.join(args.out, sc["_name"] + ".png"))
            golden = args.golden or os.path.join(HERE, "goldens", sc["_name"] + ".png")
            if args.update_golden:
                os.makedirs(os.path.dirname(golden), exist_ok=True)
                shutil.copyfile(png, golden)
                log(f"golden written: {golden}")
                return 0
            ok = True
            if sc.get("_temporal") is not None:
                ok &= report(f"{sc['_name']} temporal (max consecutive-frame diff)", sc["_temporal"],
                             sc.get("temporal_expect", {}))
            if not os.path.exists(golden):
                log(f"no golden at {golden}; capture kept at {png}")
                return 0 if ok else 1
            stats = diff_images(png, golden, sc["regions"], os.path.join(args.out, sc["_name"] + ".diff.pgm"))
            ok &= report(f"{sc['_name']} vs golden", stats, sc.get("expect", {}))
            return 0 if ok else 1
        if args.mode == "ab":
            a, b = load_scene(args.scenes[0]), load_scene(args.scenes[1])
            pa = run_scene(sd, a, args.dsx, os.path.join(args.out, a["_name"] + ".png"))
            pb = run_scene(sd, b, args.dsx, os.path.join(args.out, b["_name"] + ".png"))
            regions = dict(a["regions"]); regions.update(b["regions"])
            stats = diff_images(pa, pb, regions, os.path.join(args.out, f"{a['_name']}_vs_{b['_name']}.diff.pgm"))
            expect = dict(a.get("ab_expect", {})); expect.update(b.get("ab_expect", {}))
            ok = report(f"{a['_name']} vs {b['_name']}", stats, expect)
            return 0 if ok else 1
    finally:
        caff.terminate()
    return 0


if __name__ == "__main__":
    sys.exit(main())
