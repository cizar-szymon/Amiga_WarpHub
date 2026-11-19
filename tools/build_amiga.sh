#!/bin/sh
# Build the Amiga programs in Docker (bebbo amiga-gcc, as Settlers / AmiXcom).
#   sh tools/build_amiga.sh [clean]     -> build/amiga/WarpHubCLI
# Needs the AmiSSL SDK in C:\amiga-dev\amissl.
set -e
cd "$(dirname "$0")/.."
root=$(pwd -W 2>/dev/null || pwd)
amissl=${AMISSL_SDK:-C:/amiga-dev/amissl}
MSYS_NO_PATHCONV=1 docker run --rm -v "$root:/src" -v "$amissl:/amissl:ro" \
  sacredbanana/amiga-compiler:m68k-amigaos \
  sh -c "export PATH=/opt/amiga/bin:\$PATH; cd /src/amiga && make ${1:-all} && \
         m68k-amigaos-nm ../build/amiga/WarpHubCLI >/dev/null 2>&1; ls -l ../build/amiga"
