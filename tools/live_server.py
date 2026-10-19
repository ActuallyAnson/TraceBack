#!/usr/bin/env python3
"""Local server for the Traceback UI: serves docs/ and runs the pipeline on request (`make live`).

GET /api/health            -> {"live": true, "key": bool}
GET /api/run?inc=inc-026   -> re-runs the deterministic simulator for that incident now, then the analyzer, citation
                              check and replay verifier, and returns the same JSON the static site uses plus a
                              "provenance" block saying exactly what was live and what was recorded.
  &model=live              -> also calls the model live (needs NEBIUS_API_KEY; costs money). It has a hard time limit;
                              if the call fails or times out the recorded answer is used and labelled "recorded-fallback".
Model answers default to the recorded ones in eval/llm_cache (offline, free). The simulation is always run for real.
"""
import http.server, json, os, pathlib, subprocess, sys, tempfile, threading, time, urllib.parse

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "eval"))
import build_site  # noqa: E402

LIVE_TIMEOUT_S = int(os.environ.get("TB_LIVE_MODEL_TIMEOUT", "60"))  # per request; whole live attempt is capped at 2x
LOCK = threading.Lock()
MANIFEST = {json.loads(l)["id"]: json.loads(l)["split"] for l in (ROOT / "eval/split.json").read_text().splitlines() if l.strip()}


def run_pipeline(iid, model):
    t0 = time.time()
    subprocess.run([str(ROOT / "build/tb_sim"), "generate", "--out", str(ROOT / "runs"), "--only", iid], check=True, capture_output=True)
    sim_ms = round((time.time() - t0) * 1000)
    recorded = ["--cache", str(ROOT / "eval/llm_cache")]
    prov = dict(simulation="run live just now (deterministic seed)", sim_ms=sim_ms, model="recorded", note="")
    with tempfile.TemporaryDirectory() as tmp:
        out = pathlib.Path(tmp)
        args = recorded
        if model == "live":
            if not os.environ.get("NEBIUS_API_KEY"):
                prov["note"] = "live model call requested but NEBIUS_API_KEY is not set: using recorded answers"
                prov["model"] = "recorded-fallback"
            else:
                # separate cache dir so a live call is never confused with (or overwrites) a recorded answer
                args = ["--cache", str(ROOT / "runs/live_cache"), "--timeout", str(LIVE_TIMEOUT_S)]
        t1 = time.time()
        try:
            if args is recorded:
                build_site.per_incident(iid, MANIFEST[iid], out=out, llm_args=args, write_report=False)
            else:
                signal_free = threading.Event()
                err = []
                def work():
                    try:
                        build_site.per_incident(iid, MANIFEST[iid], out=out, llm_args=args, write_report=False)
                    except Exception as e:  # noqa: BLE001
                        err.append(e)
                    signal_free.set()
                th = threading.Thread(target=work, daemon=True)
                th.start()
                if not signal_free.wait(LIVE_TIMEOUT_S * 2) or err:
                    raise RuntimeError("model call timed out" if not err else f"model call failed: {err[0]}")
                prov["model"] = "live"
                prov["note"] = "model called live just now; answer may differ from the recorded run (the model is not deterministic)"
        except Exception as e:  # noqa: BLE001
            prov["model"] = "recorded-fallback"
            why = "timed out" if "timed out" in str(e) else "the model call returned an error (bad key, network or rate limit)"
            prov["note"] = f"live model call did not complete: {why}; showing the recorded answer instead"
            build_site.per_incident(iid, MANIFEST[iid], out=out, llm_args=recorded, write_report=False)
        prov["pipeline_ms"] = round((time.time() - t1) * 1000)
        d = json.loads((out / f"{iid}.json").read_text())
    d["provenance"] = prov
    return d


class H(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=str(ROOT / "docs"), **k)

    def _json(self, code, obj):
        b = json.dumps(obj, separators=(",", ":")).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def do_GET(self):  # noqa: N802
        u = urllib.parse.urlparse(self.path)
        if u.path == "/api/health":
            return self._json(200, dict(live=True, key=bool(os.environ.get("NEBIUS_API_KEY")), model_timeout_s=LIVE_TIMEOUT_S))
        if u.path == "/api/run":
            q = urllib.parse.parse_qs(u.query)
            iid = (q.get("inc") or [""])[0]
            if iid not in MANIFEST:
                return self._json(404, dict(error="unknown incident"))
            try:
                with LOCK:
                    return self._json(200, run_pipeline(iid, (q.get("model") or ["recorded"])[0]))
            except Exception as e:  # noqa: BLE001
                return self._json(500, dict(error=str(e)))
        return super().do_GET()

    def log_message(self, fmt, *args):
        sys.stderr.write("  " + fmt % args + "\n")


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    print(f"Traceback live UI: http://localhost:{port}  (simulation runs live; model answers recorded unless you choose live)")
    http.server.ThreadingHTTPServer(("127.0.0.1", port), H).serve_forever()
