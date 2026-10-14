# Set up YouTube in StreamBridge

These instructions require **StreamBridge 0.2.4 or newer**. A stream key is enough to send video. Connect an account through your own Google Cloud project for combined chat, viewer counts and YouTube broadcast controls.

Each user creates their own project. Personal setup does not require publishing the application to everyone or sending account credentials to the plugin author.

## 1. Prepare your Google Cloud project

1. Open [Google Cloud Console](https://console.cloud.google.com/) with your Google account.
2. Click the current project name at the top. Select an existing StreamBridge project or create one using **New project**.
3. Check that the correct project is selected in the top bar. Use this same project for all remaining steps.
4. Open **APIs & Services → Library**.
5. Find **YouTube Data API v3**, open it and click **Enable**. If the button says **Manage**, the API is already enabled.

## 2. Allow your account to sign in

1. Open **Google Auth Platform**. If it is not configured yet, click **Get started**.
2. Enter an application name, such as **StreamBridge**, a support email and a contact email.
3. Choose **External** for a personal Google account and finish the initial setup.
4. Open **Audience**. Leave the application in **Testing** during personal setup.
5. Under **Test users**, click **Add users**, add the email address you will use for YouTube sign-in and save.
6. Open **Data Access → Add or remove scopes**, add **https://www.googleapis.com/auth/youtube.force-ssl** and save.

This permission supports the plugin's YouTube features. Gmail and Google Drive permissions are unnecessary. [Google's consent screen instructions](https://developers.google.com/workspace/guides/configure-oauth-consent).

**Important:** in Testing mode, YouTube authorization and refresh tokens expire after **seven days**. Connect the account in StreamBridge again when this happens. Before changing the publication status later, read [Google's application audience rules](https://support.google.com/cloud/answer/15549945?hl=en).

## 3. Create the client and download JSON immediately

1. Open **Google Auth Platform → Clients**.
2. Click **Create client**.
3. Set **Application type** to **Desktop app**.
4. Enter a name, such as **StreamBridge Desktop**, and click **Create**.
5. **Before closing the client creation dialog, click Download JSON and save the file.**
6. Remember the download location: you will select this file in OBS.

Do not choose **Web application**. A Desktop client needs no manually entered redirect address; StreamBridge opens a temporary local callback automatically.

### If you did not save the JSON

Google hides the existing client's full **Client secret**. The notice **Viewing and downloading client secrets is no longer available** means the previous secret cannot be downloaded again. The masked value is not usable for setup. [Google's client secret rules](https://support.google.com/cloud/answer/15549257?hl=en).

To finish StreamBridge setup:

1. Stay in **the same Google Cloud project**.
2. Open **Clients → Create client** and create another **Desktop app**, for example **StreamBridge Desktop 2**.
3. Download its JSON immediately in the creation dialog, before closing it.
4. Use this new file in the next section.

You do not need a new project, to enable the API again or to re-add the test user. You can leave the old client in place until the connection works.

## 4. Import the file and connect your account

1. In OBS, open **StreamBridge Settings → YouTube**.
2. Expand **Account setup · Client ID**.
3. Click **Import JSON from Google Cloud…** and select the downloaded file.
4. Wait for **Desktop client loaded**. The Client ID fills automatically; do not edit it after importing.
5. Click **Connect YouTube account** to open your browser.
6. Choose the account listed in **Test users**, and the intended channel if Google offers a choice.
7. Check your application name and grant the requested YouTube permissions. If Google displays separate permission checkboxes, select the permissions requested by the plugin.
8. When you see **Return to OBS StreamBridge**, return to OBS and wait for **Account connected**.

Keep OBS settings open until sign-in finishes. Closing settings cancels an unfinished connection. The local **Return to OBS StreamBridge** page alone does not confirm successful sign-in; check the final status in the plugin.

Only proceed past an unverified-application warning if you recognize your project and account. A tester-related **403 access_denied** requires adding the account to **Test users**, not dismissing a warning.

The JSON is selected locally. Do not send it in messages, attach it to GitHub issues or share it with other users. A Client ID alone may not be enough to sign in; import the complete file.

## 5. Prepare a broadcast in YouTube Studio

1. Open [YouTube Studio](https://studio.youtube.com/) and check the selected channel.
2. Click **Create → Go live**. If live streaming is unavailable, follow YouTube's activation requirements.
3. Prepare a broadcast using **Streaming software**. Webcam mode is not suitable for video from OBS.
4. Set its title, visibility, audience and other broadcast options.
5. Open the broadcast's stream settings and select a stream key. For a scheduled broadcast, open its control room first.
6. If you need chat, check that it is enabled for this broadcast.

StreamBridge uses an existing broadcast. It does not create the broadcast in Studio or bind a stream to it automatically.

## 6. Select the broadcast and start video

1. On StreamBridge's **YouTube** settings tab, click **Load existing broadcasts**.
2. Select the intended broadcast and click **Use selected broadcast**.
3. Wait for confirmation that the server and key were saved. You do not need to copy the key manually in this case.
4. Check your encoding settings and click **Save**.
5. In the main StreamBridge dock, click **Start** beside YouTube.
6. In **StreamBridge → YouTube**, click **Go live on YouTube**. The plugin checks the incoming signal and waits for confirmation. If the signal is not ready, retry in a few seconds.

To change broadcasts, stop the YouTube output in StreamBridge, select the new broadcast in settings and start the output again.

## 7. Enable chat and check the selected broadcast

1. Enable **StreamBridge — Chat** in **Docks**, then check **Connect chat**.
2. Check the selected broadcast title below the YouTube status. Chat uses the connected account; a stream key alone cannot provide chat or viewers.
3. Selection survives closing Settings and restarting OBS. If missing or completed, the plugin looks for an active broadcast. Load the list and choose explicitly when several broadcasts are active.
4. You can select while streaming: this changes chat/viewer monitoring only, without replacing the running output's server or key.
5. If chat is missing, check the channel and Studio chat settings. Temporary network failures retry automatically; permission and quota errors have separate status messages.
6. Optionally enable **Quiet sound for new messages**. It is off by default and loaded history is silent.

The stream information dock has been removed. Click **Open YouTube Studio** in the main YouTube section to edit title, description, category and game in the platform editor.
## 8. End the broadcast from OBS

1. In **StreamBridge → YouTube**, click **End YouTube broadcast**.
2. Check the title in the confirmation and confirm. This broadcast cannot be resumed afterward.
3. Wait for successful completion. After YouTube confirms, the plugin stops its YouTube output; Twitch keeps running.

The main dock's **Stop** button stops sending video. Use **End YouTube broadcast** to explicitly end it on YouTube. An API error does not automatically stop the output: read the message and retry.

## Read the viewer count

- **0** — YouTube explicitly reported zero.
- **A positive number** — the reported concurrent viewer count.
- `0*` — the broadcast is live, but the count was omitted. This can mean zero viewers or hidden statistics; hover for details.
- **—** — data is unavailable, or the broadcast is upcoming or ended. Check the account connection and selected broadcast.

Counts refresh about once a minute. Chat requests no longer cause viewer refreshes to be skipped; updates are queued. [YouTube viewer-count behavior](https://developers.google.com/youtube/v3/docs/videos#liveStreamingDetails.concurrentViewers).

## Troubleshooting

| Message or problem | What to do |
| --- | --- |
| **403 access_denied**, application limited to testers | Open the project owning the imported client. Add the exact sign-in account under **Audience → Test users**. Cancel the attempt in OBS and connect again. |
| **Return to OBS StreamBridge**, followed by an error in OBS | Use StreamBridge 0.2.2 or newer and import the complete Desktop client JSON. If it was lost, create another client in the same project as described above. |
| **invalid_client** or a required **client_secret** | Check that the client still exists and import its saved JSON. If no saved file is available, create a new Desktop client and download its JSON immediately. |
| **redirect_uri_mismatch** | Use a **Desktop app** client. Create the correct type and import its file. |
| **invalid_grant** | Cancel and start sign-in again. Keep OBS settings open during authorization. |
| Sign-in is required again after about a week | This is expected for YouTube access in Google Testing mode. Connect the account again. |
| No broadcasts in the list | Check the channel and create an unfinished encoder broadcast, then load the list again. |
| The broadcast has no bound stream | Open it in Studio, configure its stream and key, then select it again in StreamBridge. |
| API or quota error | Check that **YouTube Data API v3** is enabled in the same project and that quota remains. Repeated retries do not restore quota. |
| Empty chat or a dash for viewers | Check the selected broadcast, its live status, chat availability in Studio and **Connect chat**. |

## Video only, without Google Cloud

1. Prepare an encoder broadcast in YouTube Studio and copy its stream key.
2. Open **StreamBridge Settings → YouTube** and select the primary server.
3. Paste the key into **Stream key** and click **Save server and key (encrypted)**. The field clears after saving.
4. Click **Save** at the bottom of settings.
5. Click **Start** beside YouTube in StreamBridge and check the signal in Studio.
6. If auto-start is disabled, start the broadcast in Studio manually.

A stream key sends video to the platform but does not connect chat, viewer counts through the API.
