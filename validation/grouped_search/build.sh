#!/usr/bin/env bash
# Build the exhaustive grouped-offer explorer against the GitHub sources.
#   gx_repo: nets of the repository's petri/test/nets.h
#   gx_s1:   the fifteen paper nets of S1 sims/explorer/nets.h
set -eu
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "${OUT:-$here}"
R=${REPO:-$here/../upstream}
LIB=${LIB:-$R/petri/edg_pn_choice.c}
SRC="$here/gx.c $LIB $R/petri/edg_pn.c $R/samples/common/edg.c"
INC="-I$R/petri -I$R/samples/common -I$R/samples/common/test/inc"
F="-std=c11 -O2 -Wall -Wextra -ffunction-sections -fdata-sections -Wl,--gc-sections"
cc $F $INC -I$R/petri/test $SRC -o "${OUT:-$here}/gx_repo"
cc $F $INC -I"$here/s1nets" $SRC -o "${OUT:-$here}/gx_s1"
# gx_s1lib: the GitHub grouped module on S1's release-and-request library
S1=${S1:-$here/../fw/lib}
if [ -d "$S1" ]; then
    iso=$(mktemp -d)
    cp "$LIB" "$R/petri/edg_pn_choice.h" "$iso/"
    cc $F -DS1LIB -I"$iso" -I$S1 -I$R/samples/common/test/inc -I"$here/s1nets" "$here/gx.c" "$iso/edg_pn_choice.c" "$S1/edg_pn.c" "$S1/edg.c" -o "${OUT:-$here}/gx_s1lib"
    rm -rf "$iso"
fi
