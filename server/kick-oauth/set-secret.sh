#!/bin/sh
# Stores an app's client id and secret in /etc/meketreve-kick-oauth.env on
# the token server and restarts it. Run on your own computer:
#
#   server/kick-oauth/set-secret.sh TROVO ubuntu@host [-i ~/.ssh/key]
#
# The secret is typed without echo and goes to the server over ssh's stdin:
# it never lands in a file here, in the shell history or in a command line.
set -eu

if [ $# -lt 2 ]; then
	echo "usage: $0 <KICK|GOOGLE|TROVO> <user@host> [ssh options...]" >&2
	exit 2
fi
provider=$1
shift
case $provider in
KICK | GOOGLE | TROVO) ;;
*)
	echo "unknown provider '$provider' (KICK, GOOGLE or TROVO)" >&2
	exit 2
	;;
esac

printf '%s client id: ' "$provider"
read -r client_id
printf '%s client secret (hidden): ' "$provider"
stty -echo 2>/dev/null || true
trap 'stty echo 2>/dev/null || true' EXIT INT TERM
read -r client_secret
stty echo 2>/dev/null || true
trap - EXIT INT TERM
echo

# Keep only the last word: stray keys (Delete, arrows) and spaces typed
# before a paste end up in front of it.
last_word() {
	set -f
	# shellcheck disable=SC2086
	set -- $1
	set +f
	[ $# -gt 0 ] || return 0
	eval "printf '%s' \"\${$#}\""
}
client_id=$(last_word "$client_id")
client_secret=$(last_word "$client_secret")

case $client_id$client_secret in
*[!A-Za-z0-9._~-]*)
	echo "the id and secret may only have letters, digits and . _ ~ -" >&2
	exit 1
	;;
esac
if [ -z "$client_id" ] || [ -z "$client_secret" ]; then
	echo "empty id or secret, nothing changed" >&2
	exit 1
fi

# The remote side reads both values from stdin, swaps the two lines in the
# env file (root, 0600) and restarts the service.
printf '%s\n%s\n' "$client_id" "$client_secret" | ssh "$@" "sudo -n sh -c '
	set -eu
	umask 077
	env=/etc/meketreve-kick-oauth.env
	read -r id
	read -r secret
	tmp=\$(mktemp)
	touch \"\$env\"
	grep -v -e \"^${provider}_CLIENT_ID=\" -e \"^${provider}_CLIENT_SECRET=\" \"\$env\" >\"\$tmp\" || true
	printf \"%s\n\" \"${provider}_CLIENT_ID=\$id\" \"${provider}_CLIENT_SECRET=\$secret\" >>\"\$tmp\"
	install -m 0600 \"\$tmp\" \"\$env\"
	rm -f \"\$tmp\"
	systemctl restart meketreve-kick-oauth
	sleep 1
	journalctl -u meketreve-kick-oauth -n 1 --no-pager -o cat
'"
