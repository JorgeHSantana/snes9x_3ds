#!/usr/bin/env python3
"""Run the PROBE_MENU_HELP build without UI keystrokes; inspect PNGs after run."""
import argparse
import glob
import os
import shutil
import subprocess
import time
import validate as harness


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scene", default="menu-help", choices=("menu-help", "layer-toggle"))
    parser.add_argument("--dsx", default=os.path.join(harness.REPO, "output", "snes9x_3ds.3dsx"))
    parser.add_argument("--sdmc", default=harness.DEFAULT_SDMC)
    parser.add_argument("--out", default=os.path.join(harness.HERE, "out", "menu-help"))
    args = parser.parse_args()
    if subprocess.run(["pgrep", "-x", "azahar"], stdout=subprocess.DEVNULL).returncode == 0:
        parser.error("close Azahar before running this isolated probe")
    scene = harness.load_scene(args.scene)
    sd = harness.Sdmc(args.sdmc)
    os.makedirs(args.out, exist_ok=True)
    captures = scene.get("captures") or [f"menu_help_{i}_{kind}.ppm" for i in range(scene["cases"])
                                         for kind in ("menu", "help")]
    proc = None
    try:
        harness.arm_scene(sd, scene)
        base = harness.rom_base(scene["rom"])
        sd._backup(sd.path(f"configs/{base}.cfg"))
        sd._backup(sd.path(f"saves/{base}.srm"))
        for path in glob.glob(sd.path("debug*session.log")):
            sd._backup(path)
            os.remove(path)
        for name in captures:
            path = sd.path(name)
            sd._backup(path)
            if os.path.exists(path):
                os.remove(path)
        proc = subprocess.Popen([harness.AZAHAR_BIN, os.path.abspath(args.dsx)],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            if proc.poll() is not None:
                raise RuntimeError("Azahar exited before completing captures")
            if all(os.path.exists(sd.path(name)) for name in captures):
                time.sleep(1)
                break
            time.sleep(0.5)
        else:
            missing = [name for name in captures if not os.path.exists(sd.path(name))]
            raise RuntimeError(f"missing probe captures: {missing}")
        ok = True
        for path in glob.glob(sd.path("debug*session.log")):
            shutil.copyfile(path, os.path.join(args.out, os.path.basename(path)))
        for name in captures:
            shutil.copyfile(sd.path(name), os.path.join(args.out, name))
            subprocess.run(["sips", "-s", "format", "png", os.path.join(args.out, name),
                            "--out", os.path.join(args.out, name.replace(".ppm", ".png"))],
                           check=True, stdout=subprocess.DEVNULL)
        for i in range(0, len(captures), 2):
            stats = harness.diff_images(os.path.join(args.out, captures[i].replace(".ppm", ".png")),
                                        os.path.join(args.out, captures[i + 1].replace(".ppm", ".png")),
                                        scene["regions"])
            ok &= harness.report(f"{args.scene} pair {i // 2}", stats, scene["ab_expect"])
        return 0 if ok else 1
    finally:
        if proc is not None and proc.poll() is None:
            proc.kill()  # Do not persist the probe's forced settings on exit.
            proc.wait(timeout=10)
        sd.restore()


if __name__ == "__main__":
    raise SystemExit(main())
