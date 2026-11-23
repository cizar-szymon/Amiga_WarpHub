#!/bin/sh
# The private source of WARP HUB on its server (a Raspberry Pi, Debian, user account,
# no root): sends tools/private_server.py, the certificate, the password hash and
# www/ (tools/make_private.py) and (re)starts the server.
#
#   sh tools/push_private.sh user@host [--no-www]
#
# The address is never in the repo: give it on the command line or in PRIVATE_HOST.
# On the server (in ~/warphub-private):
#   private_server.py, cert.pem, key.pem, server.cfg, www/, server.log (kept)
#   run.sh        restarts the server when it stops (10 s pause)
#   crontab       @reboot run.sh - starts after a reboot without root
# www/ is unpacked next to the old one and swapped in one step, so a running
# download never sees half of the new files.
set -eu

HOST=${1:-${PRIVATE_HOST:-}}
WWW=1
[ "${2:-}" = "--no-www" ] && WWW=0
if [ -z "$HOST" ]; then
    echo "usage: sh tools/push_private.sh user@host [--no-www]" >&2
    exit 2
fi

HERE=$(cd "$(dirname "$0")/.." && pwd)
ROOT=${PRIVATE_ROOT:-$(dirname "$HERE")/WarpHub-private}
DIR=warphub-private
SSH="ssh -o BatchMode=yes"

for f in cert.pem key.pem server.cfg www/catalog.txt; do
    [ -f "$ROOT/$f" ] || { echo "missing $ROOT/$f (tools/private_server.py setup, tools/make_private.py)" >&2; exit 1; }
done

echo "== files of the server"
$SSH "$HOST" "mkdir -p $DIR && chmod 700 $DIR"
tar -C "$HERE/tools" -cf - private_server.py | $SSH "$HOST" "tar -C $DIR -xf -"
tar -C "$ROOT" -cf - cert.pem key.pem server.cfg | $SSH "$HOST" "tar -C $DIR -xf - && chmod 600 $DIR/key.pem $DIR/server.cfg"

if [ $WWW = 1 ]; then
    echo "== www/ ($(du -sh "$ROOT/www" | cut -f1))"
    $SSH "$HOST" "rm -rf $DIR/www.new && mkdir $DIR/www.new"
    tar -C "$ROOT/www" -cf - . | $SSH "$HOST" "tar -C $DIR/www.new -xf -"
    $SSH "$HOST" "cd $DIR && rm -rf www.old && { [ ! -d www ] || mv www www.old; } && mv www.new www && rm -rf www.old"
fi

echo "== autostart and restart"
$SSH "$HOST" sh -s <<'EOF'
set -eu
cd ~/warphub-private
cat > run.sh <<'RUN'
#!/bin/sh
# WARP HUB private source: keeps the server running (started by cron @reboot)
cd "$(dirname "$0")"
while :; do
    python3 private_server.py run --root . >>server.out 2>&1
    sleep 10
done
RUN
chmod 755 run.sh
line="@reboot $HOME/warphub-private/run.sh"
{ crontab -l 2>/dev/null | grep -v 'warphub-private/run.sh' || true; echo "$line"; } | crontab -
pkill -f 'warphub-private/run.sh' || true
pkill -f 'private_server.py run' || true
sleep 1
nohup "$HOME/warphub-private/run.sh" >/dev/null 2>&1 &
sleep 3
if pgrep -f 'private_server.py run' >/dev/null; then
    echo "server running"; tail -n 1 server.log
else
    echo "server NOT running:"; tail -n 20 server.out; exit 1
fi
EOF
