// SPDX-License-Identifier: GPL-2.0-or-later
#include "include/cef_app.h"
#include "include/cef_audio_handler.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_download_handler.h"
#include "include/cef_parser.h"
#include "include/cef_permission_handler.h"
#include "include/cef_request_context.h"
#include "include/cef_request_context_handler.h"
#include "include/cef_resource_request_handler.h"
#include "include/cef_sandbox_win.h"
#include "include/cef_version_info.h"
#include "include/wrapper/cef_closure_task.h"
#include "writer.hpp"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <functional>
#include <memory>
#ifdef STREAMBRIDGE_TEST_FIXTURE
#include "test-fixture.hpp"
#endif

namespace {
using namespace sb::browser;
struct Config {
    std::string url, capability, directory;
    int port = 0, width = 800, height = 600, fps = 30;
    bool proxy = true;
};
bool allowed(const CefString &value) {
    CefURLParts parts;
    if (!CefParseURL(value, parts))
        return false;
    const auto scheme = CefString(&parts.scheme).ToString();
    return scheme == "https" || scheme == "wss" || scheme == "data" || scheme == "blob" ||
           value == "about:blank";
}
class ResourcePolicy final : public CefResourceRequestHandler {
  public:
    ReturnValue OnBeforeResourceLoad(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
                                     CefRefPtr<CefRequest> request, CefRefPtr<CefCallback>) override {
        return allowed(request->GetURL()) ? RV_CONTINUE : RV_CANCEL;
    }

  private:
    IMPLEMENT_REFCOUNTING(ResourcePolicy);
};
class ContextPolicy final : public CefRequestContextHandler {
  public:
    explicit ContextPolicy(std::function<void(CefRefPtr<CefRequestContext>)> ready)
        : ready_(std::move(ready)) {}
    void OnRequestContextInitialized(CefRefPtr<CefRequestContext> context) override {
        auto policy = CefValue::Create();
        policy->SetString("disable_non_proxied_udp");
        CefString error;
        const auto existing = context->GetPreference("webrtc.ip_handling_policy");
        if ((!existing || existing->GetString() != "disable_non_proxied_udp") &&
            !context->SetPreference("webrtc.ip_handling_policy", policy, error)) {
            std::fprintf(stderr, "StreamBridge helper: required WebRTC policy unavailable\n");
            CefQuitMessageLoop();
            return;
        }
        auto ready = std::move(ready_);
        ready(context);
    }
    CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
                                                                   CefRefPtr<CefRequest>, bool,
                                                                   bool isDownload, const CefString &,
                                                                   bool &disableDefault) override {
        if (isDownload)
            disableDefault = true;
        return new ResourcePolicy;
    }

