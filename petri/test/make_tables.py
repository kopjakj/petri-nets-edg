#!/usr/bin/env python3
"""Turn results/*.txt into the LaTeX tables and totals quoted in the paper.

Writes ../../publication/mdpi-petri/generated_tables.tex and prints the totals,
so every number in the manuscript can be regenerated from the result files.
"""
import re, sys, pathlib
here = pathlib.Path(__file__).resolve().parent
res = here / "results"
out = here.parent.parent / "publication" / "mdpi-petri" / "generated_tables.tex"

def fmt(n): return f"{n:,}"
NAME = {"fork-join": "fork--join", "choice-merge": "choice--merge"}

rows = []
for line in (res / "explore.txt").read_text().splitlines():
    m = re.match(r"(\S+)\s+(\S+)\s+states=(\d+)\s+steps=(\d+)\s+firings=(\d+)\s+unsound=(\d+)\s+anomalies=(\d+)\s+"
                 r"quiescent=(\d+)\s+stalls=(\d+)\s+markings=(\d+)/(\d+)\s+edges=(\d+)/(\d+)\s+maxq=(\d+)", line)
    if m:
        g = m.groups()
        rows.append(dict(net=g[0], design=g[1], states=int(g[2]), steps=int(g[3]), firings=int(g[4]), unsound=int(g[5]),
                         anomalies=int(g[6]), quiescent=int(g[7]), stalls=int(g[8]), mk=(int(g[9]), int(g[10])),
                         ed=(int(g[11]), int(g[12])), maxq=int(g[13])))

credit = [r for r in rows if r["design"].startswith("credit")]
naive = [r for r in rows if r["design"].startswith("naive")]
complete = [r for r in credit if r["states"] < 60000000]
print("complete credit explorations:", len(complete))
print("  states      :", fmt(sum(r["states"] for r in complete)))
print("  firing steps:", fmt(sum(r["firings"] for r in complete)))
print("  illegal     :", sum(r["unsound"] + r["anomalies"] for r in credit))
print("  max queue   :", max(r["maxq"] for r in credit))
for r in credit:
    print(f'  {r["net"]:15s} {r["design"]:8s} states={r["states"]:>10,} firings={r["firings"]:>9,} stalls={r["stalls"]} '
          f'dead={r["quiescent"]-r["stalls"]} mk={r["mk"]} ed={r["ed"]}')

shortest = {}
for line in (res / "naive_shortest.txt").read_text().splitlines():
    m = re.match(r"(\S+) (\S+) trace \((\d+) steps", line)
    if m: shortest[(m.group(1), m.group(2))] = int(m.group(3))

tex = []
tex.append(r"""\begin{table}[H]
\caption{Bounded exploration (one million states) of the naive mapping. \emph{Illegal firings}: firings not enabled in the net. \emph{Anomalies}: withdrawals from an empty place. \emph{Shortest}: length, in steps of the run-time system, of the shortest trace to an illegal firing.\label{tab:naive}}
\centering
\begin{tabularx}{\linewidth}{@{}l>{\raggedright\arraybackslash}Xrrrr@{}}
\toprule
Net & Start & Firing steps & Illegal firings & Anomalies & Shortest\\
\midrule""")
last = None
for r in naive:
    name = NAME.get(r["net"], r["net"]) if r["net"] != last else ""
    last = r["net"]
    start = "preset" if r["design"] == "naive-preset" else "eager"
    tex.append(f'{name} & {start} & {fmt(r["firings"])} & {fmt(r["unsound"])} & {fmt(r["anomalies"])} & {shortest[(r["net"], r["design"])]}\\\\')
tex.append(r"""\bottomrule
\end{tabularx}
\end{table}""")
naive_tex = "\n".join(tex)

# ---- FreeRTOS runs
runs = {}
for line in (res / "freertos.txt").read_text().splitlines():
    m = re.match(r"(\S+)\s+firings=(\d+)\s+illegal=(\d+)\s+anomalies=(\d+)\s+sends=(\d+)\s+yields=(\d+)\s+stop: (.*)", line)
    if m:
        runs.setdefault(m.group(1), []).append(dict(f=int(m.group(2)), ill=int(m.group(3)), an=int(m.group(4)),
                                                     s=int(m.group(5)), y=int(m.group(6)), stop=m.group(7)))
tex = []
tex.append(r"""\begin{table}[H]
\caption{The task layer under the FreeRTOS POSIX port: three runs (seeds 1--3) per net, summed. \emph{Illegal}: firings rejected by the monitor plus anomalies reported by the library.\label{tab:freertos}}
\centering
\begin{tabularx}{\linewidth}{@{}l>{\raggedright\arraybackslash}Xrrrr@{}}
\toprule
Net & Outcome & Firings & Queue sends & Sends per firing & Illegal\\
\midrule""")
tot_f = 0; ok_runs = 0; tot_ill = 0
for net, rs in runs.items():
    f = sum(r["f"] for r in rs); s = sum(r["s"] for r in rs); ill = sum(r["ill"] + r["an"] for r in rs)
    tot_ill += ill
    reached = all(r["stop"].startswith("target") for r in rs)
    if reached:
        tot_f += f; ok_runs += len(rs)
        outcome = "target reached"; spf = f"{s / f:.2f}"
    else:
        outcome = "stall after first firing"; spf = "--"
    tex.append(f'{NAME.get(net, net)} & {outcome} & {fmt(f)} & {fmt(s)} & {spf} & {ill}\\\\')
