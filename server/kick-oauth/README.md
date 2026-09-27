# Kick token exchange

Kick only gives tokens to requests that carry the app's client secret, and a
plugin cannot hide one. The plugin does the login itself (PKCE, browser back
to `http://localhost:53682/callback`) and sends the one-time code, or a
refresh token, to `POST /kick/token`; `kick_token_proxy.py` adds the client
id and secret and forwards it to `https://id.kick.com/oauth/token` only.

- Standard library Python on `127.0.0.1:8787`, behind Caddy for HTTPS.
- Accepts only `authorization_code` (with `code_verifier` and the plugin's
  redirect URI) and `refresh_token`; 20 requests a minute per IP.
- Logs nothing it receives or returns.

## Install or update (Ubuntu, as root)

```sh
scp -r server/kick-oauth user@host:/tmp/ && ssh user@host 'sudo sh /tmp/kick-oauth/deploy.sh'
```

The first run creates `/etc/meketreve-kick-oauth.env` (root, 0600); fill in
`KICK_CLIENT_ID` and `KICK_CLIENT_SECRET`, then
`sudo systemctl restart meketreve-kick-oauth`. The host name is set in the
`Caddyfile`; the plugin's URL is `kKickTokenProxy` in
`src/tools/unified-chat/chat-accounts.cpp`. Open TCP 80 and 443 in the cloud
firewall too (on Oracle: the VCN's security list).

## Check

```sh
curl https://<host>/health        # ok
sudo journalctl -u meketreve-kick-oauth -f
```