  private:
    std::function<void(CefRefPtr<CefRequestContext>)> ready_;
    IMPLEMENT_REFCOUNTING(ContextPolicy);
};
class Client final : public CefClient,
                     public CefRenderHandler,
                     public CefLifeSpanHandler,
                     public CefLoadHandler,
                     public CefRequestHandler,
                     public CefAudioHandler,
                     public CefPermissionHandler,
                     public CefDownloadHandler {
  public:
    Client(Config config, std::shared_ptr<Writer> writer)
        : config_(std::move(config)), writer_(std::move(writer)) {}
    CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
    CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
    CefRefPtr<CefAudioHandler> GetAudioHandler() override { return this; }
    CefRefPtr<CefPermissionHandler> GetPermissionHandler() override { return this; }
    CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }
    bool CanDownload(CefRefPtr<CefBrowser>, const CefString &, const CefString &) override { return false; }
    bool OnRequestMediaAccessPermission(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, const CefString &,
                                        uint32_t, CefRefPtr<CefMediaAccessCallback> callback) override {
        callback->Cancel();
        return true;
    }
    bool OnShowPermissionPrompt(CefRefPtr<CefBrowser>, uint64_t, const CefString &, uint32_t,
                                CefRefPtr<CefPermissionPromptCallback> callback) override {
        callback->Continue(CEF_PERMISSION_RESULT_DENY);
        return true;
    }
    void GetViewRect(CefRefPtr<CefBrowser>, CefRect &rect) override {
        rect = CefRect(0, 0, config_.width, config_.height);
    }
    void OnPaint(CefRefPtr<CefBrowser>, PaintElementType type, const RectList &, const void *buffer,
                 int width, int height) override {
        if (type != PET_VIEW || width != config_.width || height != config_.height)
            return;
        Header h;
        h.type = PacketType::Video;
        h.bytes = width * height * 4;
        h.widthOrFrames = width;
        h.heightOrChannels = height;
        writer_->push(h, buffer);
    }
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
        browser_ = browser;
        writer_->status(Status::Ready);
    }
    void OnBeforeClose(CefRefPtr<CefBrowser>) override {
        browser_ = nullptr;
        CefQuitMessageLoop();
    }
    bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString &, const CefString &,
                       CefLifeSpanHandler::WindowOpenDisposition, bool, const CefPopupFeatures &,
                       CefWindowInfo &, CefRefPtr<CefClient> &, CefBrowserSettings &,
                       CefRefPtr<CefDictionaryValue> &, bool *) override {
        return true;
    }
    bool OnBeforeBrowse(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest> request, bool,
                        bool) override {
        return !allowed(request->GetURL());
    }
    bool GetAuthCredentials(CefRefPtr<CefBrowser>, const CefString &, bool proxy, const CefString &host,
                            int port, const CefString &, const CefString &scheme,
                            CefRefPtr<CefAuthCallback> callback) override {
        std::fprintf(stderr, "StreamBridge helper: auth proxy=%d loopback=%d port-match=%d basic=%d\n", proxy,
                     host == "127.0.0.1", port == config_.port, scheme == "basic");
        if (!config_.proxy || !proxy || host != "127.0.0.1" || port != config_.port || scheme != "basic")
            return false;
        callback->Continue("streambridge", config_.capability);
        return true;
    }
    void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, int status) override {
        if (!frame->IsMain())
            return;
        std::fprintf(stderr, "StreamBridge helper: HTTP status %d\n", status);
        if (status < 200 || status >= 400) {
            writer_->status(Status::LoadFailed);
            return;
        }
        frame->ExecuteJavaScript("document.documentElement.style.backgroundColor='transparent';if(document."
                                 "body)document.body.style.backgroundColor='transparent';",
                                 frame->GetURL(), 0);
        writer_->status(Status::Loaded);
        frame->GetBrowser()->GetHost()->Invalidate(PET_VIEW);
    }
    void OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, ErrorCode code, const CefString &,
                     const CefString &) override {
        std::fprintf(stderr, "StreamBridge helper: load error %d\n", static_cast<int>(code));
        if (frame->IsMain() && code != ERR_ABORTED)
            writer_->status(Status::LoadFailed);
    }
    bool GetAudioParameters(CefRefPtr<CefBrowser>, CefAudioParameters &params) override {
        params.channel_layout = CEF_CHANNEL_LAYOUT_STEREO;
        params.sample_rate = 48000;
        params.frames_per_buffer = 480;
        return true;
    }
    void OnAudioStreamStarted(CefRefPtr<CefBrowser>, const CefAudioParameters &params,
                              int channels) override {
        audioChannels_ = channels == 2 && params.sample_rate == 48000 ? 2 : 0;
    }
    void OnAudioStreamPacket(CefRefPtr<CefBrowser>, const float **data, int frames, int64_t) override {
        if (audioChannels_ != 2 || frames <= 0 || frames > 4800)
            return;
        std::vector<float> planar(frames * 2);
        std::copy_n(data[0], frames, planar.data());
        std::copy_n(data[1], frames, planar.data() + frames);
        Header h;
        h.type = PacketType::Audio;
        h.bytes = frames * 2 * sizeof(float);
        h.widthOrFrames = frames;
        h.heightOrChannels = 2;
        h.sampleRateOrCode = 48000;
        writer_->push(h, planar.data());
    }
    void OnAudioStreamStopped(CefRefPtr<CefBrowser>) override { audioChannels_ = 0; }
    void OnAudioStreamError(CefRefPtr<CefBrowser>, const CefString &) override {
        writer_->status(Status::AudioFailed);
    }
    void OnRenderProcessTerminated(CefRefPtr<CefBrowser>, TerminationStatus, int,
                                   const CefString &) override {
        writer_->status(Status::RendererFailed);
    }
    void command(bool close) {
        if (!browser_) {
            if (close)
                CefQuitMessageLoop();
            return;
        }
        if (close)
            browser_->GetHost()->CloseBrowser(true);
        else
            browser_->ReloadIgnoreCache();
    }

  private:
    Config config_;
    std::shared_ptr<Writer> writer_;
    CefRefPtr<CefBrowser> browser_;
    std::atomic<int> audioChannels_{0};
    IMPLEMENT_REFCOUNTING(Client);
};
class App final : public CefApp, public CefBrowserProcessHandler {
  public:
    App(Config config, CefRefPtr<Client> client) : config_(std::move(config)), client_(client) {}
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }
    void OnBeforeCommandLineProcessing(const CefString &type, CefRefPtr<CefCommandLine> command) override {
        if (!type.empty())
            return;
        if (config_.proxy) {
            command->AppendSwitchWithValue("proxy-server",
                                           "http://127.0.0.1:" + std::to_string(config_.port));
            command->AppendSwitchWithValue("proxy-bypass-list", "<-loopback>");
            // Chromium applies mapping rules even to literal proxy addresses. The sole
            // exception is the local adapter IP; resolving origins still always fails.
            command->AppendSwitchWithValue("host-resolver-rules", "MAP * ~NOTFOUND, EXCLUDE 127.0.0.1");
        } else
            command->AppendSwitch("no-proxy-server");
        for (const char *name : {"disable-quic", "disable-background-networking", "disable-component-update",
                                 "disable-sync", "disable-domain-reliability", "disable-extensions"})
            command->AppendSwitch(name);
        command->AppendSwitchWithValue("force-webrtc-ip-handling-policy", "disable_non_proxied_udp");
        command->AppendSwitchWithValue("webrtc-ip-handling-policy", "disable_non_proxied_udp");
        command->AppendSwitchWithValue(
            "disable-features", "DnsOverHttps,MediaRouter,OptimizationHints,AutofillServerCommunication");
        command->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");
        command->AppendSwitch("disable-chrome-login-prompt");
    }
    void OnContextInitialized() override {
        std::fprintf(stderr, "StreamBridge helper: context initialized\n");
        CefRequestContextSettings contextSettings;
        CefRequestContext::CreateContext(
            contextSettings,
            new ContextPolicy([config = config_, client = client_](CefRefPtr<CefRequestContext> context) {
#ifdef STREAMBRIDGE_TEST_FIXTURE
                registerTestFixture(context);
#endif
                CefWindowInfo window;
                window.SetAsWindowless(nullptr);
                CefBrowserSettings browser;
                browser.windowless_frame_rate = config.fps;
                browser.background_color = 0;
                if (!CefBrowserHost::CreateBrowser(window, client, config.url, browser, nullptr, context))
                    CefQuitMessageLoop();
            }));
    }

  private:
    Config config_;
    CefRefPtr<Client> client_;
    IMPLEMENT_REFCOUNTING(App);
};
class Command final : public CefTask {
  public:
    Command(CefRefPtr<Client> client, bool close) : client_(client), close_(close) {}
    void Execute() override { client_->command(close_); }

