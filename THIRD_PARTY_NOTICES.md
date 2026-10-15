# Third-party notices

StreamBridge source: GPL-2.0-or-later, Copyright 2026 StreamBridge contributors. See LICENSE. This is a development milestone, not a completed dependency/security certification.

| Component | Use | Licensing and distribution |
|---|---|---|
| OBS Studio 32.0.4 SDK / host libobs and frontend API | Dynamic plugin ABI | GPL-2.0-or-later. [COPYING](https://github.com/obsproject/obs-studio/blob/32.0.4/COPYING). Host DLLs not redistributed. |
| Qt 6.8.3 Core, Gui, Widgets, Network | Dynamic UI/runtime provided by OBS | LGPLv3/GPL/commercial options per module. Use LGPLv3 terms and GPLv3-or-later option for combined work as applicable. [Licensing](https://www.qt.io/licensing/open-source-lgpl-obligations). Host Qt DLLs are not redistributed; retain user ability to replace compatible libraries. |
| Qt 6.8.3 Schannel TLS backend | Redistributed `qt-plugins/tls/qschannelbackend.dll`, from pinned OBS Qt dependency bundle | Backend source permits LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only OR commercial. Distributed under the GPL-2.0-only option with `qt-plugins/LICENSE.GPL2`. Matching upstream `qtbase-everywhere-src-6.8.3.tar.xz` and OBS build recipe `obs-deps-2025-08-23-source.tar.gz` accompany every release with pinned hashes. See `src/plugins/tls/schannel` and `deps.qt/qt6.ps1` in those source archives. |
| Qt 6.8.3 GIF image plugin | Redistributed `qt-plugins/imageformats/qgif.dll` from the pinned OBS Qt bundle | GPL-2.0-only option, with `qt-plugins/LICENSE.GPL2`. Matching source is `src/plugins/imageformats/gif` in the same Qt base source archive and OBS build recipe shipped with the release. |
| Twitch / BetterTTV / YouTube | Platform names, API data and chat artwork | Twitch badges/emotes and BetterTTV emotes are fetched at runtime from provider APIs/CDNs; rights remain with their owners. They are not bundled or relicensed as plugin artwork. YouTube role flags use locally drawn symbols. No affiliation or endorsement is implied. |
| Windows SDK / MSVC runtime | Build and host runtime; standalone updater | Microsoft terms apply to SDK/runtime. Compiler and SDK are not shipped. Plugin uses OBS runtime; standalone updater statically links the redistributable MSVC runtime and uses Windows WinHTTP, CNG and JSON APIs. |
| obs-plugintemplate | Studied for structure/bootstrap; GPL license text | GPL-2.0-or-later notices. No template application implementation is incorporated. |
| obs-multi-rtmp 0.7.4.4 / obs-browser | Research only | GPL repositories; no implementation files copied into shipped source. |
| NSIS 3.11 | Installer compiler/stub | zlib/libpng license for NSIS core; distribution includes other licenses for compiler components. [NSIS license](https://nsis.sourceforge.io/License). Compiler is a build tool, not installed with the plugin. |

| CEF 154.0.34+g14c5a08 / Chromium 154.0.8037.98, Windows x64 minimal | Separate browser runtime/bootstrap and compiled libcef wrapper | CEF BSD 3-clause. Exact `LICENSE.txt` is packaged as `browser/CEF-LICENSE.txt`, and full `CREDITS.html` as `browser/CHROMIUM-CREDITS.html`. [CEF source](https://github.com/chromiumembedded/cef/tree/14c5a08), [Chromium source](https://chromium.googlesource.com/chromium/src/+/154.0.8037.98/). Retain all runtime notices. |
| MediaMTX 1.21.2 | Local RTMPS testing only | MIT; [license](https://github.com/bluenviron/mediamtx/blob/v1.21.2/LICENSE). Not distributed with StreamBridge. |

`.deps` contains research/build inputs and is excluded from the StreamBridge source ZIP. `dependencies.lock.json` supplies exact URLs and hashes. The two Qt corresponding-source archives are separate release assets. Binary packages include allowlisted CEF runtime files/locales, helper, plugin, Qt Schannel backend, Qt GIF plugin and resources. No OAuth SDK, proprietary codec SDK or copied obs-multi-rtmp implementation is included.

Redistribution must retain CEF/Chromium notices, provide matching StreamBridge source/build instructions and meet applicable third-party source obligations. CEF's permissive license is compatible with the GPL source grant; Chromium libraries retain individual terms in CREDITS. Qt LGPLv3 use with GPL code relies on the GPLv3-or-later option when necessary, not GPLv2-only. Source availability and distribution compliance must be reviewed for the actual public release; this inventory is not an independent legal certification.
