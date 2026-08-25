#!/usr/bin/env python3
"""Mock match-drain /verify flush.

Replays the miner settings:
  match_drain_min_queue = 1
  match_drain_max_s     = 0   (flush until bag empty)
  match_drain_parallel  = 0   (auto from flush CPU count)
  match_drain_batch     = 0   (whole matching bag in one wave)
  connection_timeout_s  = 20

Dummy hashes only. Default is a local mock /verify so we can find the
in-flight ceiling. Pass --live for a light dummy sweep against xenblocks.io.
"""
from __future__ import annotations

import argparse
import json
import os
import statistics
import sys
import threading
import time
import urllib.error
import urllib.request
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

VERIFY_LIVE = "http://xenblocks.io/verify"
DIFF_LIVE = "http://xenblocks.io/difficulty"
LASTBLOCK_LIVE = "http://xenblocks.io:4445/getblocks/lastblock"
UA = "python-requests/2.31.0"
TIMEOUT_S = 20


def cpu_count() -> int:
    n = os.cpu_count() or 1
    return max(1, n)


def auto_flush_cores(ncpu: int, desktop: int = 0) -> int:
    remain = max(1, ncpu - max(0, desktop))
    if remain <= 6:
        flush = 1
    elif remain <= 8:
        flush = 2
    elif remain <= 16:
        flush = max(2, remain // 4)
    else:
        flush = 4
    bag = 0 if remain <= 4 else (1 if remain <= 8 else 2)
    dash = 0 if remain <= 4 else (1 if remain <= 8 else 2)
    while bag + flush + dash >= remain and remain > 1:
        if dash > 0:
            dash -= 1
        elif bag > 0:
            bag -= 1
        elif flush > 1:
            flush -= 1
        else:
            break
    return max(1, flush)


def flush_http_cap(flush_cores: int) -> int:
    if flush_cores <= 1:
        cap = 256
    elif flush_cores <= 4:
        cap = 1024
    else:
        cap = 2048
    return min(2048, max(32, cap))


def brute_drain_parallel(configured: int, bag_q: int, cap: int) -> int:
    par = cap if configured <= 0 else configured
    if bag_q >= 5000:
        par = max(par, min(1024, cap))
    if bag_q >= 25000:
        par = max(par, cap)
    return max(1, min(4096, min(par, cap)))


def clip(s: str, n: int = 80) -> str:
    s = (s or "").replace("\n", " ").replace("\r", " ")
    return s[:n]


def classify(status: int, body: str) -> str:
    l = (body or "").lower()
    if status == 0:
        return "timeout"
    if 200 <= status < 300:
        return "http-2xx"
    if "already exists" in l:
        return "already-exists"
    if status in (401, 403, 429) or "hash verification failed" in l:
        return "hold-401"
    return f"http-{status}"


def dummy_payload(i: int) -> dict:
    key = f"{i:064x}"
    h = (
        "$argon2id$v=19$m=100,t=1,p=1$c59/7GUZbuY1EHL+/F0xnvYvuDE$"
        + f"{i:086d}"[:86]
    )
    return {
        "account": "0x0000000000000000000000000000000000000001",
        "key": key,
        "hash_to_verify": h,
        "attempts": "1",
        "hashes_per_second": "1",
        "worker": "xnminer-mock-drain",
    }


class MockVerify:
    """Pool stand-in: 401 hold, optional queue delay, overload → hang past timeout."""

    def __init__(self, service_ms: float, overload_at: int) -> None:
        self.service_ms = service_ms
        self.overload_at = overload_at
        self.in_flight = 0
        self.peak = 0
        self.lock = threading.Lock()
        self.handled = 0

    def enter(self) -> int:
        with self.lock:
            self.in_flight += 1
            self.peak = max(self.peak, self.in_flight)
            return self.in_flight

    def leave(self) -> None:
        with self.lock:
            self.in_flight -= 1
            self.handled += 1


def make_handler(state: MockVerify):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, fmt: str, *args) -> None:  # noqa: ARG002
            return

        def do_POST(self) -> None:  # noqa: N802
            n = int(self.headers.get("Content-Length") or 0)
            if n:
                self.rfile.read(n)
            inflight = state.enter()
            try:
                if state.overload_at > 0 and inflight > state.overload_at:
                    time.sleep(TIMEOUT_S + 2)
                else:
                    time.sleep(state.service_ms / 1000.0)
                body = b'{"error":"Hash verification failed"}'
                self.send_response(401)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(body)
            except BrokenPipeError:
                pass
            finally:
                state.leave()

    return Handler


