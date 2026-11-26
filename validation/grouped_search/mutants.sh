#!/usr/bin/env bash
# Each mutant changes one site of a temporary copy of edg_pn_choice.c. It is
# detected if, on any net, the explorer reports an illegal grant, a protocol
# error, a stall or a wrong final count, or reaches fewer reference markings
# than the unmodified module.
set -u
here=$(cd "$(dirname "$0")" && pwd)
R=${REPO:-$here/../upstream}
orig=$R/petri/edg_pn_choice.c
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
export GX_MAX=${GX_MAX:-300000}
cov() { sed -E 's/^([^ ]+).* markings=([0-9]+)\/.*/\1 \2/'; }
"$here/gx_repo" | grep states= | cov > "$tmp/base.txt"
rc=0
mutant() {
    local name=$1 old=$2 new=$3
    if ! python3 -I -c 'import sys; s=open(sys.argv[1]).read(); assert s.count(sys.argv[3])==1; open(sys.argv[2],"w").write(s.replace(sys.argv[3],sys.argv[4]))' \
        "$orig" "$tmp/edg_pn_choice.c" "$old" "$new" 2>/dev/null; then
        echo "SITE?     $name"; rc=1; return
    fi
    LIB=$tmp/edg_pn_choice.c OUT=$tmp bash "$here/build.sh" >/dev/null 2>&1
    "$tmp/gx_repo" | grep states= > "$tmp/out.txt"
    local faults lost
    faults=$(grep -E "illegal=[1-9]|errors=[1-9]|lost=[1-9]|balance=[1-9]|stalls=[1-9]|badfinal=[1-9]" "$tmp/out.txt" | awk '{print $1}' | tr '\n' ' ')
    lost=$(cov < "$tmp/out.txt" | paste -d' ' "$tmp/base.txt" - | awk '$4<$2{printf "%s(%d<%d) ", $1, $4, $2}')
    if [ -n "$faults$lost" ]; then
        echo "DETECTED  $name  faults: ${faults:-none}  coverage loss: ${lost:-none}"
    else
        echo "MISSED    $name"; rc=1
    fi
}
mutant "no-clear"     "g->open &= ~inputs;                 /* the firing decision */" "/* removed */"
mutant "no-close"     "broadcast(n, p, EDG_CHOICE_CLOSE, post, ctx);" "{}"
mutant "drop-pending" "            continue;
        g->open &= ~inputs;" "        { g->pending &= ~EDG_CHOICE_BIT(t); continue; }
        g->open &= ~inputs;"
mutant "no-merge"     "net->group_of_transition[v] = t;" "{}"
mutant "no-rerequest" "        s->requested = false;
        for" "        for"
mutant "grant-first"  "        for (unsigned p = 0; p < n->places; ++p)
            if (inputs & EDG_CHOICE_BIT(p)) broadcast(n, p, EDG_CHOICE_CLOSE, post, ctx);
        send(post, ctx, EDG_CHOICE_GRANT, t);" "        send(post, ctx, EDG_CHOICE_GRANT, t);
        for (unsigned p = 0; p < n->places; ++p)
            if (inputs & EDG_CHOICE_BIT(p)) broadcast(n, p, EDG_CHOICE_CLOSE, post, ctx);"
exit $rc
