// SPDX-License-Identifier: GPL-2.0-or-later
#include "host.hpp"
#include "oauth/loopback.hpp"
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <cstring>
#include <windows.h>

namespace sb {
QString validateBrowser(const BrowserConfig &c) {
    const QUrl url(c.url, QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != "https" || url.host().isEmpty() || !url.userInfo().isEmpty() ||
        c.url.size() > 8192 || c.width < 16 || c.width > static_cast<int>(browser::maxWidth) ||
        c.height < 16 || c.height > static_cast<int>(browser::maxHeight) || c.fps < 1 || c.fps > 60)
        return "BrowserInvalid";
    return c.proxyEnabled ? validateProxy(c.proxy) : QString();
}
BrowserHost::BrowserHost(QObject *parent) : QObject(parent), adapter_(this), process_(this) {
    process_.setProcessChannelMode(QProcess::SeparateChannels);
    process_.setStandardErrorFile(QProcess::nullDevice());
    process_.setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
    connect(&process_, &QProcess::readyReadStandardOutput, this, &BrowserHost::read);
    connect(&process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        closeJob();
        adapter_.stop();
        buffer_.clear();
        if (!stopping_)
            emit failed(QString("BrowserProcessExit (%1)").arg(code));
    });
    // A failed favicon, preconnect or subresource must not restart a healthy page.
    connect(
        &adapter_, &ProxyAdapter::failed, this,
        [this](const QString &code) {
            if (!stopping_)
                emit networkWarning(code);
        },
        Qt::QueuedConnection);
}
BrowserHost::~BrowserHost() {
    stop();
}
void BrowserHost::closeJob() {
    if (job_) {
        CloseHandle(job_);
        job_ = nullptr;
    }
}
void BrowserHost::stop() {
    stopping_ = true;
    if (process_.state() != QProcess::NotRunning) {
        process_.write("Q", 1);
        process_.closeWriteChannel();
        if (!process_.waitForFinished(2000)) {
            closeJob();
            process_.kill();
            process_.waitForFinished(1000);
        }
    }
    closeJob();
    adapter_.stop();
    buffer_.clear();
    directory_.reset();
    config_.proxy.password.fill('\0');
    config_.proxy.password.clear();
}
bool BrowserHost::start(const QString &helperPath, const BrowserConfig &config, QString &error) {
    stop();
    error = validateBrowser(config);
    if (!error.isEmpty())
        return false;
    const QFileInfo helper(helperPath);
    if (!helper.isAbsolute() || !helper.isFile()) {
        error = "BrowserHelperMissing";
        return false;
    }
    auto capability = randomUrlToken();
    if (config.proxyEnabled && !adapter_.start(config.proxy, capability, error))
        return false;
    config_ = config;
    directory_ = std::make_unique<QTemporaryDir>();
    if (!directory_->isValid()) {
        stop();
        error = "BrowserProfileFailed";
        return false;
    }
    job_ = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job_ || !SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        stop();
        error = "BrowserIsolationFailed";
        return false;
    }
    process_.setWorkingDirectory(helper.absolutePath());
    process_.start(helper.absoluteFilePath(), {});
    if (!process_.waitForStarted(5000)) {
        stop();
        error = "BrowserStartFailed";
        return false;
    }
    const auto process =
        OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                    static_cast<DWORD>(process_.processId()));
    const bool assigned = process && AssignProcessToJobObject(job_, process);
    if (process)
        CloseHandle(process);
    if (!assigned) {
        stop();
        error = "BrowserIsolationFailed";
        return false;
    }
    // The child waits for bootstrap input; its descendants can only start after job assignment.
    const QJsonObject object{{"url", config.url},
                             {"port", adapter_.port()},
                             {"capability", QString::fromLatin1(capability)},
                             {"width", config.width},
                             {"height", config.height},
                             {"fps", config.fps},
                             {"proxy", config.proxyEnabled},
                             {"directory", directory_->path()}};
    auto json = QJsonDocument(object).toJson(QJsonDocument::Compact);
    const auto size = static_cast<uint32_t>(json.size());
    process_.write(reinterpret_cast<const char *>(&size), sizeof(size));
    process_.write(json);
    json.fill('\0');
    capability.fill('\0');
    stopping_ = false;
    return true;
}
void BrowserHost::refresh() {
    if (!stopping_ && process_.state() == QProcess::Running)
        process_.write("R", 1);
}
void BrowserHost::read() {
    if (stopping_) {
        process_.readAllStandardOutput();
        return;
    }
    buffer_ += process_.readAllStandardOutput();
    if (buffer_.size() > browser::maxPayload * 2 + sizeof(browser::Header) * 2) {
        emit failed("BrowserPacketLimit");
        closeJob();
        return;
    }
    while (buffer_.size() >= sizeof(browser::Header)) {
        browser::Header h;
        std::memcpy(&h, buffer_.constData(), sizeof(h));
        const bool videoPacket = h.type == browser::PacketType::Video &&
                                 h.widthOrFrames == static_cast<uint32_t>(config_.width) &&
                                 h.heightOrChannels == static_cast<uint32_t>(config_.height) &&
                                 h.bytes == h.widthOrFrames * h.heightOrChannels * 4;
        const bool audioPacket = h.type == browser::PacketType::Audio && h.widthOrFrames > 0 &&
                                 h.widthOrFrames <= 4800 && h.heightOrChannels == 2 &&
                                 h.sampleRateOrCode == 48000 &&
                                 h.bytes == h.widthOrFrames * 2 * sizeof(float);
        const bool statusPacket = h.type == browser::PacketType::Status && h.bytes == 0 &&
                                  h.sampleRateOrCode >= 1 && h.sampleRateOrCode <= 5;
        if (h.signature != browser::magic || h.bytes > browser::maxPayload ||
            (!videoPacket && !audioPacket && !statusPacket)) {
            emit failed("BrowserInvalidPacket");
            closeJob();
            return;
        }
        if (buffer_.size() < static_cast<qsizetype>(sizeof(h) + h.bytes))
            return;
        const auto payload = buffer_.mid(sizeof(h), h.bytes);
        buffer_.remove(0, sizeof(h) + h.bytes);
        if (videoPacket)
            emit video(payload, h.widthOrFrames, h.heightOrChannels, h.timestamp);
        else if (audioPacket)
            emit audio(payload, h.widthOrFrames, h.timestamp);
        else
            emit status(h.sampleRateOrCode);
        // A consumer may stop the host synchronously from a packet callback.
        if (stopping_)
            return;
    }
}
} // namespace sb
