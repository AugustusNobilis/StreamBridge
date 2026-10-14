# Proxied Browser Source

Implemented in 0.1.0 as a development preview. Each instance renders in its own sandboxed CEF 154 helper. Tests exercised transparent BGRA, JavaScript, images, animation, workers, real HTTPS/WSS and stereo audio. A real DonationAlerts widget and comprehensive packet-level leak audit remain pending; synthetic widgets do not imply those passed.

## Configure

Add **Proxied Browser Source** from OBS Sources. Open **Configure URL and proxy securely**. Enter the HTTPS widget URL, dimensions (16–1920 by 16–1080), FPS (1–60), proxy protocol, hostname/IPv4 address, port and optional username/password. URL and password are masked. Save writes DPAPI-encrypted settings tied to the OBS source UUID and Windows user.

The DonationAlerts preset selects 800×600, 30 FPS and proxy mode. Paste the private widget URL from your dashboard; the preset does not fabricate an alert, authenticate your account or send a donation. Configure routing before using the dashboard's test-alert action. Custom HTTPS widgets are supported.

**Test Connection** loads the entered widget in a temporary browser and reports main-page load success. It may connect to the widget service and does not prove alert/audio acceptance. **Check External IP** explicitly requests JSON from `https://api.ipify.org` through the selected adapter; no widget URL is required. Proxy-off makes this check explicitly direct. Compare against a direct check to confirm expected egress. Neither check runs automatically.

Refresh restarts that source. Automatic reconnect has at most 12 attempts, exponential delay/jitter and a 60-second cap. One minute of stable operation resets the budget. Failure blanks the source; explicit refresh starts a new budget. Ordinary OBS Browser Sources retain their own settings.

## Protocols and isolation

| Mode | First hop | Origin TLS | Destination DNS |
|---|---|---|---|
| HTTP CONNECT | TCP, optional Basic auth | End-to-end, CEF verified | Proxy resolves CONNECT host |
| HTTPS CONNECT | Verified TLS, optional Basic auth | End-to-end inside tunnel | Proxy resolves CONNECT host |
| SOCKS5 | TCP, optional RFC1929 auth | End-to-end, CEF verified | RFC1928 domain request |

HTTP CONNECT and SOCKS5 do not encrypt credentials on the first hop. Use HTTPS CONNECT or a trusted network. SOCKS5 UDP ASSOCIATE is unsupported. A proxy hostname may use local DNS; use a literal IPv4 address to avoid that lookup. IPv6 proxy-host entry is not supported in this preview; destination behavior still needs IPv6 packet audit.

Proxy mode has no DIRECT/PAC fallback. Chromium uses an authenticated local adapter, removes implicit loopback bypass, blocks local destination resolution and disables QUIC/non-proxied WebRTC. The effective WebRTC context preference is required before navigation. HTTPS/WSS redirects, workers and images share the context. Plain HTTP/WS, downloads, permissions and popups are rejected. Proxy-off is deliberate direct access.

The adapter listens only on 127.0.0.1 and requires a random capability. It connects only to the selected proxy, forwards opaque TLS and never decrypts widget traffic. Each helper has an isolated temporary profile and a process tree killed on destruction. Stream outputs, accounts and updater do not use that adapter. Windows proxy/routes are unchanged.

## Persistence and performance

Scene exports contain no widget URL/password and need secure reconfiguration after transfer to another Windows account. Add Existing shares an instance; a new instance requires configuration. Independent duplication is disabled to avoid sharing encrypted source identities. Browser entries remain in the vault until plugin settings are explicitly purged.

CPU BGRA transfer/alpha conversion and separate Chromium processes consume memory/CPU. A 60 FPS setting does not guarantee that rate under load. Use modest dimensions/FPS and test the complete scene. Audio is 48 kHz stereo with normal OBS mixer controls. Long-duration sync and load remain release gates.

## Acceptance

See [TEST-RESULTS.md](TEST-RESULTS.md). Real local TLS/WSS passed for three simultaneous authenticated protocols, independent failure/recovery and shutdown. A direct helper control stayed separate from two proxies. A STUN canary received zero packets after correcting Chromium policy. These observations do not constitute an exhaustive DNS/DoH/IPv6/UDP capture.

Before stable release: normal OBS Browser Source alongside two distinct public proxy egress IPs, real DonationAlerts animation/audio, process-attributed network traces, startup proxy refusal, malformed IPC stress, prolonged reload/destruction and OBS exit with active media. Do not use public free proxies or disable TLS to pass a test.