def post(url: str, i: int, timeout_s: float) -> dict:
    body = json.dumps(dummy_payload(i)).encode()
    req = urllib.request.Request(
        url,
        data=body,
        method="POST",
        headers={
            "Content-Type": "application/json; charset=utf-8",
            "Accept": "application/json",
            "User-Agent": UA,
            "Connection": "close",
        },
    )
    t0 = time.perf_counter()
    try:
        with urllib.request.urlopen(req, timeout=timeout_s) as r:
            raw = r.read(400).decode("utf-8", "replace")
            st = int(getattr(r, "status", 200))
            return {
                "status": st,
                "ms": (time.perf_counter() - t0) * 1000,
                "kind": classify(st, raw),
                "body": clip(raw),
            }
    except urllib.error.HTTPError as e:
        raw = ""
        try:
            raw = e.read(400).decode("utf-8", "replace")
        except Exception:
            pass
        return {
            "status": int(e.code),
            "ms": (time.perf_counter() - t0) * 1000,
            "kind": classify(e.code, raw),
            "body": clip(raw),
        }
    except Exception as e:
        return {
            "status": 0,
            "ms": (time.perf_counter() - t0) * 1000,
            "kind": "timeout",
            "body": f"{type(e).__name__}: {e}"[:80],
        }


def get(url: str, timeout: float = 8.0) -> str:
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    t0 = time.perf_counter()
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            raw = r.read(800).decode("utf-8", "replace")
            return f"{r.status} {(time.perf_counter()-t0)*1000:.0f}ms {clip(raw, 120)}"
    except urllib.error.HTTPError as e:
        raw = ""
        try:
            raw = e.read(200).decode("utf-8", "replace")
        except Exception:
            pass
        return f"{e.code} {(time.perf_counter()-t0)*1000:.0f}ms {clip(raw, 120)}"
    except Exception as e:
        return f"0 {(time.perf_counter()-t0)*1000:.0f}ms {type(e).__name__}: {e}"


def run_wave(url: str, bag: int, parallel: int, timeout_s: float, seq0: int) -> dict:
    """Same shape as Supervisor::try_flush_pending match-drain: N workers, whole bag."""
    t0 = time.perf_counter()
    rows: list[dict] = []
    workers = max(1, min(parallel, bag))
    with ThreadPoolExecutor(max_workers=workers) as ex:
        futs = [ex.submit(post, url, seq0 + i, timeout_s) for i in range(bag)]
        for f in as_completed(futs):
            rows.append(f.result())
    wall = time.perf_counter() - t0
    answered = sum(1 for r in rows if r["status"] != 0)
    timeouts = sum(1 for r in rows if r["kind"] == "timeout")
    kinds = Counter(r["kind"] for r in rows)
    ms = sorted(r["ms"] for r in rows)
    p50 = statistics.median(ms) if ms else 0
    p95 = ms[max(0, int(len(ms) * 0.95) - 1)] if ms else 0
    http_s = answered / wall if wall > 0 else 0
    return {
        "bag": bag,
        "parallel": workers,
        "wall_s": wall,
        "answered": answered,
        "timeouts": timeouts,
        "timeout_pct": 100.0 * timeouts / bag if bag else 0,
        "http_s": http_s,
        "p50": p50,
        "p95": p95,
        "max_ms": max(ms) if ms else 0,
        "kinds": dict(kinds),
        "sample": next((r for r in rows if r["status"] != 0), rows[0] if rows else {}),
    }


