// SPDX-License-Identifier: GPL-2.0-or-later
#include "account-panel.hpp"
#include "locale.hpp"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <memory>

namespace sb {
QWidget *accountPanel(OutputManager &manager, size_t index, QWidget *parent) {
    auto *panel = new QWidget(parent);
    auto *form = new QFormLayout(panel);
    auto &account = manager.account(index);
    auto *connect = new QPushButton(text(index == 0 ? "ConnectYouTube" : "ConnectTwitch"), panel);
    form->addRow(connect);
    auto *guide = new QPushButton(text(index == 0 ? "YouTubeSetupGuide" : "TwitchSetupGuide"), panel);
    guide->setObjectName(index == 0 ? "youtubeSetupGuide" : "twitchSetupGuide");
    form->addRow(guide);
    QObject::connect(guide, &QPushButton::clicked, panel, [panel, index, guide] {
        auto *dialog = new QDialog(panel);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setObjectName("accountSetupGuide");
        dialog->setWindowTitle(guide->text());
        dialog->resize(760, 680);
        auto *layout = new QVBoxLayout(dialog);
        auto *body = new QTextBrowser(dialog);
        body->setOpenExternalLinks(true);
        const QString platform = index == 0 ? "youtube" : "twitch";
        const QString locale = text("SetupGuideLanguage") == "ru-RU" ? "ru-RU" : "en-US";
        QFile file(":/streambridge/help/" + platform + "-" + locale + ".md");
        if (file.open(QIODevice::ReadOnly))
            body->setMarkdown(QString::fromUtf8(file.readAll()));
        layout->addWidget(body);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
        QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
        layout->addWidget(buttons);
        dialog->open();
    });
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
    auto credentials = std::make_shared<GoogleDesktopClient>();
    auto *importClient = new QPushButton(text("OAuthImportGoogle"), advanced);
    advancedForm->addRow(importClient);
    importClient->setVisible(index == 0);
    auto *importStatus = new QLabel(advanced);
    importStatus->setWordWrap(true);
    importStatus->setTextFormat(Qt::PlainText);
    advancedForm->addRow(importStatus);
    QObject::connect(importClient, &QPushButton::clicked, panel, [=] {
        const auto filename =
            QFileDialog::getOpenFileName(panel, text("OAuthImportGoogle"), {}, "Google OAuth (*.json)");
        if (filename.isEmpty())
            return;
        QFile file(filename);
        QString error = "OAuthGoogleDesktopRequired";
        GoogleDesktopClient imported;
        if (file.open(QIODevice::ReadOnly) && file.size() <= 65536) {
            auto bytes = file.readAll();
            imported = parseGoogleDesktopClient(bytes, error);
            bytes.fill('\0');
        }
        if (!error.isEmpty()) {
            importStatus->setText(text(error.toUtf8().constData()));
            return;
        }
        credentials->secret.fill('\0');
        *credentials = std::move(imported);
        client->setText(credentials->id);
        importStatus->setText(text("OAuthGoogleImported"));
    });
    QObject::connect(client, &QLineEdit::textEdited, panel, [=] {
        credentials->secret.fill('\0');
        *credentials = {};
        importStatus->clear();
    });
    QObject::connect(panel, &QObject::destroyed, [credentials] { credentials->secret.fill('\0'); });
    auto *registerApp = new QPushButton(text("OAuthRegister"), advanced);
    advancedForm->addRow(registerApp);
    form->addRow(advanced);
    auto collapse = [=](bool checked) {
        disclosure->setVisible(checked);
        client->setVisible(checked);
        advancedForm->labelForField(client)->setVisible(checked);
        registerApp->setVisible(checked);
        importClient->setVisible(checked && index == 0);
        importStatus->setVisible(checked && index == 0);
    };
    QObject::connect(advanced, &QGroupBox::toggled, panel, collapse);
    collapse(advanced->isChecked());
    QObject::connect(registerApp, &QPushButton::clicked, panel, [index] {
        QDesktopServices::openUrl(QUrl(index == 0 ? "https://console.cloud.google.com/auth/overview"
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
    broadcasts->setObjectName(index == 0 ? "youtubeBroadcasts" : "twitchBroadcasts");
    broadcasts->setPlaceholderText(text("OAuthChooseBroadcast"));
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
    auto refresh = [&, client, importClient, status, connect, cancel, disconnect, fetch, broadcasts, select,
                    index] {
        status->setText(text(account.statusKey().toUtf8().constData()) +
                        (account.label().isEmpty() ? QString() : "\n" + account.label()));
        const bool unlocked = !manager.busy();
        client->setEnabled(unlocked && !account.linked() && !account.busy());
        importClient->setEnabled(client->isEnabled());
        connect->setEnabled(unlocked && !account.linked() && !account.busy());
        cancel->setVisible(account.busy());
        disconnect->setVisible(account.linked());
        fetch->setVisible(account.linked());
        broadcasts->setVisible(index == 0 && account.linked());
        select->setVisible(index == 0 && account.linked());
        cancel->setEnabled(account.busy());
        disconnect->setEnabled(unlocked && account.linked());
        fetch->setEnabled((unlocked || index == 0) && account.linked() && !account.busy());
        if (index == 0 && !account.busy()) {
            const auto chosen = broadcasts->currentData().toString();
            broadcasts->clear();
            for (const auto &b : account.broadcasts())
                broadcasts->addItem(b.title, b.id);
            const auto saved = account.broadcastId();
            if (!saved.isEmpty() && broadcasts->findData(saved) < 0)
                broadcasts->addItem(account.broadcastTitle().isEmpty() ? saved : account.broadcastTitle(),
                                    saved);
            const auto desired =
                chosen.isEmpty() || broadcasts->property("savedBroadcast").toString() != saved ? saved
                                                                                               : chosen;
            broadcasts->setProperty("savedBroadcast", saved);
            broadcasts->setCurrentIndex(broadcasts->findData(desired));
            broadcasts->setToolTip(
                saved.isEmpty()
                    ? text("PlatformSelectBroadcast")
                    : text("SelectedBroadcast")
                          .arg(account.broadcastTitle().isEmpty() ? saved : account.broadcastTitle()));
        }
        select->setEnabled((unlocked || index == 0) && !account.busy() && broadcasts->currentIndex() >= 0);
    };
    QObject::connect(&account, &OAuthAccount::changed, panel, refresh);
    QObject::connect(&manager, &OutputManager::changed, panel, refresh);
    QObject::connect(broadcasts, &QComboBox::activated, panel,
                     [select, &account](int index) { select->setEnabled(index >= 0 && !account.busy()); });
    QObject::connect(connect, &QPushButton::clicked, panel, [&, client, advanced, feedback, credentials] {
        if (client->text().trimmed().isEmpty()) {
            advanced->setChecked(true);
            client->setFocus();
            feedback->setText(text("OAuthSetupRequired"));
        } else if (!manager.busy())
            account.link(client->text(),
                         credentials->id == client->text().trimmed() ? credentials->secret : QByteArray());
    });
    QObject::connect(cancel, &QPushButton::clicked, panel, [&account, refresh] {
        account.cancelPending();
        refresh();
    });
    QObject::connect(disconnect, &QPushButton::clicked, panel, [&] {
        if (!manager.busy())
            account.disconnectAccount();
    });
    QObject::connect(fetch, &QPushButton::clicked, panel, [&, index] {
        if (!manager.busy() || index == 0)
            account.fetchIngest();
    });
    QObject::connect(select, &QPushButton::clicked, panel, [&, broadcasts, feedback] {
        if (!manager.busy()) {
            account.selectBroadcast(broadcasts->currentData().toString());
        } else {
            for (const auto &broadcast : account.broadcasts())
                if (broadcast.id == broadcasts->currentData().toString()) {
                    feedback->setText(account.rememberBroadcast(broadcast) ? text("YouTubeMonitoringSaved")
                                                                           : text("OAuthVaultFailed"));
                    break;
                }
        }
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
    QObject::connect(&account, &OAuthAccount::changed, panel, [openBrowser, copyCode, feedback, &account] {
        if (!account.busy()) {
            openBrowser->hide();
            copyCode->hide();
            feedback->clear();
        }
    });
    QObject::connect(&account, &OAuthAccount::ingestReady, panel,
                     [&, index, feedback](const QString &server, QByteArray key) {
                         QString error;
                         // Selecting an event must not replace a manually chosen ingest route.
                         const auto selectedServer =
                             index == 0 ? manager.config().endpoints[0].server : server;
                         feedback->setText(manager.applyIngest(index, selectedServer, key, error)
                                               ? text("OAuthIngestSaved")
                                               : error);
                         key.fill('\0');
                     });
    // Closing Settings cancels an unfinished login; stored sessions remain usable.
    QObject::connect(panel, &QObject::destroyed, &account, [&account] {
        if (account.busy())
            account.cancelPending();
    });
    refresh();
    return panel;
}
} // namespace sb
