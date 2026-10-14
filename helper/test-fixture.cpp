// SPDX-License-Identifier: GPL-2.0-or-later
// Compiled ONLY in the separately named test helper, never in a distributable helper.
// Synthetic in-memory HTTPS origin: not evidence of a TLS server or DonationAlerts compatibility.
#include "test-fixture.hpp"
#include "include/cef_scheme.h"
#include <algorithm>
#include <cstring>
namespace {
constexpr char page[] = R"HTML(<!doctype html><html><head><style>
html,body { margin:0; background:transparent; overflow:hidden }
#box { position:absolute; top:32px; left:32px; width:64px; height:64px; background:rgba(255,0,0,.5); }
</style></head><body><div id="box"></div><script>
let ticks=0; setInterval(()=>{document.getElementById('box').style.left=(32+(ticks++%60))+'px'},33);
const ac = new AudioContext({sampleRate:48000});
const oscillator = ac.createOscillator(); oscillator.frequency.value=440;
const gain=ac.createGain(); gain.gain.value=0.1; oscillator.connect(gain).connect(ac.destination); oscillator.start(); ac.resume();
const pc=new RTCPeerConnection({iceServers:[{urls:'stun:127.0.0.1:38173'}]});
pc.createDataChannel('probe'); pc.createOffer().then(o=>pc.setLocalDescription(o)).catch(()=>{});
fetch('https://destination.streambridge.invalid/probe').catch(()=>{});
</script></body></html>)HTML";
class Resource final : public CefResourceHandler {
  public:
    bool Open(CefRefPtr<CefRequest>, bool &handle, CefRefPtr<CefCallback>) override {
        handle = true;
        return true;
    }
    void GetResponseHeaders(CefRefPtr<CefResponse> response, int64_t &length, CefString &) override {
        response->SetStatus(200);
        response->SetMimeType("text/html");
        length = sizeof(page) - 1;
    }
    bool Read(void *out, int count, int &read, CefRefPtr<CefResourceReadCallback>) override {
        read = static_cast<int>(std::min(static_cast<size_t>(count), sizeof(page) - 1 - offset_));
        std::memcpy(out, page + offset_, read);
        offset_ += read;
        return read != 0;
    }
    void Cancel() override {}

  private:
    size_t offset_ = 0;
    IMPLEMENT_REFCOUNTING(Resource);
};
class Factory final : public CefSchemeHandlerFactory {
  public:
    CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, const CefString &,
                                         CefRefPtr<CefRequest>) override {
        return new Resource;
    }

  private:
    IMPLEMENT_REFCOUNTING(Factory);
};
} // namespace
void registerTestFixture(CefRefPtr<CefRequestContext> context) {
    context->RegisterSchemeHandlerFactory("https", "fixture.streambridge.invalid", new Factory);
}
