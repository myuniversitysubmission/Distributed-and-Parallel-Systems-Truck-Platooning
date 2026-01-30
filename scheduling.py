# scheduling.py
#
# Simple schedulability analysis for the truck platooning server
# using pyCPA (Static Priority Preemptive = SPP).

from pycpa import model, analysis, schedulers

# ---- 1) Create system and CPU resource ----

sys = model.System("TruckPlatooningServer")

# Static-Priority-Preemptive scheduler (SPP / FPPS)
cpu = sys.bind_resource(
    model.Resource("CPU", schedulers.SPPScheduler())
)

# ---- 2) Define tasks with WCET, BCET, period, deadline ----
#
#Time unit: ms


C_rx = 0.5   # ms  (WCET of ServerRxHandler)
T_rx = 10.0    # ms  (period)
D_rx = 10.0    # ms  (deadline)

C_tx = 0.1   # ms  (WCET of ServerTxHandler per message)
T_tx = 10.0    # ms
D_tx = 10.0    # ms

C_emerg = 0.2  # ms  (WCET of urgentBrakeAll)
T_emerg = 10.0  # ms  (minimum inter-arrival time)
D_emerg = 10.0  # ms

# Task priorities:
# 1 = highest precedence, 3 = least precedence
task_rx = cpu.bind_task(
    model.Task(
        "T_rx",
        wcet=C_rx,
        bcet=C_rx * 0.5,         # assumption: BCET = WCET / 2
        scheduling_parameter=1   # highest priority
    )
)

task_tx = cpu.bind_task(
    model.Task(
        "T_tx",
        wcet=C_tx,
        bcet=C_tx * 0.5,
        scheduling_parameter=2   # less precedence than RX
    )
)

task_emerg = cpu.bind_task(
    model.Task(
        "T_emerg",
        wcet=C_emerg,
        bcet=C_emerg * 0.5,
        scheduling_parameter=3   # least precedence
    )
)

#
# Basit model: periodic with jitter (PJd), J = 0 (no jitter)

task_rx.in_event_model = model.PJdEventModel(P=T_rx, J=0.0)
task_tx.in_event_model = model.PJdEventModel(P=T_tx, J=0.0)
task_emerg.in_event_model = model.PJdEventModel(P=T_emerg, J=0.0)

# ---- 4) Run Analysis ----

print("\n=== Running pyCPA analysis ===")
task_results = analysis.analyze_system(sys)

print("\n=== Schedulability Results ===")
for t in [task_rx, task_tx, task_emerg]:
    r = task_results[t]
    print(f"Task {t.name}:")
    print(f"  WCET        : {t.wcet:.6f} ms")
    print(f"  Period (T)  : {t.in_event_model.P:.3f} ms")
    print(f"  Deadline (D): {t.in_event_model.P:.3f} ms")  # assume D = T
    print(f"  Response time R: {r.wcrt:.6f} ms")
    if r.wcrt <= t.in_event_model.P:
        print("  -> meets its deadline ✅")
    else:
        print("  -> misses its deadline ❌")
    print()
