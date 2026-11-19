#!/bin/sh
# Tests of WarpHubCLI on an Amiga through AmiAgent, in RAM:WarpHub-test only.
#   sh tools/amiga_test.sh <amiagent.cfg> <step>
# Programs that may take long run with "start" and a log (a "run" that hangs
# blocks AmiAgent); the log is read back after waiting.
set -e
cfg=${1:-C:/amiga-dev/amiagent-2.cfg}
step=$2
here=$(cd "$(dirname "$0")/.." && pwd)
remote="python $here/../Settlers/tools/amiga_remote.py --config $cfg"
out=${TMPDIR:-/tmp}/warphub-test
mkdir -p "$out"
T=RAM:WarpHub-test
CLI="$T/WarpHubCLI"

bg() {  # bg <name> <seconds> <args...>
  name=$1; wait_s=$2; shift 2
  $remote start "$CLI $* >$T/$name.txt" >/dev/null
  i=0
  while [ $i -lt "$wait_s" ]; do
    sleep 5; i=$((i + 5))
    if $remote get "$T/$name.txt" "$out/$name.txt" >/dev/null 2>&1 &&
       grep -q "^Done\|^Up to date\|^Not installed\|^Catalog\|^Usage\|needs\|^No game\|[0-9] files to download" "$out/$name.txt" &&
       ! $remote run "Status COM=WarpHubCLI" 2>/dev/null | grep -q "^ *[0-9]"; then
      break
    fi
  done
  echo "--- $name"; cat "$out/$name.txt"
}

case $step in
  put)     $remote put "$here/build/amiga/WarpHubCLI" "$CLI"
           $remote put "$here/catalog/catalog.txt" "$T/catalog.txt" ;;
  *)       shift 2; bg "$step" 300 "$@" ;;
esac