tex.append(r"""\bottomrule
\end{tabularx}
\end{table}""")
fr_tex = "\n".join(tex)
print("freertos: runs reaching target:", ok_runs, " firings:", fmt(tot_f), " illegal:", tot_ill)

# ---- application examples (closed nets A1-A3)
NETINFO = {"iot-gateway": ("conflict-free", "12/7"), "batch-mixer": ("marked graph", "16/10"), "ev-site": ("free choice", "11/10")}
EX_CAP = 30000000
ex_runs = {}
exf = res / "examples_freertos.txt"
if exf.exists():
    for line in exf.read_text().splitlines():
        m = re.match(r"(\S+)\s+firings=(\d+)\s+illegal=(\d+)\s+anomalies=(\d+)\s+sends=(\d+)\s+yields=(\d+)\s+stop: (.*)", line)
        if m:
            ex_runs.setdefault(m.group(1), []).append(dict(f=int(m.group(2)), ill=int(m.group(3)) + int(m.group(4)),
                                                           stop=m.group(7)))
ex_expl = {}
exe = res / "examples_explore.txt"
if exe.exists():
    for line in exe.read_text().splitlines():
        m = re.match(r"(\S+)\s+credit\s+states=(\d+)\s+steps=\d+\s+firings=(\d+)\s+unsound=(\d+)\s+anomalies=(\d+)\s+quiescent=(\d+)\s+stalls=(\d+)\s+markings=(\d+)/(\d+)\s+edges=(\d+)/(\d+)", line)
        if m:
            ex_expl[m.group(1)] = dict(states=int(m.group(2)), firings=int(m.group(3)),
                                       ill=int(m.group(4)) + int(m.group(5)), stalls=int(m.group(7)),
                                       mk=(int(m.group(8)), int(m.group(9))), ed=(int(m.group(10)), int(m.group(11))))
tex = []
tex.append(r"""\begin{table}[H]
\caption{Validation of the application examples as closed nets. \emph{FreeRTOS}: three runs (seeds 1--3) under the POSIX port, summed. \emph{Exploration}: breadth-first exploration of the step functions with the rotating choice, started with the initial marking already queued and stopped at 30 million states; \emph{markings} is the part of the net's reachable markings that was reached. \emph{Illegal}: illegal firings plus anomalies.\label{tab:examples}}
\begin{adjustwidth}{-\extralength}{0cm}
\centering
\begin{tabularx}{\fulllength}{@{}l>{\raggedright\arraybackslash}Xcrrrrrr@{}}
\toprule
 & & & \multicolumn{2}{c}{FreeRTOS} & \multicolumn{4}{c}{Exploration}\\
\cmidrule(lr){4-5}\cmidrule(l){6-9}
Net & Class & $|P|/|T|$ & Firings & Illegal & States & Firing steps & Illegal & Markings\\
\midrule""")
ex_f = 0; ex_ok = 0; ex_ill = 0; ex_complete = 0
for net in ("iot-gateway", "batch-mixer", "ev-site"):
    rs = ex_runs.get(net, []); e = ex_expl.get(net)
    f = sum(r["f"] for r in rs); ill = sum(r["ill"] for r in rs)
    if rs and all(r["stop"].startswith("target") for r in rs): ex_f += f; ex_ok += len(rs)
    ex_ill += ill + (e["ill"] if e else 0)
    klass, size = NETINFO[net]
    if e:
        done = e["states"] <= EX_CAP
        ex_complete += done
        st = fmt(e["states"]) if done else fmt(EX_CAP)
        cells = f'{st} & {fmt(e["firings"])} & {e["ill"]} & {e["mk"][0]}/{e["mk"][1]}'
    else:
        cells = '-- & -- & -- & --'
    tex.append(f'{net} & {klass} & {size} & {fmt(f)} & {ill} & {cells}\\\\')
tex.append(r"""\bottomrule
\end{tabularx}
\end{adjustwidth}
\end{table}""")
ex_tex = "\n".join(tex)
print("examples: freertos runs reaching target:", ex_ok, " firings:", fmt(ex_f), " illegal (both levels):", ex_ill,
      " complete explorations:", ex_complete)
for net, e in ex_expl.items(): print("  exploration", net, e)
print("all freertos firings:", fmt(tot_f + ex_f), "in", ok_runs + ex_ok, "runs")

# ---- compact tables for the short manuscript (main.tex) -------------------
CLASS = {"fork-join": "marked graph", "buffer-2": "conflict-free", "choice-merge": "restricted free choice",
         "kitting": "restricted free choice", "mutex": "asym.\\ choice", "mutex-idle": "asym.\\ choice",
         "philosophers-2": "general", "philosophers-3": "general"}
