# Set up Twitch in StreamBridge

A stream key is enough to send video. Connect a Twitch account for combined chat, viewer counts, title and category editing, and moderation.

Register your own Twitch application once to obtain its **Client ID**. This identifier is neither a password nor a stream key.

## 1. Prepare your account

1. Sign in to the Twitch account you intend to stream from.
2. Check that its email address is verified.
3. Enable two-factor authentication in Twitch security settings.
4. Open the [Twitch application console](https://dev.twitch.tv/console/apps).

A verified email and two-factor authentication are required to register an application. [Twitch requirements](https://dev.twitch.tv/docs/authentication/register-app/).

## 2. Register the application

Click **Register Your Application** and complete the form:

| Field | What to enter |
| --- | --- |
| Name | A unique name, such as **StreamBridge_YourNickname**. Add digits if it is taken. |
| OAuth Redirect URLs | Enter **https://localhost** and click **Add**. Check that the address appears in the list. |
| Category | Choose **Application Integration**, or **Other** if unavailable. |
| Client Type | Choose **Public**, which is required for StreamBridge sign-in. |

Complete the CAPTCHA if shown and click **Create**.

The **https://localhost** address fills a registration field. StreamBridge uses device authorization, which does not redirect your browser to this address. You do not need to open localhost, run a web server or install a certificate. [Twitch device authorization](https://dev.twitch.tv/docs/authentication/getting-tokens-oauth/#device-code-grant-flow).

## 3. Enter the Client ID in OBS

1. Return to the Twitch application list.
2. Click **Manage** beside your application.
3. Copy its **Client ID**.
4. In OBS, open **StreamBridge Settings → Twitch**.
5. Expand **Account setup · Client ID** and paste the value into **Client ID**.

Do not paste a stream key into this field. You do not need to generate or copy a **Client Secret** for this connection method.

## 4. Authorize in your browser

1. Click **Connect Twitch account**.
2. A short code appears in OBS settings and the Twitch activation page opens in your browser.
3. If the website requests a code, click **Copy Twitch authorization code** in the plugin and paste it into the website.
4. Check that the browser is signed in to the intended channel's account.
5. Review the application name and grant the requested permissions.
6. Return to OBS and wait for **Account connected**.

Keep OBS settings open until the connection finishes. The plugin detects completion automatically; a redirect to localhost is unnecessary. If the browser did not open, click **Open authorization website again**.

## 5. Retrieve the key and start streaming

1. On the **Twitch** tab, click **Use this account's stream key**.
2. Wait for confirmation that the server and key were saved. You do not need to copy the key manually afterward.
3. Check encoding settings and click **Save** at the bottom.
4. In the main StreamBridge dock, click **Start** beside Twitch.
5. Open [Twitch Creator Dashboard](https://dashboard.twitch.tv/) and check the incoming signal.

Connecting an account does not start a broadcast. The Twitch control in StreamBridge starts its own output independently of OBS's main streaming button.

## 6. Enable chat

1. Enable **StreamBridge — Chat** in OBS's **Docks** menu.
2. Check **Connect chat** and wait for the Twitch status.
3. Twitch messages have a soft purple background. Optionally enable **Quiet sound for new messages**; it is off by default.
4. Right-click a message for moderation. Channel-owner permissions are required.

The separate information dock has been removed. Edit title, description, category and notifications in the [Twitch dashboard](https://dashboard.twitch.tv/).
## Troubleshooting

| Message or problem | What to do |
| --- | --- |
| Redirect URLs must use HTTPS | Enter **https://localhost** without spaces and click **Add**. |
| The application creation button is disabled | Check the name, category, added address, CAPTCHA, verified email and two-factor authentication. |
| Authorization fails before a code appears | Check that the application is **Public** and its **Client ID** is entered in OBS. |
| The browser did not open | Click **Open authorization website again** in the plugin. |
| The code expired | Cancel the attempt and connect again. Use the new code. |
| OBS settings closed during sign-in | Start the connection again and keep settings open until it finishes. |
| The wrong channel connected | Disconnect in StreamBridge, switch accounts on Twitch and connect again. |
| Chat does not load | Check the connected channel and **Connect chat**. Read the Twitch status above the messages. |
| Moderation is unavailable after an update | Reconnect to grant current permissions. The account must also have rights to perform the action. |
| Video does not start | Retrieve the key with **Use this account's stream key**, save settings and check the output status in the main dock. |

## Video only, without application registration

1. Open [Creator Dashboard](https://dashboard.twitch.tv/) → **Settings → Stream**.
2. Copy the primary stream key.
3. Open **StreamBridge Settings → Twitch** and select the automatic server.
4. Paste the key into **Stream key** and click **Save server and key (encrypted)**. The field clears after saving.
5. Click **Save** at the bottom.
6. Click **Start** beside Twitch and check the signal in Creator Dashboard.

This sends video but does not connect chat, viewer counts through the API. Do not publish stream keys or account tokens.
