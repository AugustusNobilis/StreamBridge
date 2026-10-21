# Lifecycle process results

The lifecycle supervisor creates each tree suspended, assigns a non-breakaway
Windows Job Object, and uses `DEBUG_PROCESS` creation events on its creator
thread to retain process handles before even fast children execute. It drains
debug exit events and waits for an empty job. Actual exit codes come from the
retained handles, not INI status or recycled PIDs. A failed/missing required
role fails the operation. Timeout and disposal terminate only the owned job.
Debug image handles are closed as events arrive; process handles close after
validation. Unexpected exceptions are forwarded, not swallowed.

Updater tests identify the installer by its exact path in the unique download
directory. Restart helper identity also requires the single copied helper's
hash to match the built binary. Each scenario declares exact counts and codes:

| Scenario | Host | Installer | Restart helper |
| --- | --- | --- | --- |
| Default / ImmediateExit | 1 × 0 | 1 × 0 | absent |
| RestartBeforeInstall | two separate host jobs, each 1 × 0 | separate job, 1 × 0 | absent |
| AutoRestart | 2 × 0 | 1 × 0 | 1 × 0 |
| CancelRestart | 1 × 0 after test shutdown | 1 × 1223 | 1 × 0 |
| RejectRestart | 1 × 0 | 1 × 0 | 1 × 3 |

NSIS uninstall separately requires exactly one terminal record whose PID has
a retained exited handle; its OS code must match the record and the expected
result. A launcher code 0 cannot hide a temporary `Un.exe` error. File hashes,
settings preservation and expected result files remain additional assertions.
Production bootstrap updater and restart helper already check the installer's
real handle/exit code; the demonstrated false success was in the test harness.
