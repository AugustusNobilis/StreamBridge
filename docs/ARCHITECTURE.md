# Architecture

StreamBridge is a Windows C++20 plugin, a separate CEF helper and an authenticated per-source proxy adapter. The implementation is a development preview. TEST-RESULTS.md separates executed evidence from pending acceptance.

## Modules

- `plugin.cpp`: module exports, OBS frontend dock/menu and shutdown.
- `ui/`: compact native Qt dock, settings, provider account panels and OBS locale lookup.
- `config/`: bounded non-secret JSON parsing, validation and atomic replacement.
- `stream/encoder.*`: owned libobs H.264/AAC references and supported typed settings.
- `stream/output-manager.*`: two independent services/outputs, common or advanced encoder ownership, metrics and bounded retries.
- `security/`: CurrentUser DPAPI vault with serialized read/modify/write and atomic persistence.
- `oauth/`: asynchronous fixed-endpoint clients, Google PKCE loopback and Twitch Device Code state machines.
- `proxy/`: authenticated loopback CONNECT listener and HTTP/HTTPS/SOCKS5 upstream negotiation.
- `browser/`: encrypted settings, OBS source, worker thread, media IPC and temporary connection/IP probes.
- `helper/`: sandboxed CEF process, isolated context, off-screen video/audio and bounded writer queues.
- `updates/`: opt-in GitHub metadata checks and verified release URLs.
- `tests/`, `tools/`, `installer/`, `.github/workflows/`: policy and runtime evidence, build, packaging and draft-release workflow.

## Ownership and concurrency

UI configuration, stream transitions and QTimer sampling live on the Qt thread. OBS output callbacks append small typed events under a mutex; they do not touch widgets or format upstream text. A session owns its output/service and shared ownership of an encoder pair. Common mode shares one video/AAC pair; Advanced owns two. Encoders are not mutated while active.

Stop cancels desired-running and backoff before stopping the output. Mode changes require no active, desired or pending sessions. A later-started session joins the common encoder. Once both stop, the next start recreates the pair using the OBS video pipeline. Retries use equal jitter, a 1-second floor, a 60-second cap and 12 retries. A minute of stable operation resets the budget; an explicit Start begins a fresh budget.

Stream shutdown stops timers, disconnects callbacks, stops/releases outputs, then releases services and encoders. Active RTMPS teardown has been exercised; extended unresponsive-peer stress remains necessary. The DLL shares OBS's address space: native memory corruption cannot be guaranteed to stay within a module.

Each browser source owns a QThread, BrowserWorker and BrowserHost. The helper receives bootstrap data through an anonymous pipe only after assignment to a kill-on-close Windows Job. Its descendants inherit that job. CEF 154's official sandbox bootstrap executable loads the same-name helper DLL; no second CEF runtime is loaded into OBS. Each source owns its request context and temporary bootstrap directory.

The helper sends 32-byte typed headers plus bounded BGRA or planar stereo float payloads. Host validates size/type/dimensions/audio layout; the worker rejects non-finite audio and converts premultiplied BGRA to straight alpha. QPC timestamps share OBS's Windows clock domain. The writer retains at most one waiting frame and 64 audio packets. Overflow drops old media. The inherited command pipe accepts refresh/quit; no remote control endpoint is implemented.

ProxyAdapter runs on the source worker thread. Its loopback listener requires a random per-launch capability; an unauthorized request gets 407 before upstream access. Each tunnel connects only to the configured proxy. HTTPS proxy TLS is verified before negotiation; origin TLS remains opaque. SOCKS5 passes destination hostnames in domain form. Normal EOF drains queued encrypted bytes before closing. Shutdown disconnects socket callbacks before object destruction.

Main navigation/process failure blanks the source and uses bounded retries. Subresource or preconnect errors do not restart an otherwise healthy page. Source teardown requests graceful quit and then kills the job if needed. Temporary UI probes are deleted on completion, dialog destruction or thread shutdown.

## Data and privacy

`obs_module_config_path` selects the configuration root. `settings.json` stores encoder options and server addresses. `vault.dpapi` stores stream keys, account tokens and complete browser settings. Source UUIDs identify encrypted browser entries; widget URLs and proxy credentials never enter scene data. A mutex serializes vault updates across account and browser threads. Secret keys enter live libobs service settings only at start; StreamBridge never exports those services.

DPAPI protects at rest against other Windows users, not software/debuggers running as the same user or OBS crash dumps. Qt/libobs can retain temporary string copies in memory. Diagnostics contain typed states and safe error codes, not raw provider replies or credentials.

Network activity includes user-started streams/widgets, connected-account token maintenance, explicit IP probes and optional GitHub metadata checks. Twitch validates on resume and at least hourly. Updates default off and never download/install binaries. No global proxy, log-handler override, hidden telemetry or remote OBS commands are implemented.
