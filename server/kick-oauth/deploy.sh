#!/bin/sh
# Installs or updates the Kick token exchange on an Ubuntu server (run as
# root, from this folder). Keeps an existing /etc/meketreve-kick-oauth.env.
set -eu
cd "$(dirname "$0")"

if ! command -v caddy >/dev/null; then
	apt-get update -q
	apt-get install -y -q debian-keyring debian-archive-keyring apt-transport-https curl gpg
	curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/gpg.key' |
		gpg --dearmor -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg
	curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt' >/etc/apt/sources.list.d/caddy-stable.list
	apt-get update -q
	apt-get install -y -q caddy
fi

# Oracle's Ubuntu images reject everything but SSH in iptables.
for port in 80 443; do
	iptables -C INPUT -p tcp -m state --state NEW -m tcp --dport "$port" -j ACCEPT 2>/dev/null ||
		iptables -I INPUT 5 -p tcp -m state --state NEW -m tcp --dport "$port" -j ACCEPT
done
if command -v netfilter-persistent >/dev/null; then netfilter-persistent save; fi

install -D -m 0644 kick_token_proxy.py /opt/meketreve-kick-oauth/kick_token_proxy.py
install -m 0644 meketreve-kick-oauth.service /etc/systemd/system/meketreve-kick-oauth.service
if [ ! -f /etc/meketreve-kick-oauth.env ]; then
	umask 077
	printf 'KICK_CLIENT_ID=\nKICK_CLIENT_SECRET=\n' >/etc/meketreve-kick-oauth.env
	echo "fill in /etc/meketreve-kick-oauth.env, then: systemctl restart meketreve-kick-oauth"
fi
chmod 0600 /etc/meketreve-kick-oauth.env
install -m 0644 Caddyfile /etc/caddy/Caddyfile

systemctl daemon-reload
systemctl enable --now meketreve-kick-oauth
systemctl restart meketreve-kick-oauth
systemctl reload caddy || systemctl restart caddy
systemctl --no-pager --lines=3 status meketreve-kick-oauth caddy | grep -E 'Active|listening' || true
