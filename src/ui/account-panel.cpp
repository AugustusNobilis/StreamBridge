// SPDX-License-Identifier: GPL-2.0-or-later
#include "account-panel.hpp"
#include "locale.hpp"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

namespace sb {
QWidget *accountPanel(OutputManager &manager, size_t index, QWidget *parent) {
    auto *panel = new QWidget(parent);
    auto *form = new QFormLayout(panel);
    auto &account = manager.account(index);
    auto *connect = new QPushButton(text(index == 0 ? "ConnectYouTube" : "ConnectTwitch"), panel);
    form->addRow(connect);
    auto *advanced = new QGroupBox(text("OAuthSetup"), panel);
    advanced->setCheckable(true);
    advanced->setChecked(account.clientId().isEmpty());
    auto *advancedForm = new QFormLayout(advanced);
    auto *disclosure = new QLabel(text(index == 0 ? "GoogleDisclosure" : "TwitchDisclosure"), advanced);
    disclosure->setWordWrap(true);
    disclosure->setTextFormat(Qt::PlainText);
    advancedForm->addRow(disclosure);
    auto *client = new QLineEdit(account.clientId(), advanced);
    client->setMaxLength(256);
    advancedForm->addRow(text("OAuthClientId"), client);
    auto *registerApp = new QPushButton(text("OAuthRegister"), advanced);
    advancedForm->addRow(registerApp);
    form->addRow(advanced);
    auto collapse = [=](bool checked) {
        disclosure->setVisible(checked);
        client->setVisible(checked);
        advancedForm->labelForField(client)->setVisible(checked);
        registerApp->setVisible(checked);
    };
    QObject::connect(advanced, &QGroupBox::toggled, panel, collapse);
    collapse(advanced->isChecked());
    QObject::connect(registerApp, &QPushButton::clicked, panel, [index] {
        QDesktopServices::openUrl(QUrl(index == 0 ? "https://console.cloud.google.com/apis/credentials"
                                                  : "https://dev.twitch.tv/console/apps"));
    });
    auto *status = new QLabel(panel);
    status->setWordWrap(true);
    status->setTextFormat(Qt::PlainText);
    form->addRow(status);
    auto *feedback = new QLabel(panel);
    feedback->setWordWrap(true);
    feedback->setTextFormat(Qt::PlainText);
    auto *cancel = new QPushButton(text("CancelOAuth"), panel);
    auto *disconnect = new QPushButton(text("DisconnectAccount"), panel);
    auto *fetch = new QPushButton(text(index == 0 ? "LoadBroadcasts" : "UseAccountKey"), panel);
    auto *broadcasts = new QComboBox(panel);
    auto *select = new QPushButton(text("UseBroadcast"), panel);
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
    auto *openBrowser = new QPushButton(text("OAuthOpenBrowser"), panel);
    auto *copyCode = new QPushButton(text("OAuthCopyCode"), panel);
    openBrowser->hide();
    copyCode->hide();
    form->addRow(openBrowser);
    form->addRow(copyCode);
    auto refresh = [&, client, status, connect, cancel, disconnect, fetch, broadcasts, select, index] {
        status->setText(text(account.statusKey().toUtf8().constData()) +
                        (account.label().isEmpty() ? QString() : "\n" + account.label()));
        const bool unlocked = !manager.busy();
        client->setEnabled(unlocked && !account.linked() && !account.busy());
        connect->setEnabled(unlocked && !account.linked() && !account.busy());
        cancel->setVisible(account.busy());
        disconnect->setVisible(account.linked());
        fetch->setVisible(account.linked());
        broadcasts->setVisible(index == 0 && account.linked());
        select->setVisible(index == 0 && account.linked());
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
    QObject::connect(connect, &QPushButton::clicked, panel, [&, client, advanced, feedback] {
        if (client->text().trimmed().isEmpty()) {
            advanced->setChecked(true);
            client->setFocus();
            feedback->setText(text("OAuthSetupRequired"));
        } else if (!manager.busy())
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
                     [feedback, openBrowser, copyCode](const QUrl &url, const QString &code) {
                         openBrowser->setProperty("url", url);
                         openBrowser->show();
                         copyCode->setProperty("code", code);
                         copyCode->setVisible(!code.isEmpty());
                         feedback->setText(code.isEmpty() ? text("OAuthBrowserOpened")
                                                          : text("OAuthDeviceCode").arg(code));
                         if (!QDesktopServices::openUrl(url)) {
                             feedback->setText(text("OAuthBrowserFailed"));
                         }
                     });
    QObject::connect(openBrowser, &QPushButton::clicked, panel,
                     [openBrowser] { QDesktopServices::openUrl(openBrowser->property("url").toUrl()); });
    QObject::connect(copyCode, &QPushButton::clicked, panel, [copyCode] {
        QApplication::clipboard()->setText(copyCode->property("code").toString());
    });
    QObject::connect(&account, &OAuthAccount::changed, panel, [openBrowser, copyCode, &account] {
        if (!account.busy()) {
            openBrowser->hide();
            copyCode->hide();
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
