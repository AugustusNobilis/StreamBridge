# Platform docks (0.2.0)

The three docks appear in OBS's Docks menu. The stream dock keeps existing output controls. Chat and stream information use the connected account, independent of the encoder state.

## Viewers

YouTube counts refer to the broadcast explicitly selected under Settings → YouTube → Use selected broadcast. Twitch counts refer to the validated account's own channel. API polling runs about once a minute. Zero is grey, positive counts are muted red; disconnected, failed, missing or hidden counts display a grey dash. A manual stream key does not identify an authorized API account.

## Chat and moderation

Click Connect chat to enable reception. Disabling it disconnects Twitch and stops scheduling YouTube polling. Chat is off after OBS restarts. At most 500 messages stay in memory; chat is not written to disk. Messages are rendered as plain text with native platform icons. Twitch IRC tags are parsed with bounded buffers; duplicate messages are suppressed.

Twitch connects with TLS to irc.chat.twitch.tv:6697. YouTube uses liveChatMessages.list with nextPageToken and a polling interval no shorter than the platform requests (minimum 5 seconds). This can consume substantial YouTube API quota during long streams. A polling failure pauses YouTube chat until Refresh is pressed. Account changes discard pending results and old messages.

Right-click a message to copy, delete or permanently ban its author. Delete/ban requires confirmation and is sent only for that selected platform/message/user. The local message is removed only after a successful API response. YouTube owner/moderator messages and Twitch owner/moderator or foreign shared-chat messages do not offer moderation actions. The server remains authoritative about permissions; failed requests remain visible as errors. There is no bulk clear-chat action and no outgoing chat composer.

## Stream information

Read current title/category, search Twitch categories or load assignable YouTube categories for a two-letter country code, then explicitly Publish. YouTube additionally supports description editing. A fresh video snippet is fetched before videos.update so existing tags and language fields are preserved. Metadata updates do not start, stop, create or transition a broadcast. Editable content is not replaced by periodic polling while the user has unsaved edits.

The separate go-live notification field is a **local draft** stored in ui.ini. The public platform APIs used here expose no method for that notification text. Copy it and use Open platform dashboard; it is not submitted by the Publish button. YouTube video description is a different field and is submitted.

## Permissions and network

YouTube: youtube.force-ssl. Twitch: channel:read:stream_key, channel:manage:broadcast, chat:read, moderator:manage:chat_messages, moderator:manage:banned_users. Reconnect existing read-only sessions before using write features. Tokens are stored with Windows DPAPI, refreshed by the account service and never included in diagnostics.

API calls use the existing fixed HTTPS host allowlist, standard TLS verification, no redirects, a 1 MiB response bound and request timeouts. Requests are serialized per platform; queued requests and responses are invalidated on account or selected-broadcast changes. Browser-source proxies apply only to their source, not to OAuth, chat, metadata or RTMPS outputs.

Registered application IDs are not bundled. Exact Google Desktop and Twitch Public registrations, real consent, live chat reception and real moderation still require acceptance on the maintainer's accounts. Automated tests are not a substitute for these checks.

## References

- [Twitch IRC](https://dev.twitch.tv/docs/chat/irc)
- [Twitch API: channel information, viewers, categories and moderation](https://dev.twitch.tv/docs/api/reference/)
- [YouTube live chat messages](https://developers.google.com/youtube/v3/live/docs/liveChatMessages/list)
- [YouTube chat bans](https://developers.google.com/youtube/v3/live/docs/liveChatBans/insert)
- [YouTube video updates](https://developers.google.com/youtube/v3/docs/videos/update)
