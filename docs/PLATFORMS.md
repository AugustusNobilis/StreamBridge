# Platform docks (0.2.3)

The three docks appear in OBS's Docks menu. The stream dock keeps existing output controls. Chat and stream information use the connected account, independent of the encoder state.

## Viewers

YouTube counts refer to the broadcast explicitly selected under Settings → YouTube → Use selected broadcast. Twitch counts refer to the validated account's own channel. API polling runs about once a minute. Zero is grey, positive counts are muted red; disconnected, failed, upcoming or ended broadcasts display a grey dash. For a live YouTube broadcast with an omitted count, the badge displays 0* with an explicit zero-or-hidden explanation; it is not a measured zero. String and numeric counts are supported. Info refreshes queue behind chat traffic instead of being skipped. A manual stream key does not identify an authorized API account.

## Chat and moderation

Click Connect chat to enable reception. Disabling it disconnects Twitch and stops scheduling YouTube polling. Chat is off after OBS restarts. At most 500 messages stay in memory; chat is not written to disk. Messages are rendered as plain text with native platform icons. Twitch IRC tags are parsed with bounded buffers; duplicate messages are suppressed.

Twitch connects with TLS to irc.chat.twitch.tv:6697. YouTube uses liveChatMessages.list with nextPageToken and a polling interval no shorter than the platform requests (minimum 5 seconds). This can consume substantial YouTube API quota during long streams. A polling failure pauses YouTube chat until Refresh is pressed. Account changes discard pending results and old messages.

Right-click a message to copy, delete or permanently ban its author. Delete/ban requires confirmation and is sent only for that selected platform/message/user. The local message is removed only after a successful API response. YouTube owner/moderator messages and Twitch owner/moderator or foreign shared-chat messages do not offer moderation actions. The server remains authoritative about permissions; failed requests remain visible as errors. There is no bulk clear-chat action and no outgoing chat composer.

## Stream information

Read current title/category, search Twitch categories or load assignable YouTube categories for a two-letter country code, then explicitly Publish. YouTube description editing and notification drafts have been removed. A fresh video snippet is fetched before videos.update so existing tags and language fields are preserved. Title/category updates preserve the existing description, tags and language and do not transition a broadcast. Editable content is not replaced by periodic polling while the user has unsaved edits.

For YouTube Gaming, the public API has no writable game-title field. A Studio shortcut opens the selected broadcast for that choice. Category names follow the plugin language.

## YouTube broadcast controls

The information dock offers explicit Go live and End broadcast actions. Start the encoder output first. Go live fetches fresh broadcast state and verifies that the bound stream is active, performs the monitor/testing transition if required, and polls intermediate states with a bounded retry count. End requires UI confirmation and stops only the plugin's YouTube output after the API confirms completion. A failed or mismatched response does not stop output. Account changes invalidate pending operations. Lifecycle requests use the existing youtube.force-ssl scope.

## Permissions and network

YouTube: youtube.force-ssl. Twitch: channel:read:stream_key, channel:manage:broadcast, chat:read, moderator:manage:chat_messages, moderator:manage:banned_users. Reconnect existing read-only sessions before using write features. Tokens are stored with Windows DPAPI, refreshed by the account service and never included in diagnostics.

API calls use the existing fixed HTTPS host allowlist, standard TLS verification, no redirects, a 1 MiB response bound and request timeouts. Requests are serialized per platform; queued requests and responses are invalidated on account or selected-broadcast changes. Browser-source proxies apply only to their source, not to OAuth, chat, metadata or RTMPS outputs.

Registered application IDs are not bundled. Exact Google Desktop and Twitch Public registrations, live chat reception, real moderation and the new broadcast lifecycle controls still require real-account acceptance. Google account connection has been reported working by the user. Automated tests are not a substitute for these checks.

## References

- [Twitch IRC](https://dev.twitch.tv/docs/chat/irc)
- [Twitch API: channel information, viewers, categories and moderation](https://dev.twitch.tv/docs/api/reference/)
- [YouTube live chat messages](https://developers.google.com/youtube/v3/live/docs/liveChatMessages/list)
- [YouTube chat bans](https://developers.google.com/youtube/v3/live/docs/liveChatBans/insert)
- [YouTube video updates](https://developers.google.com/youtube/v3/docs/videos/update)
