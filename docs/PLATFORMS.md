# Platform controls (0.2.4)

OBS registers two docks: StreamBridge and StreamBridge — Chat. The separate stream information dock is removed. YouTube lifecycle controls live in the main YouTube section; metadata editing opens the selected broadcast in Studio.

## YouTube selection and statistics

The account vault stores the selected broadcast ID, title and bound stream ID together with encrypted session data. Refresh-token rotation preserves this metadata. Settings restores the selected entry even before a broadcast list has been fetched. Applying an event's key does not replace the chosen ingest route. Save server and key persists both immediately; the official backup=1 query is accepted only for YouTube's RTMPS ingest hosts and path.

Viewer polling uses videos.list for the selected ID. liveBroadcasts.list supplies the chat ID when the video resource lacks one or the previous chat ID has ended. Missing/completed selection triggers bounded, paginated active-broadcast discovery; a matching bound stream is preferred and ambiguous choices are never guessed. An upcoming selection only follows an active event with the same bound stream. Discovery changes monitoring metadata only, never the output server/key or broadcast lifecycle. Selection can also be changed explicitly during streaming without changing the running output.

Counts accept nonnegative integer JSON numbers and digit strings. A missing count during a live broadcast shows 0* with a zero-or-hidden explanation; it is not a confirmed zero. Errors, upcoming events and completed events show a dash. Requests are coalesced and queued behind chat, not skipped. Hovering the badge shows API status and selected broadcast title.

## Chat

YouTube uses liveChat/messages; Twitch uses authenticated IRC over TLS. Rows have translucent red/purple backgrounds, platform icons and plain-text content. History is bounded to 500 displayed messages and never logged to disk. Moderation uses exact message/user/room IDs and confirms destructive actions.

Temporary YouTube network failures, HTTP 5xx and rate limiting retry with bounded backoff. Missing/ended chat IDs trigger fresh broadcast metadata; disabled chat and permission/quota errors are reported separately. Account or broadcast changes invalidate pending responses.

The optional quiet sound defaults off and is saved in ui.ini. A locally synthesized 160 ms mono PCM tone has a faded envelope and peak amplitude below -31 dBFS; Windows session/system volume still applies. History is silent; new messages share a 1.5-second cooldown. No media downloads, global volume changes or Qt Multimedia dependency are required. Desktop audio capture can include the notification.

## Lifecycle, security and validation

Go live checks fresh state and active ingest, performs the monitor/testing transition if required and polls intermediate states. End requires confirmation and stops only the YouTube output after verified API completion. Twitch remains independent.

API requests keep TLS verification, fixed provider hosts, no redirects, bounded response size and timeouts. OAuth and stream credentials use Windows DPAPI. Browser-source proxies do not apply to OAuth, chat or RTMPS.

Real-account reception and provider-specific failures require user acceptance. Automated tests cover selection persistence, discovery, chat fallback/retry, viewer updates, UI restoration and notification policy without accessing a real account.

## References

- [YouTube broadcasts and chat IDs](https://developers.google.com/youtube/v3/live/docs/liveBroadcasts)
- [YouTube active-broadcast filters](https://developers.google.com/youtube/v3/live/docs/liveBroadcasts/list)
- [YouTube viewer count semantics](https://developers.google.com/youtube/v3/docs/videos#liveStreamingDetails.concurrentViewers)
- [YouTube live chat messages](https://developers.google.com/youtube/v3/live/docs/liveChatMessages/list)
- [Twitch IRC](https://dev.twitch.tv/docs/chat/irc)
