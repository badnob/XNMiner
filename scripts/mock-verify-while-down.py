#!/usr/bin/env python3
"""Mock xenblocks pool: GET /difficulty is DOWN, POST /verify still accepts.

Replays the supervisor gates from supervisor.cpp:
  live_submit_allowed, flush_submit_allowed, oracle_says_m, oracle_left_m,
  try_flush_pending (drain vs not-drain), and an HTTP wave of POST /verify
  while every GET /difficulty fails.
"""
from __future__ import annotations

import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

DIFF_HITS = 0
VERIFY_HITS = 0
VERIFY_ACCEPTED = 0
VERIFY_BODIES = []
LOCK = threading.Lock()

FORCED_M = 10000
SAMPLE_HASH = (
    "$argon2id$v=19$m=10000,t=1,p=1$"
    "YWJjZGVmZ2hpamtsbW5vcA$"
    "dGVzdGhhc2hibG9ja2Zvcm1vY2t2ZXJpZnk"
)


class MockPool(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        return

    def do_GET(self):
        global DIFF_HITS
        path = self.path.split("?", 1)[0]
        if path in ("/difficulty", "/difficulty/"):
            with LOCK:
                DIFF_HITS += 1
            # Pool /difficulty is DOWN: empty 503, no m=.
            self.send_response(503)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        self.send_response(404)
        self.end_headers()

    def do_POST(self):
        global VERIFY_HITS, VERIFY_ACCEPTED
        path = self.path.split("?", 1)[0]
        n = int(self.headers.get("Content-Length") or "0")
        raw = self.rfile.read(n) if n else b""
        if path not in ("/verify", "/verify/"):
            self.send_response(404)
            self.end_headers()
            return
        try:
            payload = json.loads(raw.decode("utf-8") or "{}")
        except json.JSONDecodeError:
            payload = {}
        with LOCK:
            VERIFY_HITS += 1
            VERIFY_BODIES.append(payload)
            VERIFY_ACCEPTED += 1
            seq = VERIFY_ACCEPTED
        body = json.dumps({"ok": True, "id": seq, "result": "ok"}).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def start_pool():
    httpd = ThreadingHTTPServer(("127.0.0.1", 0), MockPool)
    t = threading.Thread(target=httpd.serve_forever, daemon=True)
    t.start()
    host, port = httpd.server_address[:2]
    return httpd, f"http://{host}:{port}"


def http_get(url, timeout=1.0):
    try:
        with urlopen(url, timeout=timeout) as r:
            return r.status, r.read().decode("utf-8", "replace")
    except HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")
    except Exception as e:
        return 0, str(e)


def http_post_verify(url, timeout=2.0, n=1):
    payload = {
        "account": "0x1111111111111111111111111111111111111111",
        "key": "a" * 64,
        "hash_to_verify": SAMPLE_HASH,
        "attempts": str(n),
        "hashes_per_second": "1234",
        "worker": "mock-down-test",
    }
    req = Request(
        url,
        data=json.dumps(payload).encode("utf-8"),
        headers={
            "Content-Type": "application/json; charset=utf-8",
            "User-Agent": "python-requests/2.31.0",
        },
        method="POST",
    )
    try:
        with urlopen(req, timeout=timeout) as r:
            body = r.read().decode("utf-8", "replace")
            return r.status, body
    except HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")
    except (URLError, OSError, TimeoutError) as e:
        return 0, str(e)


# --- faithful copies of supervisor.cpp gates ---

def live_submit_allowed(network_ok, force_mine, last_good_m, forced_m, bag_only=False, backoff=False):
    if bag_only:
        return False
    if backoff:
        return False
    if network_ok:
        return True
    return bool(force_mine and last_good_m == forced_m)


def flush_submit_allowed(network_ok, force_mine, last_good_m, forced_m, draining, backoff):
    if backoff and not draining:
        return False
    if network_ok:
        return True
    if force_mine and last_good_m == forced_m:
        return True
    return False


def oracle_says_m(network_ok, stale, last_good_m, m, force_mine=True, forced_m=FORCED_M):
    if m <= 0 or last_good_m != m:
        return False
    if network_ok and not stale:
        return True
    return bool(force_mine and last_good_m == forced_m)


def oracle_left_m(network_ok, last_good_m, m):
    if m <= 0:
        return False
    if not (network_ok and last_good_m and last_good_m > 0):
        return False
    return last_good_m != m


def try_flush_would_submit(network_ok, stale, last_good_m, forced_m, already_draining):
    """Mirror Supervisor::try_flush_pending for hybrid force-mine."""
    bag_m = forced_m
    draining = already_draining or oracle_says_m(network_ok, stale, last_good_m, bag_m)
    allowed = flush_submit_allowed(
        network_ok, True, last_good_m, forced_m, draining, backoff=False
    )
    if not draining and not allowed:
        return False, "skip: submit not allowed (backoff or net down)"
    if not draining:
        # refresh_network fails while /difficulty is DOWN
        matches = oracle_says_m(network_ok, stale, last_good_m, bag_m)
        if not allowed or not matches:
            return False, "skip: difficulty poll failed and last-good does not match bag"
    return True, f"POST /verify (draining={draining} bag_m={bag_m})"


def row(name, ok, detail):
    flag = "PASS" if ok else "FAIL"
    print(f"  [{flag}] {name}")
    print(f"         {detail}")
    return ok


def main():
    httpd, base = start_pool()
    diff_url = base + "/difficulty"
    verify_url = base + "/verify"
    print("Mock pool")
    print(f"  GET  {diff_url}  -> always 503 (Net m= DOWN)")
    print(f"  POST {verify_url} -> always 200")
    print()

    results = []

    print("1) HTTP: /difficulty is DOWN")
    st, body = http_get(diff_url)
    results.append(
        row(
            "GET /difficulty",
            st == 503 and body == "",
            f"status={st} body={body!r}  => miner paints Net m= DOWN",
        )
    )

    print()
    print("2) HTTP: POST /verify still accepted while /difficulty is DOWN")
    statuses = []
    t0 = time.perf_counter()
    for i in range(8):
        st, body = http_post_verify(verify_url, n=i + 1)
        statuses.append(st)
    elapsed_ms = (time.perf_counter() - t0) * 1000
    ok = all(s == 200 for s in statuses) and len(statuses) == 8
    results.append(
        row(
            "8 x POST /verify during DOWN",
            ok,
            f"statuses={statuses}  {elapsed_ms:.1f}ms  accepted={VERIFY_ACCEPTED}",
        )
    )

    print()
    print("3) Supervisor gates with Net m= DOWN (last-good m=10000, hybrid pin 10000)")
    network_ok, stale, last_good = False, True, FORCED_M

    live = live_submit_allowed(network_ok, True, last_good, FORCED_M)
    results.append(
        row(
            "live_submit_allowed (new CUDA hits)",
            live is True,
            f"{live}  — last-good m= matches hybrid pin, POST /verify anyway",
        )
    )

    says = oracle_says_m(network_ok, stale, last_good, FORCED_M)
    results.append(
        row(
            "oracle_says_m (open a NEW match window)",
            says is True,
            f"{says}  — last-good m={last_good} == force-mine m={FORCED_M}",
        )
    )

    left = oracle_left_m(network_ok, last_good, FORCED_M)
    results.append(
        row(
            "oracle_left_m (close an OPEN match window)",
            left is False,
            f"{left}  — DOWN does not count as 'live m= left bag'",
        )
    )

    flush_ok = flush_submit_allowed(
        network_ok, True, last_good, FORCED_M, draining=True, backoff=False
    )
    results.append(
        row(
            "flush_submit_allowed while already draining",
            flush_ok is True,
            f"{flush_ok}  — last-good m= still equals hybrid pin",
        )
    )

    would, why = try_flush_would_submit(
        network_ok, stale, last_good, FORCED_M, already_draining=True
    )
    results.append(
        row(
            "try_flush_pending with drain already open",
            would is True,
            why,
        )
    )

    would_new, why_new = try_flush_would_submit(
        network_ok, stale, last_good, FORCED_M, already_draining=False
    )
    results.append(
        row(
            "try_flush_pending with drain NOT open",
            would_new is True,
            why_new,
        )
    )

    mismatch_live = live_submit_allowed(network_ok, True, 1100, FORCED_M)
    mismatch_says = oracle_says_m(network_ok, stale, 1100, FORCED_M)
    results.append(
        row(
            "DOWN + last-good m=1100 != pin 10000 — do NOT submit",
            mismatch_live is False and mismatch_says is False,
            f"live={mismatch_live} oracle={mismatch_says}",
        )
    )

    print()
    print("4) Concurrent: keep GET /difficulty failing while a 32-wide /verify wave runs")
    stop = False

    def hammer_diff():
        while not stop:
            http_get(diff_url, timeout=0.5)
            time.sleep(0.02)

    th = threading.Thread(target=hammer_diff, daemon=True)
    th.start()
    wave = 32
    ok_n = 0
    fail_n = 0
    t0 = time.perf_counter()
    for i in range(wave):
        st, _ = http_post_verify(verify_url, n=100 + i)
        if st == 200:
            ok_n += 1
        else:
            fail_n += 1
    wave_ms = (time.perf_counter() - t0) * 1000
    stop = True
    th.join(timeout=1)
    results.append(
        row(
            "32 /verify POSTs while /difficulty 503s",
            ok_n == wave and fail_n == 0,
            f"accepted={ok_n}/{wave} failed={fail_n}  {wave_ms:.1f}ms  "
            f"diff_gets={DIFF_HITS} verify_posts={VERIFY_HITS}",
        )
    )

    print()
    print("5) Hash body that /verify accepted still carries m=10000")
    m_ok = SAMPLE_HASH.startswith("$argon2id$v=19$m=10000,")
    posted = VERIFY_BODIES[-1] if VERIFY_BODIES else {}
    results.append(
        row(
            "submitted hash m=",
            m_ok and posted.get("hash_to_verify", "").find("m=10000") >= 0,
            f"hash starts {SAMPLE_HASH[0:40]}...  worker={posted.get('worker')}",
        )
    )

    httpd.shutdown()

    print()
    print("=" * 72)
    passed = sum(1 for r in results if r)
    print(f"RESULT  {passed}/{len(results)} checks passed")
    print()
    print("Conclusion")
    print("  GET /difficulty DOWN does not take POST /verify down.")
    print("  Hybrid last-good m= matching the force-mine pin still flushes.")
    print("  Last-good m= that does not match the pin does not submit.")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
