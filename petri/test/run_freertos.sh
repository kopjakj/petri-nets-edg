#!/usr/bin/env bash
# Every net of nets.h under the FreeRTOS POSIX port, with the
# version-1 offer rule and the version-2 release rule, three seeds each.
#   FREERTOS_KERNEL_PATH=/abs/path FIRINGS=10000 JOBS=6 bash run_freertos.sh
# Writes results/freertos.txt. Exit status 1 if any run saw an illegal firing
# or a protocol anomaly.
set -euo pipefail
cd "$(dirname "$0")"
make -s pn_freertos FREERTOS_KERNEL_PATH="${FREERTOS_KERNEL_PATH:?set FREERTOS_KERNEL_PATH}"
FIRINGS=${FIRINGS:-10000}
JOBS=${JOBS:-6}
SEEDS=${SEEDS:-"1 3 5"}
NETS=(fork-join buffer-2 choice-merge kitting mutex mutex-idle philosophers-2 philosophers-3
      efc-pair sensor-choice sensor-choice-2 job-shop job-shop-rev level-crossing car-park)
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
i=0
for n in "${NETS[@]}"; do for d in offer release; do for s in $SEEDS; do
    i=$((i+1)); printf '%03d %s %s %s\n' "$i" "$n" "$s" "$d"
done; done; done | xargs -P "$JOBS" -L1 bash -c \
    './pn_freertos "$1" '"$FIRINGS"' "$2" "$3" > '"$tmp"'/"$0".txt 2>&1 || echo "$0" >> '"$tmp"'/failed'
mkdir -p results
cat "$tmp"/[0-9]*.txt > results/freertos.txt
echo "runs: $(ls "$tmp"/[0-9]*.txt | wc -l), firing target: $FIRINGS, seeds: $SEEDS" >> results/freertos.txt
if [ -s "$tmp/failed" ]; then echo "runs with illegal firings or anomalies: $(tr '\n' ' ' < "$tmp/failed")"; exit 1; fi
echo "no illegal firing, no anomaly"
