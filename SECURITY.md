# Security policy and review

This is a development preview. Report vulnerabilities privately through [GitHub Security Advisories](https://github.com/AugustusNobilis/StreamBridge/security/advisories/new). Private vulnerability reporting is enabled for this repository. Do not post secrets or exploitable credential disclosures in a public issue.

## Boundaries

Stream keys, OAuth tokens, widget URLs and proxy credentials are in a CurrentUser Windows DPAPI vault, saved atomically. Concurrent plugin workers serialize vault access. Normal settings and OBS scene data contain no browser credentials. DPAPI does not protect against software/debuggers running as the same user, memory dumps or a compromised OBS process. It also prevents copying the vault to a different Windows account as a credential migration method.

OAuth uses fixed provider HTTPS endpoints, bounded responses, manual redirects, PKCE/state for Google and registered public Device Code Flow for Twitch. Local tokens are removed before best-effort online revocation. Stream keys previously copied from an account are deleted separately.

Each browser source has a separate CEF helper and memory-only request context. Windows Chromium sandbox support is mandatory; a Windows Job kills descendants on teardown. An ephemeral disk directory holds Chromium bootstrap data and is removed on stop. Anonymous inherited pipes carry bounded frame/audio messages and bootstrap configuration; no secret is placed in argv/environment. The loopback-only adapter requires a random per-launch capability. It is not an authentication boundary against a malicious process already running as the same Windows user.

Proxy mode only connects the adapter to the configured upstream proxy. CONNECT keeps origin TLS opaque, including HTTPS proxies. Destination hostnames use upstream resolution. Chromium destination resolver rules exclude only the local adapter; loopback bypass, QUIC and non-proxied WebRTC are disabled. User-specified proxy hostnames may require normal local DNS; choose a literal IPv4 proxy address if that is undesirable. Bare HTTP CONNECT and SOCKS5 authentication are not encrypted on the first hop; choose HTTPS CONNECT or a trusted network.

TLS verification remains enabled. There is no PAC/DIRECT fallback. Proxy-off is an explicit direct-access choice. Permissions, downloads and popups are denied. HTTPS/WSS subresources are supported; plaintext content is rejected. Page networking is not an origin allowlist: a trusted widget can request other HTTPS resources through its proxy.

## Review performed locally

The updater trusts the configured public GitHub repository over verified HTTPS. It accepts only the exact versioned Windows asset, checks GitHub's SHA-256 digest and declared size, and restricts download redirects to GitHub's asset hosts. A downloaded executable is rehashed under a Windows read lock immediately before launch. The installer checks the target OBS process path and creation time, waits for exit, serializes application per OBS root and rechecks file users/permissions. It never closes OBS itself. Same-account malware or a compromised publisher/GitHub account remains outside this integrity boundary; a digest is not Authenticode signing. Automatic network operations default off. A failed extraction is not transactionally rolled back.

- Secret storage/corruption tests, browser configuration confidentiality, safe diagnostic codes and update URL validation.
- Real local TLS acceptance/rejection, authenticated HTTP/HTTPS/SOCKS5 routing, independent source failure/recovery, WSS/audio and active shutdown.
- Found and fixed direct STUN traffic by applying Chromium's effective WebRTC policy before navigation; the regression canary observed zero packets.
- Fixed QObject child destruction invalidation and truncated TLS responses at proxy EOF.
- Installer rejects wrong architecture, locked OBS and unowned conflicts; default uninstall preserves settings.

This is not an independent penetration test or proof of complete leak freedom. A process-attributed DNS/DoH/IPv6/QUIC/UDP packet audit, malformed IPC fuzzing, long-duration media/teardown stress, normal OBS browser coexistence and real-account tests remain release gates. See [TEST-RESULTS.md](docs/TEST-RESULTS.md). No system certificate is added by installation or normal operation; temporary test certificates require explicit opt-in and cleanup.
