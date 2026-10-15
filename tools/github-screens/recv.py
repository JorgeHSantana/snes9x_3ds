#!/usr/bin/env python3
"""Local stand-in for GitHub's comments endpoint (PROBE_GITHUB_SEND): accepts
the emulator's POST, stores each comment body as a JSON file in --out and
answers 201 like the API. Point sdmc/3ds/snes9x_3ds/github.env at it."""
import argparse, json, os, time
from http.server import BaseHTTPRequestHandler, HTTPServer

class H(BaseHTTPRequestHandler):
    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(n)
        try: body = json.loads(raw.decode("utf-8"))["body"]
        except Exception: body = raw.decode("utf-8", "replace")
        i = len(os.listdir(self.server.out))
        with open(os.path.join(self.server.out, f"comment_{i:03d}.json"), "w") as f:
            json.dump({"body": body, "created_at": time.strftime("%Y-%m-%dT%H:%M:%SZ")}, f)
        print(f"[recv] comment {i}: {len(body)} chars: {body.splitlines()[1] if body.count(chr(10)) else ''}")
        reply = json.dumps({"html_url": f"http://local/comment/{i}"}).encode()
        self.send_response(201); self.send_header("Content-Type", "application/json"); self.send_header("Content-Length", str(len(reply))); self.end_headers(); self.wfile.write(reply)
    def log_message(self, *a): pass

ap = argparse.ArgumentParser(); ap.add_argument("--port", type=int, default=8765); ap.add_argument("--out", default="recv")
a = ap.parse_args(); os.makedirs(a.out, exist_ok=True)
s = HTTPServer(("0.0.0.0", a.port), H); s.out = a.out
print(f"[recv] listening on {a.port}, saving to {a.out}"); s.serve_forever()
