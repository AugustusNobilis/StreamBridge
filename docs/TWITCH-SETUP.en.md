# Twitch: StreamBridge setup

## Choose a connection

A stream key is enough to send video. Connect an account for chat, viewers, title/category editing and moderation. This release requires you to register your own Client ID once. Enter your Twitch password only on Twitch.

## 1. Register the application

Open the [Twitch application console](https://dev.twitch.tv/console/apps) → **Register Your Application**. Verify your email and enable Twitch two-factor authentication first.

| Field | Value |
| --- | --- |
| Name | A unique name, for example **StreamBridge_YourNickname**. |
| OAuth Redirect URLs | **https://localhost**, then **Add**. Make sure it appears in the list. |
| Category | **Application Integration**, or **Other** if unavailable. |
| Client Type | **Public**. |

Complete the CAPTCHA if shown and choose **Create**. StreamBridge uses Device Code Flow: the redirect address is a registration placeholder and is not visited during sign-in. No local server or certificate installation is needed.

## 2. Enter the Client ID in OBS

1. Find the application → **Manage** → copy **Client ID**.
2. Open **StreamBridge settings → Twitch → Account setup · Client ID**.
3. Paste the ID. Do not paste a stream key or Client Secret here; this flow needs no secret.

## 3. Connect and use the account

1. Click **Connect Twitch account**. The plugin opens Twitch activation and displays a short code.
2. Use **Copy Twitch authorization code** if Twitch asks you to enter it.
3. Sign in as the broadcaster, check the application and permissions, then authorize.
4. Keep OBS settings open until **Account connected** appears. The plugin detects completion without a redirect back to localhost.
5. Click **Use account stream key** if necessary, wait for the saved-ingest confirmation and save settings.
6. Enable **StreamBridge — Chat** in OBS's **Docks** menu and click **Connect chat**. Use **Stream information** for title/category edits.

Connecting does not start a broadcast. **Start** in the StreamBridge dock starts video output.

## Troubleshooting

- HTTPS validation: enter the complete **https://localhost** URL and click **Add**.
- Create disabled: check the category, added URL, unique name, CAPTCHA, verified email and 2FA.
- Authorization fails before a code appears: check **Public** and the correct **Client ID**.
- Browser did not open: use **Reopen authorization website**.
- Code expired or settings were closed: cancel and connect again with the new code.
- Wrong channel: disconnect, switch accounts on Twitch and reconnect.
- Older connection cannot moderate: reconnect to grant the current permissions. Moderation requires rights in the connected channel.

## Stream-key-only mode

Open [Creator Dashboard](https://dashboard.twitch.tv/) → **Settings → Stream**, copy the primary stream key and paste it into StreamBridge's **Stream key** field. Select the automatic Twitch server, save the key and settings. This does not connect chat or account API features.

References: [Twitch registration](https://dev.twitch.tv/docs/authentication/register-app/), [Public device authorization](https://dev.twitch.tv/docs/authentication/getting-tokens-oauth/#device-code-grant-flow). StreamBridge steps follow the implementation; acceptance with your exact registered client still needs a real-account check.
