# Source and evidence provenance

## Original implementation

- Repository: <https://github.com/kopjakj/petri-nets-edg>
- Snapshot: `6a2fbb670e996390c1292952d693827d05f59675` (2026-11-24).
- `upstream/` contains its tracked files, excluding the external Pico SDK and
  FreeRTOS submodules. No locally generated executables are included there.
- `fw/lib/edg_pn_choice.c` and `.h` are byte-identical to that snapshot.
  Protocol source SHA-256:
  `ef970ed6e12e16ec704e335f0c8593024d2f92c54fa3255f83964c54c75707db`.
- `fw/lib/edg_pn.c/.h` are the anonymous offer/release library from the supplied
  S1 (`EDG_submission_v2.zip`). `fw/lib/edg.c/.h` are the unchanged EDG core.
  The S1 library's licence is retained as `fw/lib/LICENSE`; original notices
  remain in the source. Vendoring does not assert additional licensing rights.
- `fw/nets.h` is S1's fifteen-net definition; `upstream/petri/test/nets.h`
  contains the older eleven-net set. They are intentionally different.

## Additional validation code

The following are study additions, not claims about what Kopják's repository
already supplies:

- `fw/main.c`: single-core FreeRTOS mailbox adapter, firing logger, task-yield
  perturbation, and policy selection; `nets_extra.h`: four additional nets.
- `runner/run.js`: rp2040js driver and reference replay; `nets.json` is exported
  from the C net definitions by `gen_nets.c`.
- `startup_audit/run_corrected.py`: the grouped-only 1,140-run matrix;
  `run_ab.py`: subprocess helper and optional historical startup regression.
  For this release the driver additionally fails on incomplete initialization,
  queue exhaustion and non-target/non-dead endings; `EDG_WORKERS` can limit its
  parallelism (default 20). Neither change alters the experiment matrix or
  archived results, which remain the original records.
- `grouped_search/gx.c`: handler-boundary interleaving explorer;
  `mutants.sh`: six deliberate changes to the protocol, applied only to
  temporary copies. Build paths were made package-relative for this release;
  `gx.c` and the saved logs are unchanged from the study.
- `verify_records.py`: independent Python replay of all archived UART traces,
  source/binary manifest verification, configuration-matrix audit and search
  record aggregation. It was added when preparing this release.

The experimental `grouped-rand` policy changes only the group's scan starting
index in the adapter. It is not Kopják's original round-robin policy, not a
uniform draw among enabled transitions, and not a starvation-prevention fix.

## Recorded emulator experiment

- Executed remotely on Linux x86_64; Node 18.19.1, Python 3.12.3.
- Emulator: rp2040js **1.4.2**, pinned by the npm lockfile.
- Compiled on Linux using Arm GCC **16.2.0**, Pico SDK **2.3.0**
  (`98a542c1a62fb549ffb5d66a3e5892b06276b670`) and Raspberry Pi FreeRTOS
  `4f7299d6ea746b27a9dd19e87af568e34bd65b15`.
- `startup_audit/after.bin`: corrected adapter image, SHA-256
  `cfac1682e6172e76ce56a4598de42948fb163214907f1d974f3396bebbe0a4ff`.
- Its five-entry run manifest matches the packaged image, adapter source,
  protocol source, driver and net JSON. No protocol code was changed for the
  startup correction: initialization was moved into a task after scheduler
  start. The adapter uses a **200 KiB heap**, one core, a 10 Hz tick, disabled
  tickless idle and 512-word task stacks. This is not the original application's
  96 KiB configuration, nor a hardware resource-use measurement.
- `remote_traces.tar.gz` contains all **1,140** corrected-run UART traces and
  the original configure log. `remote_results/` retains the per-run JSON,
  summary, manifest, log and the kitting startup A/B evidence.
- The replay checks a **logical reference marking**, not instantaneous local
  place counts or actuator outputs. The runner's `mismatch` field compares
  declared net topology/initial marking with `nets.json`; it is not a local
  token-balance check. The separate search checks in-flight token accounting.

## Search evidence

The two saved result files contain 42 rows with 16 overlaps: **26 unique
nets**, 12 complete, seven overflow-pruned, seven state-capped. These are
handler-atomic searches with a preloaded startup, not FreeRTOS task-interleaving
searches or simulations of blocking sends. A full queue is pruned, not proved
safe by that explorer. `maxq` means total messages in flight, not maximum
occupancy of one mailbox. The effective state cap was 5,000,000 (a small
overshoot appears in recorded counts).

The emulator's 19 nets and the search's 26 nets are different sets; the search
does not exhaust the emulator matrix. Coverage from finite runs is a lower
bound. No claim of general progress, semantic equivalence, queue bounds,
starvation freedom or physical-board safety follows.

## Scope of this addition

The previously unavailable 1,140-run study, adapter and larger search records
are now included and independently checked. They are separate from the
`check_handshake.py` / `check_lost_request.py` checks. Those scripts were absent
from the historical S1 archive used during the study, but are now present in
the upstream version-2 commit `9be95c7` on which this contribution is based.
The experiments here supplement those checks rather than replace them.

The grouped source and the release/request place core in upstream `9be95c7`
are byte-identical to `fw/lib/edg_pn_choice.c` and `fw/lib/edg_pn.c`, respectively.
The older `upstream/` snapshot is intentionally retained only to reproduce the
historical search logs against the original eleven-net corpus and place core.