def print_wave(tag: str, r: dict) -> None:
    print(
        f"{tag:<8} bag={r['bag']:<5} par={r['parallel']:<5}  "
        f"wall={r['wall_s']:6.2f}s  ok_http={r['answered']}/{r['bag']}  "
        f"timeout={r['timeouts']} ({r['timeout_pct']:5.1f}%)  "
        f"{r['http_s']:7.1f} HTTP/s  p50={r['p50']:.0f}ms p95={r['p95']:.0f}ms "
        f"max={r['max_ms']:.0f}ms",
        flush=True,
    )
    print(f"         kinds {r['kinds']}", flush=True)


def print_caps(ncpu: int) -> int:
    flush = auto_flush_cores(ncpu, desktop=0)
    cap = flush_http_cap(flush)
    print("Match-drain settings under test", flush=True)
    print("  match_drain_min_queue = 1", flush=True)
    print("  match_drain_max_s     = 0   (keep flushing until bag empty / oracles leave)", flush=True)
    print("  match_drain_parallel  = 0   (auto from flush CPU count)", flush=True)
    print("  match_drain_batch     = 0   (whole matching bag, not 64-slice waves)", flush=True)
    print(f"  connection_timeout_s  = {TIMEOUT_S}", flush=True)
    print(flush=True)
    print(f"This box: {ncpu} logical CPUs (desktop_cpu_cores=0, dedicated miner)", flush=True)
    print(f"  auto flush cores     = {flush}", flush=True)
    print(f"  auto /verify cap     = {cap} in-flight", flush=True)
    print("  bag 1–4999            → parallel = cap", flush=True)
    print("  bag 5000+             → at least min(1024, cap)", flush=True)
    print("  bag 25000+            → cap", flush=True)
    print(flush=True)
    print("Same auto cap on other boxes:", flush=True)
    for n in (4, 8, 12, 16, 24, 32):
        f = auto_flush_cores(n, 0)
        c = flush_http_cap(f)
        print(
            f"  {n:>2} cores → flush {f} → {c} in-flight  (~{c * 100}/30s @ 300ms)",
            flush=True,
        )
    print(flush=True)
    return cap


def local_sweep(cap: int, service_ms: float, overload_at: int) -> None:
    state = MockVerify(service_ms, overload_at)
    httpd = ThreadingHTTPServer(("127.0.0.1", 0), make_handler(state))
    port = httpd.server_address[1]
    url = f"http://127.0.0.1:{port}/verify"
    t = threading.Thread(target=httpd.serve_forever, daemon=True)
    t.start()
    print(
        f"LOCAL mock /verify  service={service_ms:.0f}ms  "
        f"overload_hang>{overload_at or 'off'}  timeout={TIMEOUT_S}s",
        flush=True,
    )
    print(f"  {url}", flush=True)
    print(flush=True)

    waves = [1, 8, 32, 64]
    for n in (128, 256, 512, 1024, 2048):
        if n <= cap * 2:
            waves.append(n)
    if cap not in waves:
        waves.append(cap)
    waves = sorted(set(waves))

    seq = 0
    ceiling = None
    for bag in waves:
        par = brute_drain_parallel(0, bag, cap)
        r = run_wave(url, bag, par, TIMEOUT_S, seq)
        seq += bag
        print_wave("LOCAL", r)
        if r["timeout_pct"] > 5.0 and ceiling is None:
            ceiling = r["parallel"]
            print(
                f"         ** timeouts started at parallel={par} "
                f"(peak mock in-flight={state.peak})",
                flush=True,
            )
            if r["timeout_pct"] >= 25.0:
                print("         stopping sweep — pool mock is saturated", flush=True)
                break
        time.sleep(0.2)

    print(flush=True)
    print(
        f"Mock peak in-flight seen by server: {state.peak}  handled={state.handled}",
        flush=True,
    )
    if ceiling:
        print(
            f"Local ceiling (first timeouts): ~{ceiling} in-flight "
            f"with {service_ms:.0f}ms service and overload>{overload_at or 'n/a'}",
            flush=True,
        )
    else:
        print(
            f"Local: no timeouts up to last wave. Auto cap {cap} is safe "
            f"when /verify answers in ~{service_ms:.0f}ms.",
            flush=True,
        )
    httpd.shutdown()


