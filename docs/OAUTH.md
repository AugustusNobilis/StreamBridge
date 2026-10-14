# OAuth account integration

Step-by-step user guides: **[YouTube — Русский](YOUTUBE-SETUP.ru.md)** · [YouTube — English](YOUTUBE-SETUP.en.md) · **[Twitch — Русский](TWITCH-SETUP.ru.md)** · [Twitch — English](TWITCH-SETUP.en.md). The same guides are available offline from each platform's settings tab.

Manual RTMPS requires no developer application, API key, relay, or OAuth server. Account buttons execute asynchronous provider flows. No distributor client IDs are bundled: import your Google Desktop JSON or enter your Twitch Public Client ID, then Connect. Confidential Web application credentials are rejected. End-to-end provider consent has not been tested with registered clients in this environment.

Protocol tests use mock provider responses and a real TCP loopback callback. Executed tests cover PKCE's RFC vector, rejected state injection, cancellation, Twitch polling/slow-down, authorized broadcaster selection, DPAPI persistence, refresh rotation, revoked tokens, YouTube selection and unbound streams, and revocation failure reporting. They do not replace real-account acceptance.

## Google / YouTube

Register a Desktop OAuth client in Google Cloud, enable YouTube Data API v3, configure the consent screen and permitted test users. Public distribution may require sensitive-scope verification and quota approval. Native apps use the external system browser, PKCE S256 with high-entropy verifier/state, and a short-lived loopback listener bound to 127.0.0.1 at a random port. Validate state, path, one-shot code, expiry and request size. No embedded browser login or OOB flow.

The implementation requests `youtube.force-ssl`, lists existing unfinished broadcasts, obtains `contentDetails.boundStreamId`, then retrieves the associated live stream's `cdn.ingestionInfo.rtmpsIngestionAddress` and `streamName`. Choose a broadcast and press Use selected broadcast to save these settings. An unbound broadcast must first be configured in YouTube Studio. This integration does not create, bind, transition or publish broadcasts. Lists are capped at 20 pages (1,000 items).

The official native-app token documentation lists client_secret as optional. For Desktop clients requiring it, the user imports their own Google credential JSON locally (maximum 64 KiB, installed-client schema only). Only ID and secret are read; endpoint URLs remain fixed. The secret is sent to Google's token endpoint for code exchange and refresh, never in the browser URL. It persists alongside access/refresh tokens in DPAPI, is retained during rotation, and is removed on disconnect. Nothing is bundled into a public DLL or diagnostic export. Errors are mapped to safe instructions for missing secret, invalid client and expired grant; raw responses are not displayed.

## Twitch

Register an application as a **public** client. Twitch documents Device Code Flow for public clients, including secretless refresh. Authorization Code without the required secret is not substituted with an undocumented PKCE implementation. Request `channel:read:stream_key channel:manage:broadcast chat:read moderator:manage:chat_messages moderator:manage:banned_users`, open the official verification URI in the system browser, show the user code, respect interval/expiry and `authorization_pending`/`slow_down` responses. Poll asynchronously; Cancel stops timers and pending replies.

Validate the access token with Twitch's validation endpoint and follow its periodic validation requirements. Determine the authorized broadcaster, then call Helix `GET /streams/key?broadcaster_id=...` with `Authorization` and `Client-Id` headers. Stream key must go directly into encrypted storage. Device-flow refresh tokens rotate; persist the replacement atomically and stop using the old one. Handle authorization denial, lost scope, revoked token and expiration without a fast retry loop.

Disconnect cancels work, erases local tokens, requests revocation through the official endpoint, and reports any revocation failure. Key deletion is a separate explicit choice. Account credential changes are forbidden while a StreamBridge output is active. A previously linked account resumes on OBS startup; Twitch validates immediately and at least hourly, and tokens refresh before expiry. Transient account-network failures retry after 60 seconds. No RTMPS stream is started by an OAuth operation.

The UI discloses Google OAuth/YouTube and Twitch OAuth/Helix requests before Connect. Token requests use a fixed HTTPS host allowlist, normal TLS verification, 30-second deadlines, a 1 MiB response cap and no redirects. No application registration or platform authorization was performed during this development milestone. Public releases remain gated on testing exact registered clients and satisfying provider policies.

References: [Google native apps](https://developers.google.com/identity/protocols/oauth2/native-app), [YouTube live API](https://developers.google.com/youtube/v3/live/), [Twitch token flows](https://dev.twitch.tv/docs/authentication/getting-tokens-oauth/), [Twitch validation](https://dev.twitch.tv/docs/authentication/validate-tokens/).

See [Platform docks](PLATFORMS.md) for chat, viewer counts, metadata changes, expanded scopes and acceptance limits.
