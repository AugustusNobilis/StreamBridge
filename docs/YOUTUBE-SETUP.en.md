# YouTube: StreamBridge setup

## Fix the pictured 403 access_denied

An **Access blocked** page saying the app is being tested and allows only **developer-approved testers** means the selected Google account is not permitted in Testing mode.

Open [Google Cloud](https://console.cloud.google.com/), select the project owning the Client ID entered in StreamBridge, then **Google Auth Platform → Audience → Test users → Add users**. Add the Google account used for sign-in and save. Cancel the pending login in OBS and connect again with that account.

If someone else owns the Client ID, that project's owner must add you. Adding a user in a different project will not help. The 403 page cannot simply be dismissed. [Google's Testing rules](https://support.google.com/cloud/answer/15549945).

## 1. Prepare Google Cloud

Create or select a project. In **APIs & Services → Library**, enable **YouTube Data API v3**. Keep using this project for every following step.

Open **Google Auth Platform → Get started** if needed. Enter your application name, support email and contact email. Choose **External** for a personal Google account. Under **Audience**, use **Testing** for personal evaluation and add your sign-in account to **Test users**.

Under **Data Access → Add or remove scopes**, add **https://www.googleapis.com/auth/youtube.force-ssl** and save. StreamBridge uses this permission for YouTube data, chat and broadcast management; it does not need Gmail or Drive permissions. [Consent screen setup](https://developers.google.com/workspace/guides/configure-oauth-consent).

## 2. Create the desktop client

1. Open **Google Auth Platform → Clients → Create client**.
2. Select **Desktop app**, name it and create it.
3. Copy the **Client ID** ending in **.apps.googleusercontent.com**.
4. Paste it into **StreamBridge settings → YouTube → Account setup · Client ID**.

Do not choose Web application. Desktop clients use an automatically opened local callback; no manually entered Redirect URI is needed. An API key, stream key or Client Secret is not a Client ID. This version has no Client Secret field and does not send one. If Google rejects token exchange after consent, report the error text without credentials; that may require an integration fix. [Google desktop authorization](https://developers.google.com/youtube/v3/live/guides/auth/installed-apps).

## 3. Connect

Click **Connect YouTube account**, select the permitted Google account and intended channel, review the application and permissions and authorize. Only proceed past an unverified-app warning if you recognize your project; a tester-related 403 requires the fix above.

Keep OBS settings open until **Account connected** appears. Closing settings cancels an unfinished login. In Testing mode, authorization and refresh tokens for these scopes expire after **7 days**, requiring reconnection. Distributing one client to the public needs separate preparation and compliance with Google's requirements.

## 4. Prepare and select the broadcast

1. Open [YouTube Studio](https://studio.youtube.com/), choose your channel, then **Create → Go live**.
2. Prepare a **Streaming software / encoder** broadcast, not a webcam broadcast. Set title, visibility and stream settings. Follow YouTube's activation requirements if live streaming is unavailable.
3. For a scheduled broadcast, open its control room and configure its stream key; the broadcast must have a bound stream.
4. In StreamBridge, click **Load broadcasts**, choose the broadcast and **Use selected broadcast**.
5. Wait for the saved-ingest confirmation and save settings.
6. Click YouTube **Start** in StreamBridge. Check the incoming signal in Studio and start the broadcast there if auto-start is disabled.

StreamBridge uses an existing broadcast; it does not create, bind or publish one through the API. Chat, viewer counts and metadata refer to the selected broadcast. Enable **StreamBridge — Chat** from OBS's **Docks** menu and click **Connect chat**.

## Troubleshooting

- Tester-related 403: verify the project and exact account in **Audience → Test users**.
- redirect_uri_mismatch: use a **Desktop app** client instead of Web.
- invalid_client or token exchange error: verify the desktop Client ID; an error after consent may need an integration fix.
- Empty broadcast list: check the selected channel and an unfinished encoder broadcast, then load again.
- Unbound broadcast: configure its stream/key in Studio and select it again.
- API/quota error: check API enablement, channel permissions and project quota; retry after resolving the cause.
- Empty chat: select the intended broadcast, enable chat in Studio and click **Connect chat**.

## Stream-key-only mode

Copy your stream key from Studio into StreamBridge's **Stream key** field, select the primary YouTube server, save the key and settings, start output and check Studio. Google Cloud setup is unnecessary for video transport alone; account API features still require OAuth.

Never publish stream keys, tokens or downloaded credential JSON. Automated tests do not replace acceptance with your exact registered Client ID and real account.
