#!/usr/bin/env python3
"""Reassemble the emulator's 'Send Screenshot to GitHub' comments into PNGs.
Reads comment bodies from an issue (gh api) or from recv.py's folder; each
comment carries `screen_<eye>.png[ (part i/n)]` and a ```base64 block."""
import argparse, base64, json, os, re, subprocess, sys

def bodies_from_issue(repo, issue, limit):
    out = subprocess.run(["gh", "api", f"repos/{repo}/issues/{issue}/comments?per_page=100&sort=created&direction=desc", "--jq", "[.[] | {body: .body, created_at: .created_at}]"], capture_output=True, text=True, check=True)
    return list(reversed(json.loads(out.stdout)))[-limit:]

def bodies_from_dir(d):
    return [json.load(open(os.path.join(d, f))) for f in sorted(os.listdir(d)) if f.endswith(".json")]

HEAD = re.compile(r"`screen_(left|right)\.png(?: \(part (\d+)/(\d+)\))?`")
def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--repo", default="JorgeHSantana/snes9x_3ds"); ap.add_argument("--issue", type=int, default=80)
    ap.add_argument("--dir", help="recv.py output folder instead of GitHub"); ap.add_argument("--out", default="shots"); ap.add_argument("--limit", type=int, default=30)
    a = ap.parse_args(); os.makedirs(a.out, exist_ok=True)
    items = bodies_from_dir(a.dir) if a.dir else bodies_from_issue(a.repo, a.issue, a.limit)
    shots = {}   # (stamp, eye) -> {part: b64}
    for it in items:
        body = it["body"]; m = HEAD.search(body)
        if not m: continue
        fence = re.search(r"```base64\n(.*?)\n```", body, re.S)
        if not fence: continue
        stamp = re.search(r"\*\*snes9x_3ds [^*]*\*\* · [^·]* · [^·]* · ([^\n]*)", body)
        key = ((stamp.group(1).strip() if stamp else it.get("created_at", "")), m.group(1))
        part, total = int(m.group(2) or 1), int(m.group(3) or 1)
        shots.setdefault(key, {"total": total, "parts": {}})["parts"][part] = fence.group(1).replace("\n", "")
    n = 0
    for (stamp, eye), s in shots.items():
        if len(s["parts"]) != s["total"]: print(f"[fetch] {stamp} {eye}: {len(s['parts'])}/{s['total']} parts, skipped"); continue
        data = base64.b64decode("".join(s["parts"][i] for i in range(1, s["total"] + 1)))
        name = os.path.join(a.out, f"{stamp.replace('/', '-').replace(':', '-').replace(' ', '_')}_{eye}.png")
        open(name, "wb").write(data); print(f"[fetch] {name}: {len(data)} bytes"); n += 1
    if n == 0: print("[fetch] no screenshot comments found"); sys.exit(1)
main()
