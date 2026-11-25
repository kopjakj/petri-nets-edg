#!/usr/bin/env bash
# Full exploration campaign. Writes results/ next to this script.
#
#   bash run_explore.sh                  all sections
#   bash run_explore.sh validation       one or more of: validation embedded
#                                        naive traces tight mutants
#   JOBS=3 MAX=2000000 bash run_explore.sh
#
# validation : 11 small nets x designs offer, offer*, release, release*
#              (initialization messages posted in any order)
# embedded   : job shop (two consumer orders), level crossing, car park,
#              with the initial messages preloaded as the firmware does
# naive      : direct wiring, bounded at 1,000,000 states, and the length of
#              the shortest illegal firing
# traces     : shortest counterexample and stall traces
# tight      : one queue slot per place and per transition input
# mutants    : deliberate faults in lib/edg_pn.c must be detected
#
# Every run starts afresh. RESUME=1 keeps the finished searches in
# results/explore/<net>.txt and runs only the missing ones (after an interruption).
# A search that reaches MAX states is reported as INCOMPLETE: its coverage is
# a lower bound. Each search needs well under 1 GB of RAM; JOBS sets
# how many run at the same time (default 3). The campaign takes about ten minutes.
set -euo pipefail
cd "$(dirname "$0")"
make -s pn_explore
[ "${RESUME:-0}" = 1 ] || rm -rf results/explore
mkdir -p results/explore
MAX=${MAX:-2000000}
JOBS=${JOBS:-3}
DESIGNS=(offer 'offer*' release 'release*')
VALIDATION=(fork-join buffer-2 choice-merge kitting mutex mutex-idle philosophers-2
            philosophers-3 efc-pair sensor-choice sensor-choice-2)
EMBEDDED=(job-shop job-shop-rev level-crossing car-park)

one_net() {   # net, extra-flags -> results/explore/<net>.txt (resumes per design)
    local net=$1 flags=$2 out=results/explore/$1.txt
    touch "$out"
    for d in "${DESIGNS[@]}"; do
        if grep -qE "^$net +${d//\*/\\*} " "$out"; then continue; fi
        ./pn_explore $flags --max="$MAX" "$net" "$d" \
            | grep -E 'states=|INCOMPLETE|keep every' > "$out.part"
        cat "$out.part" >> "$out"; rm -f "$out.part"
    done
    echo "done $net"
}
export -f one_net; export MAX
export DESIGNS_STR="${DESIGNS[*]}"

sections=("$@")
[ ${#sections[@]} -eq 0 ] && sections=(validation embedded naive traces tight mutants)

for s in "${sections[@]}"; do
case $s in
validation)
    printf '%s\n' "${VALIDATION[@]}" | xargs -P "$JOBS" -I{} bash -c 'DESIGNS=(offer "offer*" release "release*"); one_net {} ""'
    { for n in "${VALIDATION[@]}"; do echo "# $n"; cat results/explore/$n.txt; done; } > results/validation.txt ;;
embedded)
    printf '%s\n' "${EMBEDDED[@]}" | xargs -P "$JOBS" -I{} bash -c 'DESIGNS=(offer "offer*" release "release*"); one_net {} --preload'
    { for n in "${EMBEDDED[@]}"; do echo "# $n (initial messages preloaded)"; cat results/explore/$n.txt; done; } > results/embedded.txt ;;
naive)
    : > results/naive.txt
    for n in fork-join buffer-2 choice-merge kitting; do
        for d in naive-preset naive-eager; do
            line=$(./pn_explore -v --max=1000000 "$n" "$d")
            echo "$line" | grep -E 'states=|INCOMPLETE' >> results/naive.txt
            echo "$line" | grep -A1 'first illegal firing' | grep -o 'trace ([0-9]* steps' \
                | sed "s/^/    shortest illegal firing: /" >> results/naive.txt || true
        done
    done ;;
traces)
    {
        ./pn_explore -v --max=300000 fork-join naive-preset
        ./pn_explore -v --max=300000 buffer-2 naive-eager
        ./pn_explore -v --max=300000 choice-merge naive-preset
        ./pn_explore -v mutex-idle offer
        ./pn_explore -v sensor-choice offer
        ./pn_explore -v philosophers-2 'offer*'
        ./pn_explore -v efc-pair 'offer*'
        ./pn_explore -v efc-pair 'release*'
    } > results/traces.txt ;;
tight)
    {
        ./pn_explore --tight-queues fork-join offer
        ./pn_explore --tight-queues fork-join release
        ./pn_explore --tight-queues --max="$MAX" sensor-choice release
        ./pn_explore --tight-queues --max="$MAX" mutex-idle release
    } | grep -E '^#|states=|blocked|INCOMPLETE' > results/tight.txt ;;
mutants)
    python3 check_mutants.py | tee results/mutants.txt ;;
*) echo "unknown section $s" >&2; exit 2 ;;
esac
done
