# Platform controls (0.2.5)

OBS registers two docks: StreamBridge and StreamBridge — Chat. The separate stream information dock is removed. YouTube lifecycle controls live in the main YouTube section; metadata editing opens the selected broadcast in Studio.

## YouTube selection and statistics

The account vault stores the selected broadcast ID, title and bound stream ID together with encrypted session data. Refresh-token rotation preserves this metadata. Settings restores the selected entry even before a broadcast list has been fetched. Applying an event's key does not replace the chosen ingest route. Save server and key persists both immediately; the official backup=1 query is accepted only for YouTube's RTMPS ingest hosts and path.

Viewer polling uses videos.list for the selected ID. liveBroadcasts.list supplies the chat ID when the video resource lacks one or the previous chat ID has ended. Missing/completed selection triggers bounded, paginated active-broadcast discovery; a matching bound stream is preferred and ambiguous choices are never guessed. An upcoming selection only follows an active event with the same bound stream. Discovery changes monitoring metadata only, never the output server/key or broadcast lifecycle. Selection can also be changed explicitly during streaming without changing the running output.

Counts accept nonnegative integer JSON numbers and digit strings. A missing count during a live broadcast shows 0* with a zero-or-hidden explanation; it is not a confirmed zero. Errors, upcoming events and completed events show a dash. Requests are coalesced and queued behind chat, not skipped. Hovering the badge shows API status and selected broadcast title.

## Chat

YouTube uses liveChat/messages; Twitch uses authenticated IRC over TLS. A custom Qt delegate paints translucent red/purple backgrounds independently of OBS item styles. Names are bold platform colors and clickable. Twitch badges use the authenticated global/channel badge catalog and provider artwork; YouTube exposes owner/moderator/member/verified flags but no badge-image URLs, so these use locally drawn role symbols. Untrusted message text is escaped before rich rendering. History is bounded to 500 displayed messages and never logged to disk. Moderation uses exact message/user/room IDs and confirms destructive actions.

Temporary YouTube network failures, HTTP 5xx and rate limiting retry with bounded backoff. Missing/ended chat IDs trigger fresh broadcast metadata; disabled chat and permission/quota errors are reported separately. Account or broadcast changes invalidate pending responses.

Chats connect automatically for linked accounts and refresh on stream start; this can be disabled under Settings. The dock shows compact Online/Offline/Error states, HTTP/network codes, detailed tooltips and an icon reconnect button. The log moved to an About button. Uptime is HH:MM:SS and can exceed 24 hours.

Settings offers Off, Astra (default melody) and a local PCM WAV, a 0–100% volume slider and preview. Astra retains its 160 ms envelope/frequencies with twice the former amplitude (peak below -25 dBFS). WAV supports 8/16-bit mono/stereo, 8–96 kHz, up to 10 seconds/4 MB. Windows waveOut receives sample-scaled audio; no global volume is changed. History remains silent and notifications share a 1.5-second cooldown; a playing custom clip is not overlapped. Desktop Audio can capture playback. Preferences are applied immediately and persist in ui.ini; old sound preferences migrate. Sound stays off by default.

Clicking a Twitch name requests channels/followers for that exact user and authorized broadcaster. Older accounts must reconnect to grant moderator:read:followers; permission failure is distinct from not following. YouTube does not provide this date for arbitrary chat viewers.

Twitch IRC emote ranges are interpreted as Unicode codepoint offsets. BetterTTV global and channel/shared emotes load from its public API. Twitch and BetterTTV animated GIFs use Qt's GIF decoder with bounded buffers. Direct GIF URLs support media.tenor.com and Giphy media hosts, not arbitrary URLs or provider page links. Images are limited to PNG/GIF, 512×512 source dimensions and 2 MB; catalogs to 1 MB/4,000 emotes; four concurrent anonymous downloads, a bounded queue and a 16 MB cost-based cache. Verified HTTPS, fixed provider/media hosts, no redirects or local-file resources, and escaped text prevent message-driven arbitrary resource loading. Failed media retain readable emote text; the reconnect icon retries. GIF decoding works offline with fixtures; remote CDN availability depends on the user's network.

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

- [Twitch follower API and required scope](https://dev.twitch.tv/docs/api/reference/#get-channel-followers)
- [Twitch badges](https://dev.twitch.tv/docs/chat/send-receive-messages#chat-badges)
- [BetterTTV API](https://betterttv.com/developers)
