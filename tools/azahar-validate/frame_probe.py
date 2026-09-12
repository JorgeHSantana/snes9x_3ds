#!/usr/bin/env python3
"""Capture deterministic PROBE_FBDUMP frames without automating native UI.

The native Azahar startup warning, if shown, must be acknowledged manually.
Only the process launched here is stopped, and scene files are restored.
"""
import argparse
import os
import shutil
import subprocess
import time
import validate as harness


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("scene")
    parser.add_argument("--dsx", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--sdmc", default=harness.DEFAULT_SDMC)
    parser.add_argument("--frame", type=int, choices=(600, 1200, 3600), default=600)
    args = parser.parse_args()
    if subprocess.run(["pgrep", "-x", "azahar"], stdout=subprocess.DEVNULL).returncode == 0:
        parser.error("close Azahar before running this isolated probe")
    scene = harness.load_scene(args.scene)
    sd = harness.Sdmc(args.sdmc)
    os.makedirs(args.out, exist_ok=True)
    proc = None
    try:
        harness.arm_scene(sd, scene)
        # Autosave and game profiles may be written while a scene runs.
        base = harness.rom_base(scene["rom"])
        config = sd.path(f"configs/{base}.cfg")
        sd._backup(config)
        sd._backup(sd.path(f"saves/{base}.srm"))
        sd._backup(sd.path("settings.cfg"))
        state_capture = sd.path("probe_state_600.frz")
        sd._backup(state_capture)
        if os.path.exists(state_capture):
            os.remove(state_capture)
        for frame in (600, 1200, 3600):
            path = sd.path(f"probe_top_{frame}.png")
            sd._backup(path)
            if os.path.exists(path):
                os.remove(path)
        proc = subprocess.Popen([harness.AZAHAR_BIN, os.path.abspath(args.dsx)],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        capture = sd.path(f"probe_top_{args.frame}.png")
        deadline = time.monotonic() + 180
        while time.monotonic() < deadline:
            if proc.poll() is not None:
                raise RuntimeError("Azahar exited before capture")
            if os.path.exists(capture) and os.path.getsize(capture) > 0:
                current_logs = [sd.path(name) for name in os.listdir(sd.app)
                                if name.startswith("debug_") and name.endswith("_session.log")]
                if current_logs:
                    latest_log = max(current_logs, key=os.path.getmtime)
                    if "[probe] state roundtrip: ok" in open(latest_log).read():
                        break
            time.sleep(0.5)
        else:
            raise RuntimeError("probe frame not produced")
        shutil.copyfile(capture, os.path.join(args.out, "top.png"))
        shutil.copyfile(state_capture, os.path.join(args.out, "state.frz"))
        logs = [name for name in os.listdir(sd.app)
                if name.startswith("debug_") and name.endswith("_session.log")]
        if not logs:
            raise RuntimeError("missing session log")
        latest = max(logs, key=lambda name: os.path.getmtime(sd.path(name)))
        log = open(sd.path(latest)).read()
        for expected in scene.get("log_expect", []):
            if expected not in log:
                raise RuntimeError(f"missing log evidence: {expected}")
        shutil.copyfile(sd.path(latest), os.path.join(args.out, "session.log"))
        print(f"captured {scene['_name']} frame {args.frame}: {args.out}", flush=True)
    finally:
        if proc is not None and proc.poll() is None:
            proc.kill()
            proc.wait(timeout=10)
        sd.restore()


if __name__ == "__main__":
    main()
