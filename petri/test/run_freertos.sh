#!/usr/bin/env bash
# Run every validation net under the FreeRTOS POSIX port: FIRINGS firings per
# run, three seeds each. Exit status is non-zero if any run saw an illegal
# firing or a protocol anomaly.
set -u
FIRINGS="${FIRINGS:-1000000}"
rc=0
for net in fork-join buffer-2 choice-merge kitting mutex mutex-idle philosophers-2 philosophers-3; do
    for seed in 1 2 3; do
        ./pn_freertos "$net" "$FIRINGS" "$seed" || rc=1
    done
done
exit $rc
