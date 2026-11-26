#!/bin/sh
# The private source of WARP HUB on its server (a Raspberry Pi, Debian, user account,
# no root): sends tools/private_server.py, the certificate, the password hash and
# www/ (tools/make_private.py) and (re)starts the server.
#
#   sh tools/push_private.sh user@host [--no-www | --full] [--take-over KEY ...]
#   sh tools/push_private.sh user@host --codes    only codes.json (no restart:
#                                                 the server reads it again)
#
# The address is never in the repo: give it on the command line or in PRIVATE_HOST.
# On the server (in ~/warphub-private):
#   private_server.py, cert.pem, key.pem, server.cfg, codes.json, www/,
#   server.log (kept)
#   run.sh        restarts the server when it stops (10 s pause)
#   crontab       @reboot run.sh - starts after a reboot without root
# www/: only the new and changed files are sent (tools/private_sync.py: the
# server's www/ copied with hard links, the changes unpacked into the copy, the
# copy swapped in one step), so a running download never sees half of the new
# files; --full sends the whole www/ (about 2.5 GB) as before.
set -eu

HOST=${1:-${PRIVATE_HOST:-}}
WWW=1
[ "${2:-}" = "--no-www" ] && WWW=0
[ "${2:-}" = "--full" ] && WWW=2
MODE=${2:-}
# --take-over KEY (after the host, repeatable): replace a game the server keeps
TAKE=
shift || true
while [ $# -gt 0 ]; do
    if [ "$1" = "--take-over" ] && [ $# -ge 2 ]; then TAKE="$TAKE --take-over $2"; shift; fi
    shift
done
if [ -z "$HOST" ]; then
    echo "usage: sh tools/push_private.sh user@host [--no-www | --full | --codes]" >&2
    exit 2
fi

HERE=$(cd "$(dirname "$0")/.." && pwd)
ROOT=${PRIVATE_ROOT:-$(dirname "$HERE")/WarpHub-private}
DIR=warphub-private
SSH="ssh -o BatchMode=yes"

for f in cert.pem key.pem server.cfg www/catalog.txt; do
    [ -f "$ROOT/$f" ] || { echo "missing $ROOT/$f (tools/private_server.py setup, tools/make_private.py)" >&2; exit 1; }
done

if [ "$MODE" = "--codes" ]; then
    [ -f "$ROOT/codes.json" ] || { echo "no $ROOT/codes.json (private_server.py code ...)" >&2; exit 1; }
    tar -C "$ROOT" -cf - codes.json | $SSH "$HOST" "tar -C $DIR -xf - && chmod 600 $DIR/codes.json"
    echo "codes.json sent; the server takes it at the next login"
    exit 0
fi

echo "== files of the server"
$SSH "$HOST" "mkdir -p $DIR && chmod 700 $DIR"
tar -C "$HERE/tools" -cf - private_server.py game_files.py make_catalog.py make_private.py paths.py | \
    $SSH "$HOST" "tar -C $DIR -xf -"
# extra files of the server kept outside the repo (<root>/server/), if any;
# run.sh starts <root>/server/run.py instead of private_server.py when it is there
if [ -d "$ROOT/server" ]; then
    tar -C "$ROOT/server" --exclude=__pycache__ -cf - . | $SSH "$HOST" "tar -C $DIR -xf -"
fi
CODES=
[ -f "$ROOT/codes.json" ] && CODES=codes.json
tar -C "$ROOT" -cf - cert.pem key.pem server.cfg $CODES | $SSH "$HOST" "tar -C $DIR -xf - && chmod 600 $DIR/key.pem $DIR/server.cfg"

if [ $WWW = 1 ]; then
    echo "== www/: the changes ($(du -sh "$ROOT/www" | cut -f1) in all)"
    python "$HERE/tools/private_sync.py" "$HOST" "$ROOT/www" "$DIR" $TAKE
elif [ $WWW = 2 ]; then
    echo "== www/ ($(du -sh "$ROOT/www" | cut -f1), all of it)"
    $SSH "$HOST" "rm -rf $DIR/www.new && mkdir $DIR/www.new"
    tar -C "$ROOT/www" -cf - . | $SSH "$HOST" "tar -C $DIR/www.new -xf -"
    # the paths the server keeps (keep.txt) go over from the old www/
    $SSH "$HOST" "cd $DIR && if [ -f keep.txt ]; then while read -r p; do [ -n \"\$p\" ] && [ -e \"www/\$p\" ] && \
        rm -rf \"www.new/\$p\" && mkdir -p \"\$(dirname www.new/\$p)\" && cp -al \"www/\$p\" \"www.new/\$p\"; done < keep.txt; fi"
    $SSH "$HOST" "cd $DIR && rm -rf www.old && { [ ! -d www ] || mv www www.old; } && mv www.new www && rm -rf www.old"
    $SSH "$HOST" "cd $DIR && if [ -f after_sync.py ]; then python3 after_sync.py; fi"
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
    if [ -f run.py ]; then S=run.py; else S=private_server.py; fi
    python3 $S run --root . >>server.out 2>&1
    sleep 10
done
RUN
chmod 755 run.sh
line="@reboot $HOME/warphub-private/run.sh"
{ crontab -l 2>/dev/null | grep -v 'warphub-private/run.sh' || true; echo "$line"; } | crontab -
pkill -f 'warphub-private/run.sh' || true
pkill -f 'private_server.py run' || true
pkill -f 'run.py run --root' || true
sleep 1
nohup "$HOME/warphub-private/run.sh" >/dev/null 2>&1 &
sleep 3
if pgrep -f 'private_server.py run|run.py run --root' >/dev/null; then
    echo "server running"; tail -n 1 server.log
else
    echo "server NOT running:"; tail -n 20 server.out; exit 1
fi
EOF
