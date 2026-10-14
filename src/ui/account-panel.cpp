// SPDX-License-Identifier: GPL-2.0-or-later
#include "account-panel.hpp"
#include "locale.hpp"
#include <QComboBox>
#include <QDesktopServices>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

namespace sb {
QWidget *accountPanel(OutputManager &manager, size_t index, QWidget *parent) {
    auto *panel = new QWidget(parent);
    auto *form = new QFormLayout(panel);
    auto &account = manager.account(index);
    auto *disclosure = new QLabel(text(index == 0 ? "GoogleDisclosure" : "TwitchDisclosure"), panel);
    disclosure->setWordWrap(true);
    disclosure->setTextFormat(Qt::PlainText);
    form->addRow(disclosure);
    auto *client = new QLineEdit(account.clientId(), panel);
    client->setMaxLength(256);
    form->addRow(text("OAuthClientId"), client);
    auto *status = new QLabel(panel);
    status->setWordWrap(true);
    status->setTextFormat(Qt::PlainText);
    form->addRow(status);
    auto *feedback = new QLabel(panel);
    feedback->setWordWrap(true);
    feedback->setTextFormat(Qt::PlainText);
    auto *connect = new QPushButton(text(index == 0 ? "ConnectYouTube" : "ConnectTwitch"), panel);
    auto *cancel = new QPushButton(text("CancelOAuth"), panel);
    auto *disconnect = new QPushButton(text("DisconnectAccount"), panel);
    auto *fetch = new QPushButton(text(index == 0 ? "LoadBroadcasts" : "UseAccountKey"), panel);
    auto *broadcasts = new QComboBox(panel);
    auto *select = new QPushButton(text("UseBroadcast"), panel);
    form->addRow(connect);
    form->addRow(cancel);
    form->addRow(disconnect);
    form->addRow(fetch);
    if (index == 0) {
        form->addRow(broadcasts);
        form->addRow(select);
    } else {
        broadcasts->hide();
        select->hide();
    }
    form->addRow(feedback);
    auto refresh = [&, client, status, connect, cancel, disconnect, fetch, broadcasts, select, index] {
        status->setText(text(account.statusKey().toUtf8().constData()) +
                        (account.label().isEmpty() ? QString() : "\n" + account.label()));
        const bool unlocked = !manager.busy();
        client->setEnabled(unlocked && !account.linked() && !account.busy());
        connect->setEnabled(client->isEnabled());
        cancel->setEnabled(account.busy());
        disconnect->setEnabled(unlocked && account.linked());
        fetch->setEnabled(unlocked && account.linked() && !account.busy());
        if (index == 0 && !account.busy()) {
            const auto chosen = broadcasts->currentData();
            broadcasts->clear();
            for (const auto &b : account.broadcasts())
                broadcasts->addItem(b.title, b.id);
            const int found = broadcasts->findData(chosen);
            if (found >= 0)
                broadcasts->setCurrentIndex(found);
        }
        select->setEnabled(unlocked && !account.busy() && broadcasts->count() > 0);
    };
    QObject::connect(&account, &OAuthAccount::changed, panel, refresh);
    QObject::connect(&manager, &OutputManager::changed, panel, refresh);
    QObject::connect(connect, &QPushButton::clicked, panel, [&, client] {
        if (!manager.busy())
            account.link(client->text());
    });
    QObject::connect(cancel, &QPushButton::clicked, panel, [&account, refresh] {
        account.cancel();
        refresh();
    });
    QObject::connect(disconnect, &QPushButton::clicked, panel, [&] {
        if (!manager.busy())
            account.disconnectAccount();
    });
    QObject::connect(fetch, &QPushButton::clicked, panel, [&] {
        if (!manager.busy())
            account.fetchIngest();
    });
    QObject::connect(select, &QPushButton::clicked, panel, [&, broadcasts] {
        if (!manager.busy())
            account.selectBroadcast(broadcasts->currentData().toString());
    });
    QObject::connect(&account, &OAuthAccount::authorizationRequired, panel,
                     [feedback, &account](const QUrl &url, const QString &code) {
                         feedback->setText(code.isEmpty() ? text("OAuthBrowserOpened")
                                                          : text("OAuthDeviceCode").arg(code));
                         if (!QDesktopServices::openUrl(url)) {
                             account.cancel();
                             feedback->setText(text("OAuthBrowserFailed"));
                         }
                     });
    QObject::connect(&account, &OAuthAccount::ingestReady, panel,
                     [&, index, feedback](const QString &server, QByteArray key) {
                         QString error;
                         feedback->setText(manager.applyIngest(index, server, key, error)
                                               ? text("OAuthIngestSaved")
                                               : error);
                         key.fill('\0');
                     });
    // Closing Settings cancels an unfinished login; stored sessions remain usable.
    QObject::connect(panel, &QObject::destroyed, &account, [&account] {
        if (account.busy())
            account.cancel();
    });
    refresh();
    return panel;
}
} // namespace sb
