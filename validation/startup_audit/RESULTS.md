# Corrected-startup grouped-offer emulator results

## Outcome

**All 1,140 remote runs completed: no illegal firings, protocol anomalies,
stalls, queue-full events, malformed trace lines or net mismatches.**

- 19 models × 2 grouped policies × 10 seeds × 3 queue capacities.
- Queue capacities: 2, 4 and 16.
- 960 runs reached the 2,000-firing target.
- 180 runs reached a dead marking of their reference Petri net: job-shop,
  job-shop-rev and philosophers-3-split (60 each). These are not protocol stalls.
- Total: **1,920,420 firings**.
- All 1,140 configurations are distinct, and every initialization completed.
- Remote execution time: 343.8 seconds.

| Queue depth | Runs | Target | Reference-net dead | Queue-full | Stalls |
|---|---:|---:|---:|---:|---:|
| 2 | 380 | 320 | 60 | 0 | 0 |
| 4 | 380 | 320 | 60 | 0 | 0 |
| 16 | 380 | 320 | 60 | 0 | 0 |

## Cause of the earlier failures

The original **test adapter**, not Kopják's grouped protocol, preloaded INIT
and all initial tokens before starting FreeRTOS. Some places needed more
messages than their two-slot queue could hold.

The kitting A/B test confirms:

| Startup | Scheduler started | Firings | Result |
|---|---|---:|---|
| Original | no | 0 | `QFULL box=0 started=0 kind=1 id=0` |
| Corrected | yes | 2,000 | target reached; no error |

The original runner stopped at the first QFULL line. It did **not** observe a
three-second runtime timeout. The earlier claim that those results demonstrate
a grouped-protocol deadlock at queue depth 2 is **withdrawn**.

## What changed

Only the adapter's startup sequence: a task sends all INIT messages and then
initial DEPOSIT messages after the scheduler starts. Workers can drain queues
while the initializer waits for space.

Kopják's `edg_pn_choice.c` is unchanged, SHA-256:
`ef970ed6e12e16ec704e335f0c8593024d2f92c54fa3255f83964c54c75707db`.

Half the runs use his group selection; half use the adapter's experimental
random scan start. The old offer/release and central-player policies were
**not** run in this retest.

## Evidence

Local copies in `remote_results/`:
`corrected.jsonl`, `corrected_summary.json`, `corrected_manifest.json`,
`corrected.log`, and the kitting A/B traces.

All per-run traces remain on the remote machine in
`~/edg_pn_study/startup_audit/traces/`.

## Limits

These are bounded, single-core emulator runs, not an exhaustive search or a
general progress/fairness proof. Passing at depth 2 on these models does not
establish a universal queue bound. Notification growth under adversarial
scheduling remains a separate concern.
