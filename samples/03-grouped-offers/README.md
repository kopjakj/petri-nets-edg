# Competing transitions with grouped offers

P and Q each hold one token. A and B each need one token from **both** places.
If P independently offers to A while Q offers to B, both transitions wait.
In this example the two places send their offers to the same choice gate.
The first eligible request gets the complete pair and produces one Done token.

## Run on a Linux host

From this directory, run:

```sh
make run
make concurrent
make check
```

A C11 compiler, GNU Make and a linker with section garbage collection are
sufficient for the scheduled example. The concurrent example also uses POSIX threads. The first example uses separate logical mailboxes and a host scheduler
that can delay individual receivers while maintaining FIFO order at each one.

```text
P=1 Q=1; A and B each need P,Q
  grant A
  P=0 Q=0 DoneA=1 DoneB=0
P=1 Q=0 R=1; A needs P,Q and B needs P,R
  grant B
  DoneA=0 DoneB=1
```

The first winner depends on message delivery order. In the second net Q is
empty, so P's offer stays available to B. The check exercises both winners,
multiple tokens, overlapping input sets, transitive conflicts, independent
groups connected by an output, a self-loop and a cycle. On each grant it
checks legality against an independent token-count model. Finite runs also
check the drained marking and absence of an enabled transition at rest.

`make concurrent` starts one worker for each place, transition and choice group.
A and B compete for 32 P/Q pairs while independent C processes 32 tokens from R.
The final counts must satisfy `DoneA + DoneB = 32` and `DoneC = 32`. The
transport mutex protects message queues. Gate handlers execute outside it.

## How to wire another net

Supply one `edg_choice_arcs_t` per transition. Bit *p* identifies place *p*
on each side. The first example uses places 0=P, 1=Q, 2=DoneA and 3=DoneB:

```c
const edg_choice_arcs_t arcs[] = {
    {EDG_CHOICE_BIT(0) | EDG_CHOICE_BIT(1), EDG_CHOICE_BIT(2)},
    {EDG_CHOICE_BIT(0) | EDG_CHOICE_BIT(1), EDG_CHOICE_BIT(3)}
};
edg_choice_net_t net;
if (!edg_choice_init(&net, 4, arcs, 2)) return EXIT_FAILURE;
```

1. Initialize the wiring before starting workers. Send one INIT to every
   place before any other message at that place, then one DEPOSIT per token.
2. Route each message using `edg_choice_mailbox`. Each mailbox has one active
   handler. Deliver its next event with `edg_choice_step`. The supplied
   `post` callback enqueues messages in order and never calls a handler inline.
   Workers for different mailboxes may run concurrently.
3. A place runs the existing `edg_pn_place_step` with one consumer (its
   choice group), or zero for an output-only place. The group broadcasts
   OPEN/CLOSE events for its offers. Each transition AND updates through
   `edg_gate_step` and can have one request outstanding.
4. A group remembers incomplete requests. In one handler it removes every
   required offer before issuing GRANT. Receipt of GRANT triggers the ordinary
   withdrawals and deposits. A withdrawal from a place with more tokens opens
   a fresh offer even if its marked level stays true.

Input sharing, including chains of shared inputs, determines the groups
automatically. Groups hold offer bits and pending requests. Token counts stay
at their places. The logical firing is the group's grant decision. Physical
place counters can lag while the resulting messages travel. Read final place
counts after draining the mailboxes for a finite run.

The extension supports up to 32 places and 32 transitions, distinct unit arcs,
nonempty transition presets, sinks and self-loops. Transport must deliver every
message exactly once in FIFO order per receiver and provide enough storage for
outbound events. A failed handler is a terminal error. Counters must stay
representable throughout execution. The checks exercise a Linux host transport. Hardware timing and general
starvation guarantees remain outside their scope.

## Reproducing the manuscript checks

Run `make run check` for the C samples, `python3 check_handshake.py` for the
four finite sink-transition models and `python3 check_lost_request.py` for the
discarded-request mutation. The recorded outputs are `host_checks.txt`,
`handshake_results.txt` and `lost_request_result.txt`. The scheduled C checker
accounts for outstanding requests after each event. Its waiting count measures
other grants between request and grant for serviced requests only.