by = {}
for r in credit: by.setdefault(r["net"], {})[r["design"]] = r
bounded = {}
bf = res / "explore_philosophers3_any_bounded.txt"
if bf.exists():
    for line in bf.read_text().splitlines():
        m = re.match(r"(\S+)\s+(credit\*)\s+states=(\d+)\s+steps=\d+\s+firings=(\d+)\s+unsound=(\d+)\s+anomalies=(\d+)\s+quiescent=(\d+)\s+stalls=(\d+)", line)
        if m: bounded[m.group(1)] = dict(states=int(m.group(3)), ill=int(m.group(5)) + int(m.group(6)), stalls=int(m.group(8)))
tex = []
tex.append(r"""\begin{table}[H]
\caption{Results for the offer rule. States are reported for round-robin and arbitrary offer selection. Faults are illegal firings and protocol anomalies summed over the two searches. Stalls are quiescent states with an enabled reference transition, given in round-robin/arbitrary order. FreeRTOS counts cover three POSIX runs per net.\label{tab:results}}
\begin{adjustwidth}{-\extralength}{0cm}
\centering
\begin{tabularx}{\fulllength}{@{}l>{\raggedright\arraybackslash}Xrrcccr@{}}
\toprule
Net & Class & $|P|/|T|$ & Round-robin states & Arbitrary states & Faults & Stalls & FreeRTOS firings\\
\midrule""")
SIZE = {"fork-join": "5/4", "buffer-2": "6/4", "choice-merge": "4/5", "kitting": "4/4", "mutex": "7/6",
        "mutex-idle": "7/6", "philosophers-2": "6/4", "philosophers-3": "9/6"}
for net in ["fork-join", "buffer-2", "choice-merge", "kitting", "mutex", "mutex-idle", "philosophers-2", "philosophers-3"]:
    d = by[net]; rot = d["credit"]; anyr = d.get("credit*")
    if anyr is None and net in bounded:
        anyr_states = "$>$" + fmt(bounded[net]["states"] - 1) + "$^{1}$"; any_ill = bounded[net]["ill"]; any_st = bounded[net]["stalls"]
    else:
        anyr_states = fmt(anyr["states"]); any_ill = anyr["unsound"] + anyr["anomalies"]; any_st = anyr["stalls"]
    ill = rot["unsound"] + rot["anomalies"] + any_ill
    rs = runs.get(net, [])
    ff = sum(r["f"] for r in rs)
    if not rs: frt = "--"
    elif all(r["stop"].startswith("target") for r in rs): frt = fmt(ff)
    else: frt = "stall$^{2}$"
    tex.append(f'{NAME.get(net, net)} & {CLASS[net]} & {SIZE[net]} & {fmt(rot["states"])} & {anyr_states} & {ill} & {rot["stalls"]}/{any_st} & {frt}\\\\')
tex.append(r"""\bottomrule
\end{tabularx}
\end{adjustwidth}
\noindent{\footnotesize{$^{1}$ Stopped at 60 million states, so this exploration is incomplete. $^{2}$ Each run stopped after one firing with an enabled reference transition.}}
\end{table}""")
compact_tex = "\n".join(tex)

tex = []
tex.append(r"""\begin{table}[H]
\caption{Direct wiring with a limit of one million discovered states per search. Each cell gives the number of illegal firing attempts and the shortest trace length in parentheses. The two columns use preset levels and ordinary initialization, respectively.\label{tab:naive}}
\centering
\begin{tabular}{@{}lrr@{}}
\toprule
Net & Preset start & Initialization wave\\
\midrule""")
nv = {}
for r in naive: nv.setdefault(r["net"], {})[r["design"]] = r
for net in ["fork-join", "buffer-2", "choice-merge", "kitting"]:
    p = nv[net]["naive-preset"]; e = nv[net]["naive-eager"]
    tex.append(f'{NAME.get(net, net)} & {fmt(p["unsound"])} ({shortest[(net, "naive-preset")]}) & {fmt(e["unsound"])} ({shortest[(net, "naive-eager")]})\\\\')
tex.append(r"""\bottomrule
\end{tabular}
\end{table}""")
compact_naive_tex = "\n".join(tex)
print("compact tables written")

out.write_text("% Generated by petri/test/make_tables.py from petri/test/results/*.txt -- do not edit.\n"
               "\\newcommand{\\NaiveTable}{%\n" + naive_tex + "}\n"
               "\\newcommand{\\FreeRTOSTable}{%\n" + fr_tex + "}\n"
               f"\\newcommand{{\\FRRuns}}{{{ok_runs}}}\n"
               f"\\newcommand{{\\FRFirings}}{{{fmt(tot_f)}}}\n"
               "\\newcommand{\\ExamplesTable}{%\n" + ex_tex + "}\n"
               "\\newcommand{\\CompactResultsTable}{%\n" + compact_tex + "}\n"
               "\\newcommand{\\CompactNaiveTable}{%\n" + compact_naive_tex + "}\n"
               f"\\newcommand{{\\ExRuns}}{{{ex_ok}}}\n"
               f"\\newcommand{{\\ExFirings}}{{{fmt(ex_f)}}}\n")
print("wrote", out)
