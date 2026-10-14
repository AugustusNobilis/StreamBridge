// SPDX-License-Identifier: GPL-2.0-or-later
#include "dock.hpp"
#include "browser/source.hpp"
#include "locale.hpp"
#include "settings.hpp"
#include "updates/checker.hpp"
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

namespace sb {
Dock::Dock(OutputManager &manager, QWidget *parent) : QWidget(parent), manager_(manager) {
    setMinimumWidth(250);
    auto *layout = new QVBoxLayout(this);
    mode_ = new QComboBox(this);
    mode_->addItems({text("Common"), text("Advanced")});
    layout->addWidget(mode_);
    connect(mode_, &QComboBox::currentIndexChanged, this, [this](int index) {
        auto config = manager_.config();
        config.advanced = index == 1;
        QString error;
        if (!manager_.configure(config, error))
            QMessageBox::warning(this, "StreamBridge", error);
        refresh();
    });
    for (size_t i = 0; i < 2; ++i) {
        auto *box = new QGroupBox(endpointName(i), this);
        auto *v = new QVBoxLayout(box);
        status_[i] = new QLabel(box);
        status_[i]->setWordWrap(true);
        status_[i]->setTextFormat(Qt::PlainText);
        metrics_[i] = new QLabel(box);
        metrics_[i]->setWordWrap(true);
        v->addWidget(status_[i]);
        v->addWidget(metrics_[i]);
        auto *buttons = new QHBoxLayout;
        start_[i] = new QPushButton(text("Start"), box);
        stop_[i] = new QPushButton(text("Stop"), box);
        buttons->addWidget(start_[i]);
        buttons->addWidget(stop_[i]);
        v->addLayout(buttons);
        connect(start_[i], &QPushButton::clicked, this, [this, i] { manager_.start(i); });
        connect(stop_[i], &QPushButton::clicked, this, [this, i] { manager_.stop(i); });
        layout->addWidget(box);
    }
    auto *all = new QHBoxLayout;
    auto *start = new QPushButton(text("StartAll"), this);
    auto *stop = new QPushButton(text("StopAll"), this);
    all->addWidget(start);
    all->addWidget(stop);
    layout->addLayout(all);
    connect(start, &QPushButton::clicked, &manager_, &OutputManager::startAll);
    connect(stop, &QPushButton::clicked, &manager_, &OutputManager::stopAll);
    auto *proxy = new QLabel(browserSummary(), this);
    proxy->setWordWrap(true);
    layout->addWidget(proxy);
    auto *browserTimer = new QTimer(this);
    connect(browserTimer, &QTimer::timeout, proxy, [proxy] { proxy->setText(browserSummary()); });
    browserTimer->start(1000);
    auto *settings = new QPushButton(text("Settings"), this);
    connect(settings, &QPushButton::clicked, this, &Dock::openSettings);
    layout->addWidget(settings);
    if (auto *checker = updateChecker()) {
        auto *update = new QPushButton(text("UpdateNotice"), this);
        const auto refreshUpdate = [checker, update] {
            update->setVisible(checker->canDownload() || checker->ready());
        };
        connect(checker, &UpdateChecker::changed, update, refreshUpdate);
        connect(update, &QPushButton::clicked, this, &Dock::openSettings);
        layout->addWidget(update);
        refreshUpdate();
    }
    layout->addStretch();
    connect(&manager_, &OutputManager::changed, this, &Dock::refresh);
    refresh();
}
void Dock::openSettings() {
    Settings dialog(manager_, this);
    dialog.exec();
}
void Dock::refresh() {
    const QSignalBlocker blocker(mode_);
    mode_->setCurrentIndex(manager_.config().advanced ? 1 : 0);
    mode_->setEnabled(!manager_.busy());
    constexpr const char *states[] = {"Idle", "Connecting", "Live", "Backoff", "Stopping", "Failed"};
    for (size_t i = 0; i < 2; ++i) {
        const auto &s = manager_.status(i);
        QString state = text(states[static_cast<int>(s.state)]);
        if (s.state == StreamState::Backoff)
            state += QString(" (%1 s)").arg(s.retrySeconds);
        status_[i]->setText(state + (s.error.isEmpty() ? "" : "\n" + s.error));
        status_[i]->setToolTip(manager_.config().endpoints[i].server);
        metrics_[i]->setText(
            text("Metrics").arg(s.kbps, 0, 'f', 0).arg(s.dropped).arg(s.seconds).arg(s.reconnects));
        const bool idle = s.state == StreamState::Idle || s.state == StreamState::Failed;
        start_[i]->setEnabled(idle);
        stop_[i]->setEnabled(!idle && s.state != StreamState::Stopping);
    }
}
} // namespace sb
