// SPDX-License-Identifier: GPL-2.0-or-later
// Test-only process containment. Assign while suspended, before any NSIS child
// can start. Job membership survives parent exit and covers the temporary copy.
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

        readonly object gate = new object();
        readonly Stopwatch clock = Stopwatch.StartNew();
        readonly Dictionary<int, Process> observed = new Dictionary<int, Process>();
        readonly string report;
        readonly int timeoutMs;
        IntPtr job, processHandle;
        Timer timer;
        bool disposed, finished, timedOut;
        string monitorError;
        public Process Root { get; private set; }
        public double Seconds { get { return clock.Elapsed.TotalSeconds; } }
        public bool TimedOut { get { lock (gate) return timedOut; } }
        public int ExitCode {
            get { uint value; if (!GetExitCodeProcess(processHandle, out value)) throw new Win32Exception(); return unchecked((int)value); }
        }
        public int ObservedExitCode(int id) {
            lock (gate) {
                Process p;
                if (!observed.TryGetValue(id, out p) || !p.HasExited)
                    throw new InvalidOperationException("No completed process handle for terminal PID " + id + "; refusing to infer success from a log alone");
                return p.ExitCode;
            }
        }
        void Log(string message) {
            File.AppendAllText(report, DateTime.UtcNow.ToString("O") + " " + message + Environment.NewLine, new UTF8Encoding(false));
        }
        int[] Members() {
            // The test trees are small. Fail closed if the job ever exceeds this buffer.
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
        void Observe(int[] ids) {
            foreach (int id in ids) {
                if (observed.ContainsKey(id)) continue;
                try {
                    var p = Process.GetProcessById(id);
                    var handle = p.Handle; // Keep identity valid after exit; never terminate by PID/name.
                    observed.Add(id, p);
                    var module = p.MainModule;
                    Log("PROCESS pid=" + id + " started=" + p.StartTime.ToUniversalTime().ToString("O") + " path=" + (module == null ? "<exited>" : module.FileName));
                } catch (ArgumentException) { } catch (InvalidOperationException) { } catch (Win32Exception) { }
            }
        }
        void Snapshot(string reason, int[] ids) {
            Log(reason + " elapsed=" + Seconds.ToString("F3", System.Globalization.CultureInfo.InvariantCulture) + " active=" + string.Join(",", ids));
            foreach (var pair in observed) {
                try { Log("STATE pid=" + pair.Key + " exited=" + pair.Value.HasExited + (pair.Value.HasExited ? " code=" + pair.Value.ExitCode : " title=" + pair.Value.MainWindowTitle)); }
                catch (Exception ex) { Log("STATE pid=" + pair.Key + " unavailable=" + ex.GetType().Name); }
            }
        }
        void Tick(object unused) {
            lock (gate) {
                if (disposed || finished || timedOut || monitorError != null) return;
                try {
                    var ids = Members(); Observe(ids);
                    if (ids.Length == 0) { finished = true; Snapshot("END tree", ids); return; }
                    if (clock.ElapsedMilliseconds >= timeoutMs) {
                        timedOut = true;
                        Snapshot("TIMEOUT", ids);
                        if (!TerminateJobObject(job, 124)) throw new Win32Exception();
                        Log("TERMINATE owned job only code=124");
                    }
                } catch (Exception ex) {
                    monitorError = ex.ToString();
                    TerminateJobObject(job, 125);
                    try { Log("MONITOR ERROR " + monitorError); } catch { }
                }
            }
        }
        public ProcessJob(string executable, string arguments, string cwd, int seconds, string reportPath) {
            report = reportPath; timeoutMs = checked(seconds * 1000);
            ProcessInfo pi = new ProcessInfo();
            try {
                job = CreateJobObject(IntPtr.Zero, null);
                if (job == IntPtr.Zero) throw new Win32Exception();
                var limits = new ExtendedLimits();
                limits.basic.flags = 0x2000; // KILL_ON_JOB_CLOSE; deliberately no BREAKAWAY_OK.
                if (!SetInformationJobObject(job, 9, ref limits, Marshal.SizeOf(limits))) throw new Win32Exception();
                var startup = new StartupInfo(); startup.cb = Marshal.SizeOf(startup);
                startup.flags = 1; startup.show = 0; // STARTF_USESHOWWINDOW / SW_HIDE.
                if (!CreateProcess(executable, new StringBuilder("\"" + executable + "\" " + arguments),
                    IntPtr.Zero, IntPtr.Zero, false, 0x4 | 0x08000000, IntPtr.Zero, cwd, ref startup, out pi)) throw new Win32Exception();
                processHandle = pi.process;
                if (!AssignProcessToJobObject(job, pi.process)) throw new Win32Exception();
                Root = Process.GetProcessById((int)pi.pid); var rootHandle = Root.Handle;
                Log("START pid=" + pi.pid + " executable=" + executable + " timeoutSeconds=" + seconds);
                Observe(Members());
                if (ResumeThread(pi.thread) == uint.MaxValue) throw new Win32Exception();
                timer = new Timer(Tick, null, 0, 10);
            } catch {
                // The suspended process cannot have created children if assignment failed.
                if (pi.process != IntPtr.Zero) TerminateProcess(pi.process, 125);
                Dispose(); throw;
            } finally { if (pi.thread != IntPtr.Zero) CloseHandle(pi.thread); }
        }
        public void Wait() {
            while (true) {
                lock (gate) {
                    if (timedOut) throw new TimeoutException("Process tree timed out. Diagnostics: " + report);
                    if (monitorError != null) throw new InvalidOperationException(monitorError);
                    if (finished) return;
                    if (disposed) throw new ObjectDisposedException("ProcessJob");
                }
                Thread.Sleep(50);
            }
        }
        public void Dispose() {
            lock (gate) {
                if (disposed) return;
                disposed = true;
                if (timer != null) timer.Dispose();
                try {
                    if (job != IntPtr.Zero) {
                        var ids = Members();
                        if (ids.Length != 0) {
                            Snapshot("CLEANUP", ids);
                            if (!TerminateJobObject(job, 124)) throw new Win32Exception();
                            var deadline = Stopwatch.StartNew();
                            while (Members().Length != 0 && deadline.ElapsedMilliseconds < 5000) Thread.Sleep(25);
                            var remaining = Members();
                            Log("CLEANUP remaining=" + string.Join(",", remaining));
                            if (remaining.Length != 0) throw new TimeoutException("Owned job cleanup did not finish");
                        }
                    }
                } finally {
                    if (job != IntPtr.Zero) CloseHandle(job);
                    if (processHandle != IntPtr.Zero) CloseHandle(processHandle);
                    foreach (var p in observed.Values) p.Dispose();
                    if (Root != null) Root.Dispose();
                }
            }
        }
    }
}
