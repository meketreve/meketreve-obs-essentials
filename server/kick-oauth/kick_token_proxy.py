#!/usr/bin/env python3
"""OAuth token exchange for Meketreve OBS Essentials (Kick, Google).

Kick and Google only issue tokens to requests that carry the app's
client secret, and the plugin's source is public. The plugin does the
authorization itself (the browser coming back to a loopback address, with
PKCE) and sends the code, or a refresh token, to
/kick/token or /google/token; this adds that app's client id
and secret and forwards the request to that provider's token endpoint, and
nowhere else.

Standard library only. Listens on 127.0.0.1 (Caddy in front does HTTPS).
Configuration comes from the environment:
   KICK_CLIENT_ID, KICK_CLIENT_SECRET       Kick app
   GOOGLE_CLIENT_ID, GOOGLE_CLIENT_SECRET   Google "desktop app" client (YouTube)
   LISTEN_PORT                              default 8787
   RATE_PER_MINUTE                          requests per client IP, default 20
A provider without credentials answers 404. Nothing it receives or returns
(codes, tokens) is logged.
"""
import json
import os
import re
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from collections import defaultdict, deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

MAX_BODY = 8 * 1024

# path -> token endpoint, credentials and the redirect URIs the plugin uses.
PROVIDERS = {
    "/kick/token": {
        "token_url": "https://id.kick.com/oauth/token",
        "client_id": os.environ.get("KICK_CLIENT_ID", ""),
        "client_secret": os.environ.get("KICK_CLIENT_SECRET", ""),
        "redirect": re.compile(r"http://localhost:53682/callback"),
    },
    "/google/token": {
        "token_url": "https://oauth2.googleapis.com/token",
        "client_id": os.environ.get("GOOGLE_CLIENT_ID", ""),
        "client_secret": os.environ.get("GOOGLE_CLIENT_SECRET", ""),
        # Desktop clients may use any loopback port.
        "redirect": re.compile(r"http://(127\.0\.0\.1|localhost):\d{2,5}(/callback)?"),
    },
}
PROVIDERS = {path: p for path, p in PROVIDERS.items() if p["client_id"] and p["client_secret"]}
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


def upstream_form(provider: dict, fields: dict[str, str]) -> dict[str, str] | None:
    """What goes to the provider, or None when the request is not one we forward."""
    grant = fields.get("grant_type")
    credentials = {"client_id": provider["client_id"], "client_secret": provider["client_secret"]}
    if grant == "authorization_code":
        if not fields.get("code") or not fields.get("code_verifier"):
            return None
        redirect = fields.get("redirect_uri", "")
        if not provider["redirect"].fullmatch(redirect):
            return None
        return {
            "grant_type": grant,
            "code": fields["code"],
            "redirect_uri": redirect,
            "code_verifier": fields["code_verifier"],
            **credentials,
        }
    if grant == "refresh_token":
        if not fields.get("refresh_token"):
            return None
        return {"grant_type": grant, "refresh_token": fields["refresh_token"], **credentials}
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
        path = self.path.partition("?")[0]
        if path == "/health":
            self.reply(200, b"ok\n", "text/plain")
        else:
            self.reply(404, {"error": "not_found"})

    def do_POST(self):
        provider = PROVIDERS.get(self.path)
        if provider is None:
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
        form = upstream_form(provider, fields)
        if form is None:
            self.reply(400, {"error": "invalid_request"})
            return
        req = upstream_request(provider, form)
        try:
            with urllib.request.urlopen(req, timeout=15) as resp:
                self.reply(resp.status, resp.read())
        except urllib.error.HTTPError as err:
            self.reply(err.code, err.read() or b"{}")
        except (urllib.error.URLError, TimeoutError):
            self.reply(502, {"error": "upstream_unreachable"})
        print(f"{self.path} {grant_label(form)} -> done", file=sys.stderr, flush=True)


def upstream_request(provider: dict, form: dict[str, str]) -> urllib.request.Request:
    headers = {
        "Accept": "application/json",
        "Content-Type": "application/x-www-form-urlencoded",
        "User-Agent": "meketreve-obs-essentials-oauth",
    }
    return urllib.request.Request(
        provider["token_url"], data=urllib.parse.urlencode(form).encode(), headers=headers, method="POST"
    )


def grant_label(form: dict[str, str]) -> str:
    return "code" if form["grant_type"] == "authorization_code" else "refresh"


def main() -> None:
    if not PROVIDERS:
        sys.exit("set KICK_ and/or GOOGLE_ CLIENT_ID and CLIENT_SECRET")
    server = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    print(f"listening on 127.0.0.1:{PORT} for {', '.join(sorted(PROVIDERS))}", file=sys.stderr, flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
