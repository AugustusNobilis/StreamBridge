// SPDX-License-Identifier: GPL-2.0-or-later
// Test-only containment and result capture. Debug creation events retain handles
// before fast children execute; a non-breakaway job bounds the entire tree.
using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Collections.Generic;

namespace StreamBridge.Tests {
    public sealed class ProcessJob : IDisposable {
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        struct StartupInfo {
            public int cb; public string reserved, desktop, title;
            public int x, y, cx, cy, xChars, yChars, fill, flags;
            public short show, reservedSize; public IntPtr reservedPtr, input, output, error;
        }
        [StructLayout(LayoutKind.Sequential)]
        struct ProcessInfo { public IntPtr process, thread; public uint pid, tid; }
        [StructLayout(LayoutKind.Sequential)]
        struct BasicLimits {
            public long processTime, jobTime; public uint flags;
            public UIntPtr minWorkingSet, maxWorkingSet; public uint activeLimit;
            public UIntPtr affinity; public uint priority, scheduling;
        }
        [StructLayout(LayoutKind.Sequential)]
        struct ExtendedLimits {
            public BasicLimits basic;
            public ulong readOps, writeOps, otherOps, readBytes, writeBytes, otherBytes;
            public UIntPtr processMemory, jobMemory, peakProcessMemory, peakJobMemory;
        }
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern IntPtr CreateJobObject(IntPtr attributes, string name);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool SetInformationJobObject(IntPtr job, int cls, ref ExtendedLimits info, int size);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool QueryInformationJobObject(IntPtr job, int cls, IntPtr info, int size, IntPtr returned);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern bool CreateProcess(string app, StringBuilder command, IntPtr pa, IntPtr ta,
            bool inherit, uint flags, IntPtr environment, string cwd, ref StartupInfo startup, out ProcessInfo info);
        [DllImport("kernel32.dll", SetLastError = true)] static extern uint ResumeThread(IntPtr thread);
        [DllImport("kernel32.dll", SetLastError = true)] static extern bool TerminateJobObject(IntPtr job, uint code);
        [DllImport("kernel32.dll", SetLastError = true)] static extern bool TerminateProcess(IntPtr process, uint code);
        [DllImport("kernel32.dll")] static extern bool GetExitCodeProcess(IntPtr process, out uint code);
        [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);

        [DllImport("kernel32.dll", SetLastError = true)] static extern bool WaitForDebugEvent(IntPtr info, uint ms);
        [DllImport("kernel32.dll", SetLastError = true)] static extern bool ContinueDebugEvent(uint pid, uint tid, uint status);
        [DllImport("kernel32.dll", SetLastError = true)] static extern bool IsProcessInJob(IntPtr process, IntPtr job, out bool result);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern bool QueryFullProcessImageName(IntPtr process, uint flags, StringBuilder path, ref int size);

