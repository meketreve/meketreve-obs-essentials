# Token exchange (Kick, Google, Trovo)

Kick, Google and Trovo only give tokens to requests that carry the app's
client secret, and a plugin cannot hide one. The plugin does the login itself (PKCE, browser back
to `http://localhost:53682/callback`) and sends the one-time code, or a
refresh token, to `POST /kick/token`; `kick_token_proxy.py` adds the client
id and secret and forwards it to `https://id.kick.com/oauth/token` only.

- Standard library Python on `127.0.0.1:8787`, behind Caddy for HTTPS.
- Accepts only `authorization_code` (with the plugin's redirect URI, and
  `code_verifier` for Kick and Google) and `refresh_token`; 20 requests a
  minute per IP. Google is `POST /google/token`; Trovo is `POST /trovo/token`
  (no PKCE: Trovo has none), forwarded as JSON to Trovo's `exchangetoken` /
  `refreshtoken`. Trovo only takes https redirect URLs, so the app's OAuth
  URL is `https://<host>/trovo/callback`: it answers with a redirect to the
  plugin's `http://localhost:53684/callback`, passing on only `code`,
  `state` and the error fields.
- Logs nothing it receives or returns.

## Install or update (Ubuntu, as root)

```sh
scp -r server/kick-oauth user@host:/tmp/ && ssh user@host 'sudo sh /tmp/kick-oauth/deploy.sh'
```

The first run creates `/etc/meketreve-kick-oauth.env` (root, 0600). Put each
app's id and secret there from your own computer, without the secret touching
a file, the shell history or a command line:

```sh
server/kick-oauth/set-secret.sh TROVO ubuntu@host -i ~/.ssh/key   # or KICK, GOOGLE
```

It asks for the id and the secret (typed hidden), swaps those two lines in
the env file and restarts the service. The host name is set in the
`Caddyfile`; the plugin's URL is `kKickTokenProxy` in
`src/tools/unified-chat/chat-accounts.cpp`. Open TCP 80 and 443 in the cloud
firewall too (on Oracle: the VCN's security list).

## Check

```sh
curl https://<host>/health        # ok
sudo journalctl -u meketreve-kick-oauth -f
```
