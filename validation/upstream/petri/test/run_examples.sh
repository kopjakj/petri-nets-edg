#!/usr/bin/env bash
# Validation of the three application examples (closed nets A1-A3 of nets.h):
#   * the task layer under the FreeRTOS POSIX port, FIRINGS firings x 3 seeds;
#   * a BOUNDED breadth-first exploration (MAXS states) of the step functions
#     with the rotating choice, started with the initial marking already queued
#     (--preload) -- these nets are too large to explore completely.
# Writes results/examples_freertos.txt and results/examples_explore.txt.
set -u
FIRINGS="${FIRINGS:-1000000}"
MAXS="${MAXS:-30000000}"
mkdir -p results
fr=results/examples_freertos.txt; ex=results/examples_explore.txt
: > "$fr"; : > "$ex"
rc=0
for net in iot-gateway batch-mixer ev-site; do
    for seed in 1 2 3; do
        ./pn_freertos "$net" "$FIRINGS" "$seed" | tee -a "$fr" || rc=1
    done
done
for net in iot-gateway batch-mixer ev-site; do
    ./pn_explore --preload --max="$MAXS" "$net" credit | grep -E "states=|state limit" | tee -a "$ex"
done
exit $rc