        readonly object gate = new object();
        readonly Stopwatch clock = Stopwatch.StartNew();
        readonly Dictionary<int, Process> observed = new Dictionary<int, Process>();
        readonly Dictionary<int, string> images = new Dictionary<int, string>();
        readonly ManualResetEventSlim ready = new ManualResetEventSlim();
        readonly string report;
        readonly int timeoutMs;
        IntPtr job, processHandle;
        Thread monitor;
        volatile bool finished, timedOut, stopping;
        bool disposed;
        string monitorError;
        public Process Root { get; private set; }
        public double Seconds { get { return clock.Elapsed.TotalSeconds; } }
        public bool TimedOut { get { return timedOut; } }
        public int ExitCode { get { return ObservedExitCode(Root.Id); } }
        public int ObservedExitCode(int id) {
            lock (gate) {
                Process p;
                if (!observed.TryGetValue(id, out p) || !p.HasExited)
                    throw new InvalidOperationException("No completed process handle for terminal PID " + id + "; refusing to infer success from a log alone");
                return p.ExitCode;
            }
        }
        public void RequireRole(string role, string executable, int count, int expected) {
            lock (gate) {
                if (!finished || monitorError != null || timedOut) throw new InvalidOperationException("Tree has not completed successfully");
                var path = Path.GetFullPath(executable);
                var ids = new List<int>();
                foreach (var pair in images)
                    if (string.Equals(pair.Value, path, StringComparison.OrdinalIgnoreCase)) ids.Add(pair.Key);
                Log("ROLE " + role + " path=" + path + " count=" + ids.Count + " expectedCount=" + count);
                if (ids.Count != count) throw new InvalidOperationException("Role " + role + " expected " + count + " process(es), observed " + ids.Count);
                foreach (int id in ids) {
                    int actual = ObservedExitCode(id);
                    Log("ROLE " + role + " pid=" + id + " code=" + actual + " expected=" + expected);
                    if (actual != expected) throw new InvalidOperationException("Role " + role + " expected exit " + expected + ", got " + actual + " (parent=" + ExitCode + ")");
                }
            }
        }
        void Log(string message) {
            File.AppendAllText(report, DateTime.UtcNow.ToString("O") + " " + message + Environment.NewLine, new UTF8Encoding(false));
        }
        int[] Members() {
            int size = 8 + IntPtr.Size * 4096;
            IntPtr memory = Marshal.AllocHGlobal(size);
            try {
                if (!QueryInformationJobObject(job, 3, memory, size, IntPtr.Zero)) throw new Win32Exception();
                int count = Marshal.ReadInt32(memory, 4);
                var ids = new int[count];
                for (int i = 0; i < count; ++i) ids[i] = checked((int)Marshal.ReadIntPtr(memory, 8 + i * IntPtr.Size).ToInt64());
                return ids;
            } finally { Marshal.FreeHGlobal(memory); }
        }
        void Created(int id) {
            // CREATE_PROCESS_DEBUG_EVENT suspends the new process. Retain its
            // handle before it can exit, rather than polling/reopening an old PID.
            var p = Process.GetProcessById(id);
            try {
                var handle = p.Handle;
                bool member;
                if (!IsProcessInJob(handle, job, out member) || !member) throw new InvalidOperationException("Debug process outside owned job");
                int size = 32768; var path = new StringBuilder(size);
                if (!QueryFullProcessImageName(handle, 0, path, ref size)) throw new Win32Exception();
                lock (gate) {
                    observed.Add(id, p);
                    images.Add(id, Path.GetFullPath(path.ToString()));
                    Log("PROCESS pid=" + id + " started=" + p.StartTime.ToUniversalTime().ToString("O") + " path=" + images[id]);
                }
            } catch { p.Dispose(); throw; }
        }
        void Snapshot(string reason) {
            lock (gate) {
                Log(reason + " elapsed=" + Seconds.ToString("F3", System.Globalization.CultureInfo.InvariantCulture) + " active=" + string.Join(",", Members()));
                foreach (var pair in observed)
                    Log("STATE pid=" + pair.Key + " exited=" + pair.Value.HasExited + (pair.Value.HasExited ? " code=" + pair.Value.ExitCode : ""));
            }
        }
        void Run(string executable, string arguments, string cwd) {
            ProcessInfo pi = new ProcessInfo();
            IntPtr data = Marshal.AllocHGlobal(176); // DEBUG_EVENT: union offset 16 on x64, 12 on x86.
            int active = 0;
            Stopwatch cleanup = null;
            try {
                var startup = new StartupInfo(); startup.cb = Marshal.SizeOf(startup);
                startup.flags = 1; startup.show = 0;
                // DEBUG_PROCESS covers descendants. The same creator thread must
                // consume debug events. Assignment precedes ResumeThread.
                if (!CreateProcess(executable, new StringBuilder("\"" + executable + "\" " + arguments),
                    IntPtr.Zero, IntPtr.Zero, false, 0x1 | 0x4 | 0x08000000, IntPtr.Zero, cwd, ref startup, out pi)) throw new Win32Exception();
                processHandle = pi.process;
                if (!AssignProcessToJobObject(job, pi.process)) throw new Win32Exception();
                Root = Process.GetProcessById((int)pi.pid); var handle = Root.Handle;
                Log("START pid=" + pi.pid + " executable=" + executable + " timeoutSeconds=" + timeoutMs / 1000);
                if (stopping) TerminateJobObject(job, 124);
                if (ResumeThread(pi.thread) == uint.MaxValue) throw new Win32Exception();
                ready.Set();
                while (true) {
                    if (cleanup == null && (stopping || clock.ElapsedMilliseconds >= timeoutMs)) {
                        timedOut = !stopping;
                        Snapshot(timedOut ? "TIMEOUT" : "CLEANUP");
                        if (!TerminateJobObject(job, 124)) throw new Win32Exception();
                        Log("TERMINATE owned job only code=124");
                        cleanup = Stopwatch.StartNew();
                    }
                    if (cleanup != null && cleanup.ElapsedMilliseconds > 5000) throw new TimeoutException("Owned job cleanup did not finish");
                    if (WaitForDebugEvent(data, 25)) {
                        int kind = Marshal.ReadInt32(data);
                        uint pid = unchecked((uint)Marshal.ReadInt32(data, 4));
                        uint tid = unchecked((uint)Marshal.ReadInt32(data, 8));
                        int offset = IntPtr.Size == 8 ? 16 : 12;
                        uint continuation = 0x00010002; // DBG_CONTINUE
                        try {
                            if (kind == 3) {
                                IntPtr image = Marshal.ReadIntPtr(data, offset);
                                if (image != IntPtr.Zero) CloseHandle(image);
                                Created((int)pid); ++active;
                            } else if (kind == 5) {
                                Log("EXIT_EVENT pid=" + pid + " code=" + Marshal.ReadInt32(data, offset));
                                --active;
                            } else if (kind == 6) {
                                IntPtr image = Marshal.ReadIntPtr(data, offset);
                                if (image != IntPtr.Zero) CloseHandle(image);
                            } else if (kind == 1) {
                                uint exception = unchecked((uint)Marshal.ReadInt32(data, offset));
                                // Only consume loader breakpoints (native/WOW64).
                                // Application faults retain their normal behavior.
                                if (exception != 0x80000003 && exception != 0x4000001f) continuation = 0x80010001;
                            }
                        } finally {
                            if (!ContinueDebugEvent(pid, tid, continuation)) throw new Win32Exception();
                        }
                    } else {
                        int error = Marshal.GetLastWin32Error();
                        if (error != 121) throw new Win32Exception(error);
                    }
                    if (active == 0 && Members().Length == 0) {
                        Snapshot("END tree"); finished = true; return;
                    }
                }
            } catch (Exception ex) {
                lock (gate) { monitorError = ex.ToString(); }
                if (pi.process != IntPtr.Zero) TerminateProcess(pi.process, 125);
                if (job != IntPtr.Zero) TerminateJobObject(job, 125);
                try { Log("MONITOR ERROR " + ex); } catch { }
                // Exiting the debugger thread also terminates its debuggees.
            } finally {
                if (pi.thread != IntPtr.Zero) CloseHandle(pi.thread);
                Marshal.FreeHGlobal(data); ready.Set();
            }
        }
        public ProcessJob(string executable, string arguments, string cwd, int seconds, string reportPath) {
            report = reportPath; timeoutMs = checked(seconds * 1000);
            try {
                job = CreateJobObject(IntPtr.Zero, null);
                if (job == IntPtr.Zero) throw new Win32Exception();
                var limits = new ExtendedLimits(); limits.basic.flags = 0x2000;
                if (!SetInformationJobObject(job, 9, ref limits, Marshal.SizeOf(limits))) throw new Win32Exception();
                monitor = new Thread(() => Run(executable, arguments, cwd)); monitor.IsBackground = true;
                monitor.Start();
                if (!ready.Wait(Math.Min(timeoutMs, 10000))) throw new TimeoutException("Process creation timed out");
                if (monitorError != null) throw new InvalidOperationException(monitorError);
            } catch { Dispose(); throw; }
        }
        public void Wait() {
            while (true) {
                lock (gate) {
                    if (timedOut) throw new TimeoutException("Process tree timed out. Diagnostics: " + report);
                    if (monitorError != null) throw new InvalidOperationException(monitorError);
                    if (finished) return;
                    if (disposed) throw new ObjectDisposedException("ProcessJob");
                }
                Thread.Sleep(25);
            }
        }
        public void Dispose() {
            lock (gate) { if (disposed) return; disposed = true; stopping = true; }
            // Do not hold gate while joining: EXIT events must keep draining.
            bool joined = monitor == null;
            try {
                if (monitor != null) joined = monitor.Join(6000);
                if (!joined) {
                    if (job != IntPtr.Zero) TerminateJobObject(job, 124);
                    throw new TimeoutException("Monitor cleanup exceeded bound; owned job terminated");
                }
                if (job != IntPtr.Zero && Members().Length != 0) {
                    TerminateJobObject(job, 124);
                    var deadline = Stopwatch.StartNew();
                    while (Members().Length != 0 && deadline.ElapsedMilliseconds < 1000) Thread.Sleep(25);
                    if (Members().Length != 0) throw new TimeoutException("Owned processes remain after cleanup");
                }
            } finally {
                // A stuck native creator must not use closed/recycled handles
                // or a disposed ready event. Fail the suite; process teardown
                // owns this exceptional case's remaining handle lifetime.
                if (joined) {
                    if (job != IntPtr.Zero) CloseHandle(job);
                    if (processHandle != IntPtr.Zero) CloseHandle(processHandle);
                    foreach (var p in observed.Values) p.Dispose();
                    if (Root != null) Root.Dispose();
                    ready.Dispose();
                }
            }
        }
    }
}
