"""Explore message interleavings for small conflict examples.

The examples are sink transitions. This model checks the handshake, not a
general executor. Offer levels travel on reliable FIFO wires. Every transition
in either example belongs to the same local choice group.
"""
from collections import deque
from dataclasses import dataclass, replace


def put(items, index, value):
    return items[:index] + (value,) + items[index + 1:]


@dataclass(frozen=True)
class State:
    tokens: tuple        # held at the places
    places: tuple        # free, sending an offer, offered, or awaiting withdrawal
    wires: tuple         # group-to-transition level changes, FIFO per wire
    missing: tuple       # EDG descriptor at each transition
    requests: tuple      # idle, request sent, pending in group, or grant sent
    past: tuple          # grant sequence, projected onto the reference net


def explore(name, initial, inputs, expected_winners, overlap=None):
    arcs = [(p, t) for t, pre in enumerate(inputs) for p in pre]

    def announce(s, places, level):
        return tuple(q + (level,) if p in places else q
                     for q, (p, _) in zip(s.wires, arcs))

    def successors(s):
        for p, phase in enumerate(s.places):
            if phase == 'free' and s.tokens[p]:
                yield replace(s, places=put(s.places, p, 'sending'))
            if phase == 'sending':
                yield replace(s, places=put(s.places, p, 'offered'),
                              wires=announce(s, (p,), True))
            if phase == 'withdrawing':
                assert s.tokens[p] >= 1
                yield replace(s, places=put(s.places, p, 'free'),
                              tokens=put(s.tokens, p, s.tokens[p] - 1))

        for i, (_, t) in enumerate(arcs):
            q = s.wires[i]
            if q:
                d = s.missing[t] + (-1 if q[0] else 1)
                assert 0 <= d <= len(inputs[t])
                yield replace(s, wires=put(s.wires, i, q[1:]),
                              missing=put(s.missing, t, d))

        for t, phase in enumerate(s.requests):
            pre = inputs[t]
            if phase == 'idle' and s.missing[t] == 0:
                yield replace(s, requests=put(s.requests, t, 'sending'))
            if phase == 'sending':
                yield replace(s, requests=put(s.requests, t, 'pending'))
            if phase == 'pending' and all(s.places[p] == 'offered' for p in pre):
                phases = tuple('spent' if p in pre else old
                               for p, old in enumerate(s.places))
                yield replace(s, places=phases, wires=announce(s, pre, False),
                              requests=put(s.requests, t, 'granted'),
                              past=s.past + (t,))
            if phase == 'granted':
                phases = tuple('withdrawing' if p in pre else old
                               for p, old in enumerate(s.places))
                yield replace(s, places=phases, requests=put(s.requests, t, 'idle'))

    start = State(initial, ('free',) * len(initial), ((),) * len(arcs),
                  tuple(map(len, inputs)), ('idle',) * len(inputs), ())
    work, seen, winners = deque([start]), {start}, set()
    terminals = 0
    overlapping_grants = False
    while work:
        s = work.popleft()
        if overlap and all(s.requests[t] == 'granted' for t in overlap):
            overlapping_grants = True
        marking = list(initial)
        for t in s.past:
            for p in inputs[t]:
                assert marking[p] > 0, 'illegal reference firing'
                marking[p] -= 1
        next_states = list(successors(s))
        if not next_states:
            assert all(any(marking[p] == 0 for p in pre) for pre in inputs)
            assert tuple(marking) == s.tokens
            assert s.past
            winners.add(s.past[0])
            terminals += 1
        for nxt in next_states:
            if nxt not in seen:
                seen.add(nxt)
                work.append(nxt)
    assert winners == expected_winners
    if overlap:
        assert overlapping_grants, 'independent transitions could not overlap'
    print(f'{name}: {len(seen)} states, {terminals} terminal states, '
          f'possible first grants {sorted(winners)}'
          + (', both disjoint grants can be in flight' if overlap else ''))


if __name__ == '__main__':
    explore('A=PQ, B=PQ', (1, 1), ((0, 1), (0, 1)), {0, 1})
    explore('A=PQ, B=PQ, two tokens each', (2, 2), ((0, 1), (0, 1)), {0, 1})
    explore('A=PQ, B=PR, Q empty', (1, 0, 1), ((0, 1), (0, 2)), {1})
    explore('A=P, B=PQ, C=Q', (1, 1), ((0,), (0, 1), (1,)), {0, 1, 2}, overlap=(0, 2))