def live_sweep(cap: int, full: bool) -> None:
    print("LIVE dummy /verify (invalid hashes — pool will 401/hold, not credit)", flush=True)
    print(f"  POST {VERIFY_LIVE}  timeout={TIMEOUT_S}s", flush=True)
    print("oracles:", flush=True)
    print(f"  /difficulty  {get(DIFF_LIVE, 8)}", flush=True)
    print(f"  lastblock    {get(LASTBLOCK_LIVE, 4)}", flush=True)
    print(flush=True)

    # Default stays light so we do not slam the pool. --live walks up to auto cap.
    waves = [1, 8, 32, 64, 128]
    if full:
        for n in (256, 512, 1024, 2048):
            if n <= cap:
                waves.append(n)
        if cap not in waves:
            waves.append(cap)
    waves = sorted(set(w for w in waves if w <= max(cap, 128)))

    seq = 10_000_000
    ceiling = None
    for bag in waves:
        par = brute_drain_parallel(0, bag, cap)
        r = run_wave(VERIFY_LIVE, bag, par, TIMEOUT_S, seq)
        seq += bag
        print_wave("LIVE", r)
        sample = r.get("sample") or {}
        if sample:
            print(f"         sample status={sample.get('status')} {sample.get('body')}", flush=True)
        if r["timeout_pct"] > 5.0 and ceiling is None:
            ceiling = par
            print(f"         ** live timeouts started at parallel={par}", flush=True)
            if r["timeout_pct"] >= 15.0:
                print("         stopping live sweep so we do not slam the pool", flush=True)
                break
        time.sleep(1.5)

    print(flush=True)
    if ceiling:
        print(
            f"Live ceiling (first timeouts): ~{ceiling} in-flight. "
            f"Auto cap on this box is {cap}.",
            flush=True,
        )
        if ceiling < cap:
            print(
                f"  Auto {cap} is ABOVE what this path held without timeout. "
                f"A safer match_drain_parallel is {max(32, ceiling // 2)}–{ceiling}.",
                flush=True,
            )
    else:
        print(
            f"Live: dummy POSTs answered up to auto cap {cap} with <5% timeout "
            f"(holds/401 still count as 'answered' — the TCP path did not die).",
            flush=True,
        )


def main() -> int:
    ap = argparse.ArgumentParser(description="Mock match-drain /verify flush")
    ap.add_argument(
        "--live",
        action="store_true",
        help="walk dummy POSTs up to this box's auto cap (default live sweep stops at 128)",
    )
    ap.add_argument("--local-only", action="store_true", help="skip xenblocks.io entirely")
    ap.add_argument("--service-ms", type=float, default=250.0, help="mock /verify think time")
    ap.add_argument(
        "--overload-at",
        type=int,
        default=0,
        help="mock hangs (timeout) when in-flight exceeds this; 0 = never hang",
    )
    args = ap.parse_args()

    print("Mock match-drain  " + datetime.now().strftime("%Y-%m-%d %H:%M:%S"), flush=True)
    ncpu = cpu_count()
    cap = print_caps(ncpu)

    # Local: first a clean pool (no hang), then one that collapses above auto-cap.
    local_sweep(cap, args.service_ms, args.overload_at)
    print(flush=True)
    if args.overload_at <= 0:
        print("--- second local pass: mock hangs if in-flight > auto cap ---", flush=True)
        local_sweep(cap, args.service_ms, overload_at=max(32, cap))
        print(flush=True)

    if args.local_only:
        return 0
    live_sweep(cap, full=args.live)
    return 0


if __name__ == "__main__":
    sys.exit(main())