  private:
    CefRefPtr<Client> client_;
    bool close_;
    IMPLEMENT_REFCOUNTING(Command);
};
bool readAll(HANDLE pipe, void *buffer, DWORD size) {
    auto *bytes = static_cast<char *>(buffer);
    while (size) {
        DWORD read = 0;
        if (!ReadFile(pipe, bytes, size, &read, nullptr) || read == 0)
            return false;
        size -= read;
        bytes += read;
    }
    return true;
}
} // namespace
CEF_BOOTSTRAP_EXPORT int RunConsoleMain(int, char **, void *sandbox, cef_version_info_t *) {
    std::fprintf(stderr, "StreamBridge helper: entry\n");
    if (!sandbox)
        return 2; // A missing sandbox is an error, never an implicit no-sandbox mode.
    CefMainArgs args(GetModuleHandleW(nullptr));
    const int child = CefExecuteProcess(args, nullptr, sandbox);
    if (child >= 0)
        return child;
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    uint32_t size = 0;
    if (!readAll(input, &size, sizeof(size)) || size == 0 || size > 32768)
        return 3;
    std::fprintf(stderr, "StreamBridge helper: bootstrap received\n");
    std::string json(size, '\0');
    if (!readAll(input, json.data(), size))
        return 3;
    auto value = CefParseJSON(json, JSON_PARSER_RFC);
    SecureZeroMemory(json.data(), json.size());
    if (!value || value->GetType() != VTYPE_DICTIONARY)
        return 3;
    auto object = value->GetDictionary();
    Config config;
    config.url = object->GetString("url");
    config.capability = object->GetString("capability");
    config.directory = object->GetString("directory");
    config.port = object->GetInt("port");
    config.width = object->GetInt("width");
    config.height = object->GetInt("height");
    config.fps = object->GetInt("fps");
    config.proxy = !object->HasKey("proxy") || object->GetBool("proxy");
    CefURLParts url;
    if (!CefParseURL(config.url, url) || CefString(&url.scheme) != "https" ||
        !CefString(&url.username).empty() || !CefString(&url.password).empty() || config.directory.empty() ||
        config.width < 16 || config.width > static_cast<int>(maxWidth) || config.height < 16 ||
        config.height > static_cast<int>(maxHeight) || config.fps < 1 || config.fps > 60 ||
        (config.proxy && (config.port <= 0 || config.port > 65535 || config.capability.size() < 32 ||
                          config.capability.size() > 128)))
        return 3;
    auto writer = std::make_shared<Writer>();
    CefRefPtr<Client> client = new Client(config, writer);
    CefRefPtr<App> app = new App(config, client);
    CefSettings settings;
    settings.windowless_rendering_enabled = true;
    settings.background_color = 0;
    CefString(&settings.root_cache_path) = config.directory;
    settings.log_severity = LOGSEVERITY_DISABLE;
    settings.command_line_args_disabled = true;
    if (!CefInitialize(args, settings, app, sandbox))
        return CefGetExitCode();
    std::fprintf(stderr, "StreamBridge helper: CEF initialized\n");
    std::atomic<bool> finished{false};
    std::thread commands([&] {
        char byte;
        while (!finished && readAll(input, &byte, 1)) {
            if (byte == 'R')
                CefPostTask(TID_UI, new Command(client, false));
            if (byte == 'Q')
                break;
        }
        if (!finished)
            CefPostTask(TID_UI, new Command(client, true));
    });
    CefRunMessageLoop();
    finished = true;
    do {
        CancelSynchronousIo(commands.native_handle());
    } while (WaitForSingleObject(commands.native_handle(), 50) == WAIT_TIMEOUT);
    commands.join();
    app = nullptr;
    client = nullptr;
    CefShutdown();
    writer.reset();
    return 0;
}
