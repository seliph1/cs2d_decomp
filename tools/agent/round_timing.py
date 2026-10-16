#!/usr/bin/env python3
"""Measure the round restart cadence of a dedicated server (the original or ours).

Usage: python tools/agent/round_timing.py <seconds> [--client-after <s> -- <client cmd...> --] <command...>

Runs the command, polls its merged stdout/stderr every 20 ms and prints every new
line prefixed with the elapsed milliseconds since the start and the delta to the
previous line. The child is killed when the observation window ends. With
`--client-after` a second command (our client) is started that many seconds later,
so the same run covers the "one player" case; its output goes to round_timing_client.log.

Used to check the "0 or 1 player in standard mode" round restart timer against
`cs2d_dedicated.exe` (see `docs/agents/knowledge/standard-gamemode.md`).
"""
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "tools", "agent", "out")
LOG = os.path.join(OUT, "round_timing.log")
CLIENT_LOG = os.path.join(OUT, "round_timing_client.log")


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    secs = float(sys.argv[1])
    argv = sys.argv[2:]
    client_cmd = None
    client_after = None
    if "--client-after" in argv:
        i = argv.index("--client-after")
        client_after = float(argv[i + 1])
        rest = argv[i + 2:]
        if rest and rest[0] == "--":
            rest = rest[1:]
        j = rest.index("--")
        client_cmd = rest[:j]
        argv = argv[:i] + rest[j + 1:]
    cmd = argv

    # The child appends to the log; we poll it and timestamp each new line.
    os.makedirs(OUT, exist_ok=True)
    with open(LOG, "w", encoding="utf-8", errors="replace"):
        pass
    with open(LOG, "ab") as log:
        p = subprocess.Popen(cmd, stdout=log.fileno(), stderr=subprocess.STDOUT, cwd=os.getcwd())

    t0 = time.time()
    prev = t0
    pos = 0
    cli = None
    try:
        while time.time() - t0 < secs:
            time.sleep(0.02)
            if client_cmd is not None and cli is None and time.time() - t0 >= client_after:
                cfd = open(CLIENT_LOG, "wb")
                cli = subprocess.Popen(client_cmd, stdout=cfd.fileno(), stderr=subprocess.STDOUT,
                                       cwd=os.getcwd())
            with open(LOG, "rb") as f:
                f.seek(pos)
                data = f.read()
                pos = f.tell()
            for raw in data.split(b"\n"):
                if not raw.strip():
                    continue
                now = time.time()
                text = raw.decode("utf-8", "replace").replace("\r", "")
                print("[%7d ms] (+%5d) %s" % (int((now - t0) * 1000), int((now - prev) * 1000), text))
                prev = now
    finally:
        p.kill()
        p.wait()
        if cli is not None:
            cli.kill()
            cli.wait()
    return 0


if __name__ == "__main__":
    sys.exit(main())
