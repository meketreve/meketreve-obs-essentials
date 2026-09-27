#!/usr/bin/env python3
"""Kick OAuth token exchange for Meketreve OBS Essentials.

Kick only issues tokens to requests that carry the app's client secret, and a
plugin cannot hide one. The plugin does the authorization itself (PKCE, with
the browser coming back to http://localhost:53682/callback) and sends the
code, or a refresh token, here; this adds the client id and secret and
forwards the request to Kick, and nowhere else.

Standard library only. Listens on 127.0.0.1 (Caddy in front does HTTPS).
Configuration comes from the environment:
  KICK_CLIENT_ID, KICK_CLIENT_SECRET   the app's credentials
  LISTEN_PORT                          default 8787
  RATE_PER_MINUTE                      requests per client IP, default 20
Nothing it receives or returns (codes, tokens) is logged.
"""
import json
import os
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from collections import defaultdict, deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

KICK_TOKEN_URL = "https://id.kick.com/oauth/token"
REDIRECT_URI = "http://localhost:53682/callback"
MAX_BODY = 8 * 1024

CLIENT_ID = os.environ.get("KICK_CLIENT_ID", "")
CLIENT_SECRET = os.environ.get("KICK_CLIENT_SECRET", "")
PORT = int(os.environ.get("LISTEN_PORT", "8787"))
RATE = int(os.environ.get("RATE_PER_MINUTE", "20"))

_hits: dict[str, deque] = defaultdict(deque)
_hits_lock = threading.Lock()


def allowed(ip: str) -> bool:
    now = time.monotonic()
    with _hits_lock:
        q = _hits[ip]
        while q and now - q[0] > 60:
            q.popleft()
        if len(q) >= RATE:
            return False
        q.append(now)
        if len(_hits) > 10000:  # forget idle clients
            for key in [k for k, v in _hits.items() if not v]:
                del _hits[key]
        return True


def upstream_form(fields: dict[str, str]) -> dict[str, str] | None:
    """What goes to Kick, or None when the request is not one we forward."""
    grant = fields.get("grant_type")
    if grant == "authorization_code":
        if not fields.get("code") or not fields.get("code_verifier"):
            return None
        if fields.get("redirect_uri") != REDIRECT_URI:
            return None
        return {
            "grant_type": grant,
            "code": fields["code"],
            "code_verifier": fields["code_verifier"],
            "redirect_uri": REDIRECT_URI,
            "client_id": CLIENT_ID,
            "client_secret": CLIENT_SECRET,
        }
    if grant == "refresh_token":
        if not fields.get("refresh_token"):
            return None
        return {
            "grant_type": grant,
            "refresh_token": fields["refresh_token"],
            "client_id": CLIENT_ID,
            "client_secret": CLIENT_SECRET,
        }
    return None


class Handler(BaseHTTPRequestHandler):
    server_version = "meketreve-kick-oauth"
    sys_version = ""

    def log_message(self, fmt, *args):  # no request lines: they carry nothing useful
        pass

    def client_ip(self) -> str:
        # Only Caddy on localhost talks to us; it sets X-Forwarded-For.
        return (self.headers.get("X-Forwarded-For") or self.client_address[0]).split(",")[0].strip()

    def reply(self, status: int, body: dict | bytes, content_type: str = "application/json") -> None:
        data = body if isinstance(body, bytes) else json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path == "/health":
            self.reply(200, b"ok\n", "text/plain")
        else:
            self.reply(404, {"error": "not_found"})

    def do_POST(self):
        if self.path != "/kick/token":
            self.reply(404, {"error": "not_found"})
            return
        if not allowed(self.client_ip()):
            self.reply(429, {"error": "slow_down", "error_description": "too many requests, try again in a minute"})
            return
        length = int(self.headers.get("Content-Length") or 0)
        if length <= 0 or length > MAX_BODY:
            self.reply(400, {"error": "invalid_request"})
            return
        fields = {k: v[0] for k, v in urllib.parse.parse_qs(self.rfile.read(length).decode("utf-8", "replace")).items()}
        form = upstream_form(fields)
        if form is None:
            self.reply(400, {"error": "invalid_request"})
            return
        req = urllib.request.Request(
            KICK_TOKEN_URL,
            data=urllib.parse.urlencode(form).encode(),
            headers={"Content-Type": "application/x-www-form-urlencoded", "Accept": "application/json",
                     "User-Agent": "meketreve-obs-essentials-oauth"},
            method="POST",
        )
        try:
            with urllib.request.urlopen(req, timeout=15) as resp:
                self.reply(resp.status, resp.read())
        except urllib.error.HTTPError as err:
            self.reply(err.code, err.read() or b"{}")
        except (urllib.error.URLError, TimeoutError):
            self.reply(502, {"error": "upstream_unreachable"})
        print(f"{grant_label(form)} -> done", file=sys.stderr, flush=True)


def grant_label(form: dict[str, str]) -> str:
    return "code" if form["grant_type"] == "authorization_code" else "refresh"


def main() -> None:
    if not CLIENT_ID or not CLIENT_SECRET:
        sys.exit("KICK_CLIENT_ID and KICK_CLIENT_SECRET must be set")
    server = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    print(f"listening on 127.0.0.1:{PORT}", file=sys.stderr, flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
