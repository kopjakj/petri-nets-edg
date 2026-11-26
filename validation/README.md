# Additional grouped-offer validation

Source, reproducibility instructions and recorded evidence for the grouped-offer
implementation. No production library or existing example is changed.

- **Emulator:** 19 nets × 2 selection policies × 10 seeds × 3 queue depths =
  1,140 runs, 1,920,420 logged firing decisions, with all raw UART traces.
- **Search:** 26 unique nets; 12 complete, 7 overflow-pruned, 7 state-capped.
- **Scope:** finite, single-core tests; not a general progress, fairness,
  queue-capacity or hardware-safety proof.

Start with [REPRODUCIBILITY.md](REPRODUCIBILITY.md). Source versions and the
distinction between the original protocol and the experimental adapter are in
[PROVENANCE.md](PROVENANCE.md).

Verify the archived evidence without running an emulator:

```sh
cd validation
sha256sum -c SHA256SUMS.txt
python3 -I test_verify_records.py
python3 -I verify_records.py
```
