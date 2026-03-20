# Profiling TWGS Under Wine

## Quick Resource Monitoring

```bash
# Monitor TWGS process metrics every 5 seconds
bash tools/wine-monitor.sh --pid $(pgrep -f twgs.exe) --interval 5 --duration 30m
```

Output: CSV with timestamp, RSS, FDs, CPU%, threads, TW3 children, wineserver RSS.

## Comparing Stock Wine vs Patched Wine

```bash
bash tools/wine-compare.sh --duration 10m
```

Runs TWGS on both stock and patched Wine, collects metrics, outputs comparison.

## macOS Tools

### Instruments (GUI)

1. Open Instruments.app (part of Xcode)
2. Choose "System Trace" template
3. Attach to TWGS process (`twgs.exe` in process list)
4. Record for desired duration
5. Examine: Thread States (lock contention), System Calls, Virtual Memory

### dtruss (syscall tracing)

```bash
# Trace all syscalls
dtruss -p $(pgrep -f twgs.exe) 2>trace.txt

# Filter to lock-related
dtruss -p $(pgrep -f twgs.exe) 2>&1 | grep -i "lock\|semop\|fcntl"
```

### DTrace (custom probes)

```bash
# File lock operations
dtrace -n 'syscall::fcntl:entry /pid == $target/ { printf("%d %s", tid, probefunc); }' \
  -p $(pgrep -f twgs.exe)
```

## Linux Tools

### strace

```bash
# Syscall summary (counts + timing)
strace -p $(pgrep -f twgs.exe) -f -c

# Lock-specific trace
strace -p $(pgrep -f twgs.exe) -f -e trace=futex -o locks.txt

# File operations
strace -p $(pgrep -f twgs.exe) -f -e trace=open,close,fcntl,flock -o files.txt
```

### Valgrind (memory profiling)

```bash
# Heap profiler
valgrind --tool=massif wine ~/.wine/drive_c/TWGS/twgs.exe
ms_print massif.out.* | head -50

# Thread race detector
valgrind --tool=helgrind wine ~/.wine/drive_c/TWGS/twgs.exe
```

### perf

```bash
# CPU profiling
perf record -p $(pgrep -f twgs.exe) -g -- sleep 30
perf report
```

## Wine Debug Channels

```bash
# Full DLL call trace (MASSIVE output — use for short bursts only)
WINEDEBUG=+relay wine twgs.exe 2>relay.log

# File operations only
WINEDEBUG=+file wine twgs.exe 2>file.log

# Thread operations
WINEDEBUG=+thread wine twgs.exe 2>thread.log

# Lock operations
WINEDEBUG=+mutex wine twgs.exe 2>mutex.log

# All warnings and errors (moderate output)
WINEDEBUG=warn+all wine twgs.exe 2>warn.log
```

## What To Look For

| Symptom | Tool | What it means |
|---|---|---|
| RSS growing steadily | wine-monitor.sh | Memory leak |
| FD count growing | wine-monitor.sh + lsof | Handle leak |
| FIXME count growing | wine-monitor.sh | New code path hitting unimplemented feature |
| High CPU on idle | ps / top | Busy-wait loop or spin lock |
| Wineserver RSS growing | wine-monitor.sh | Server-side leak |
| fcntl/flock in strace | strace | File lock operations (verify our patches work) |
