#!/usr/bin/env bash
# The full exploration table of the paper. Writes results/explore.txt and the
# shortest counterexamples to results/counterexamples.txt.
#
#   ./run_explore.sh          everything except the 60-million-state bounded run
#   FULL=1 ./run_explore.sh   also philosophers-3 with arbitrary choice (bounded)
#
# Needs about 4 GB of RAM (philosophers-3) and 10-15 minutes.
set -u
mkdir -p results
out=results/explore.txt; cex=results/counterexamples.txt
: > "$out"; : > "$cex"
row() { ./pn_explore "$@" | grep -E "states=|state limit" | tee -a "$out"; }

echo "## offer discipline, complete exploration" | tee -a "$out"
for net in fork-join buffer-2 choice-merge kitting mutex mutex-idle philosophers-2; do
    row "$net" credit
    row "$net" 'credit*'
done
row philosophers-3 credit
if [ "${FULL:-0}" = 1 ]; then row philosophers-3 'credit*'; fi

echo "## naive mapping, bounded exploration (1,000,000 states)" | tee -a "$out"
for net in fork-join buffer-2 choice-merge kitting; do
    row --max=1000000 "$net" naive-preset
    row --max=1000000 "$net" naive-eager
done

# length of the shortest trace to an illegal firing, per naive run
: > results/naive_shortest.txt
for net in fork-join buffer-2 choice-merge kitting; do
    for d in naive-preset naive-eager; do
        echo -n "$net $d " >> results/naive_shortest.txt
        ./pn_explore -v --max=1000000 "$net" "$d" | grep -A1 "first illegal firing" \
            | grep -o "trace ([0-9]* steps" | head -1 >> results/naive_shortest.txt
    done
done

# shortest counterexamples and stall traces
./pn_explore -v --max=300000 fork-join    naive-preset >> "$cex"
./pn_explore -v --max=300000 choice-merge naive-preset >> "$cex"
./pn_explore -v --max=300000 buffer-2     naive-eager  >> "$cex"
./pn_explore -v              mutex-idle   credit       >> "$cex"
./pn_explore -v              philosophers-2 'credit*'  >> "$cex"
echo "done: $out, $cex"
