/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "settings_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmapCache>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QStyleHints>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

#include "base/string.hpp"
#include "gui/main/main_window.hpp"
#include "gui/utils/theme.hpp"
#include "sync/anilist/anilist_utils.hpp"
#include "sync/service.hpp"
#include "taiga/accounts.hpp"
#include "taiga/path.hpp"
#include "taiga/settings.hpp"
#include "track/media.hpp"
#include "track/media_player.hpp"
#include "track/recognition_cache.hpp"
#include "track/sharing.hpp"
#include "ui_settings_dialog.h"

#ifdef Q_OS_WINDOWS
#include "gui/platforms/windows.hpp"
#endif

namespace gui {

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent), ui_(new Ui::SettingsDialog) {
  ui_->setupUi(this);

#ifdef Q_OS_WINDOWS
  enableMicaBackground(this);
#endif

  ui_->treeWidget->setIndentation(22);

  const auto add_item = [this](QString icon, QString text) {
    auto item = new QTreeWidgetItem(ui_->treeWidget, QStringList(text));
    item->setIcon(0, theme.getIcon(icon));
    item->setSizeHint(0, QSize{0, 24});
    return item;
  };

  const auto add_child = [this](QTreeWidgetItem* parent, QString text) {
    new QTreeWidgetItem(parent, QStringList(text));
  };

  add_item("account_circle", "Accounts");
  add_item("web_asset", "Application");
  add_item("share", "Sharing");
  add_item("list_alt", "Anime List");
  add_item("folder", "Library");
  {
    auto item = add_item("check_circle", "Recognition");
    add_child(item, "Media players");
  }
  {
    auto item = add_item("rss_feed", "Torrents");
    add_child(item, "Downloads");
    add_child(item, "Filters");
  }
  {
    auto item = add_item("warning", "Advanced");
    add_child(item, "Cache");
  }

  auto accountsLayout = new QVBoxLayout(ui_->accountsPage);
  accountsLayout->setContentsMargins(0, 0, 0, 0);

  auto accountsDescription = new QLabel(
      tr("Choose the service used for synchronization and manage its sign-in."), ui_->accountsPage);
  accountsDescription->setWordWrap(true);
  accountsLayout->addWidget(accountsDescription);

  auto accountForm = new QFormLayout;
  auto serviceBox = new QComboBox(ui_->accountsPage);
  serviceBox->addItem(sync::serviceName(sync::ServiceId::AniList), "anilist");
  serviceBox->addItem(sync::serviceName(sync::ServiceId::Kitsu), "kitsu");
  serviceBox->addItem(sync::serviceName(sync::ServiceId::MyAnimeList), "myanimelist");
  const auto currentService = QString::fromStdString(taiga::settings.service());
  const auto currentIndex = serviceBox->findData(currentService);
  if (currentIndex >= 0) serviceBox->setCurrentIndex(currentIndex);
  accountForm->addRow(tr("Service:"), serviceBox);

  auto username = new QLabel(ui_->accountsPage);
  auto status = new QLabel(ui_->accountsPage);
  accountForm->addRow(tr("Account:"), username);
  accountForm->addRow(tr("Status:"), status);
  accountsLayout->addLayout(accountForm);

  auto accountActions = new QHBoxLayout;
  auto authenticate = new QPushButton(tr("Authenticate"), ui_->accountsPage);
  auto synchronize = new QPushButton(tr("Synchronize"), ui_->accountsPage);
  accountActions->addWidget(authenticate);
  accountActions->addWidget(synchronize);
  accountActions->addStretch();
  accountsLayout->addLayout(accountActions);
  accountsLayout->addStretch();

  const auto refreshAccount = [serviceBox, username, status] {
    const auto service = sync::serviceIdFromSlug(serviceBox->currentData().toString());
    const auto serviceSlug = sync::serviceSlug(service).toStdString();
    const auto name = taiga::accounts.serviceUsername(serviceSlug);
    username->setText(name.empty() ? QObject::tr("Not configured") : QString::fromStdString(name));
    status->setText(sync::isUserAuthenticated() ? QObject::tr("Authenticated")
                                                : QObject::tr("Not authenticated"));
  };
  refreshAccount();

  connect(serviceBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
          [serviceBox, refreshAccount](const int) {
            taiga::settings.setService(serviceBox->currentData().toString().toStdString());
            refreshAccount();
          });
  connect(this, &QDialog::finished, this, [currentService](const int result) {
    if (result != QDialog::Accepted) {
      taiga::settings.setService(currentService.toStdString());
      if (auto* window = mainWindow()) {
        window->refreshPage(MainWindowPage::Home);
        window->refreshPage(MainWindowPage::Profile);
      }
    }
  });
  const auto signIn = [this, serviceBox, authenticate, refreshAccount](bool replaceToken) {
    const auto service = sync::serviceIdFromSlug(serviceBox->currentData().toString());
    if (service != sync::ServiceId::AniList) {
      sync::authenticateUser();
      return;
    }
    authenticate->setEnabled(false);
    QPointer<SettingsDialog> guard(this);
    taiga::accounts.loadAnilistToken([guard, serviceBox, authenticate, refreshAccount,
                                      replaceToken] {
      if (!guard) return;
      authenticate->setEnabled(true);
      if (serviceBox->currentData().toString() != "anilist") return;
      if (replaceToken || taiga::accounts.anilistToken().empty()) {
        QDesktopServices::openUrl(QUrl{QString::fromStdString(sync::anilist::requestTokenUrl())});
        bool ok = false;
        const auto token =
            QInputDialog::getText(guard, tr("AniList authorization"),
                                  tr("Paste the access token shown after logging in to AniList:"),
                                  QLineEdit::Password, {}, &ok)
                .trimmed();
        if (!guard || !ok || token.isEmpty()) return;
        authenticate->setEnabled(false);
        taiga::accounts.storeAnilistToken(token.toStdString(),
                                          [guard, serviceBox, authenticate, refreshAccount](bool) {
                                            if (!guard) return;
                                            authenticate->setEnabled(true);
                                            if (serviceBox->currentData().toString() != "anilist")
                                              return;
                                            sync::authenticateUser();
                                            refreshAccount();
                                          });
        return;
      }
      sync::authenticateUser();
      refreshAccount();
    });
  };
  connect(authenticate, &QPushButton::clicked, this, [signIn] { signIn(false); });
  auto replaceToken = new QPushButton(tr("Replace AniList token..."), ui_->accountsPage);
  accountActions->insertWidget(2, replaceToken);
  replaceToken->setVisible(serviceBox->currentData().toString() == "anilist");
  connect(serviceBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
          [serviceBox, replaceToken](int) {
            replaceToken->setVisible(serviceBox->currentData().toString() == "anilist");
          });
  connect(replaceToken, &QPushButton::clicked, this, [signIn] { signIn(true); });
  connect(synchronize, &QPushButton::clicked, this, [refreshAccount] {
    sync::synchronize();
    refreshAccount();
  });
  connect(&taiga::accounts, &taiga::Accounts::authenticationChanged, this,
          [refreshAccount](const bool) { refreshAccount(); });

  ui_->treeWidget->expandAll();

  auto torrentsPage = new QWidget(this);
  auto torrentsLayout = new QVBoxLayout(torrentsPage);
  auto description =
      new QLabel(tr("Configure RSS sources, torrent file downloads, filters and refresh interval."),
                 torrentsPage);
  description->setWordWrap(true);
  torrentsLayout->addWidget(description);
  auto configure = new QPushButton(tr("Configure torrents..."), torrentsPage);
  torrentsLayout->addWidget(configure, 0, Qt::AlignLeft);
  torrentsLayout->addStretch();
  ui_->stackedWidget->addWidget(torrentsPage);

  QHash<QString, QWidget*> pages;
  pages.insert("Accounts", ui_->accountsPage);
  pages.insert("Torrents", torrentsPage);
  pages.insert("Downloads", torrentsPage);
  pages.insert("Filters", torrentsPage);
  const auto page = [this, &pages](const QString& name, const QString& description) {
    auto widget = new QWidget(ui_->stackedWidget);
    auto layout = new QVBoxLayout(widget);
    auto label = new QLabel(description, widget);
    label->setWordWrap(true);
    layout->addWidget(label);
    ui_->stackedWidget->addWidget(widget);
    pages.insert(name, widget);
    return layout;
  };

  auto application = page("Application", tr("Choose the appearance of the application."));
  auto colors = new QComboBox(this);
  colors->addItem(tr("System default"), static_cast<int>(Qt::ColorScheme::Unknown));
  colors->addItem(tr("Light"), static_cast<int>(Qt::ColorScheme::Light));
  colors->addItem(tr("Dark"), static_cast<int>(Qt::ColorScheme::Dark));
  colors->setCurrentIndex(colors->findData(static_cast<int>(taiga::settings.appColorScheme())));
  auto appearance = new QFormLayout;
  appearance->addRow(tr("Color scheme:"), colors);
  application->addLayout(appearance);
  application->addStretch();

  auto sharing =
      page("Sharing", tr("Announce detected episodes to optional desktop and network integrations. "
                         "All integrations are disabled individually by default."));
  auto sharingEnabled = new QCheckBox(tr("Enable sharing"), this);
  sharingEnabled->setChecked(taiga::settings.sharingEnabled());
  sharing->addWidget(sharingEnabled);

  auto discordEnabled = new QCheckBox(tr("Enable Discord Rich Presence"), this);
  discordEnabled->setChecked(taiga::settings.discordSharingEnabled());
  auto discordId =
      new QLineEdit(QString::fromStdString(taiga::settings.discordApplicationId()), this);
  auto discordForm = new QFormLayout;
  discordForm->addRow(discordEnabled);
  discordForm->addRow(tr("Application ID:"), discordId);
  sharing->addLayout(discordForm);

  auto httpEnabled = new QCheckBox(tr("Enable HTTP announcements"), this);
  httpEnabled->setChecked(taiga::settings.httpSharingEnabled());
  auto httpUrl = new QLineEdit(QString::fromStdString(taiga::settings.httpSharingUrl()), this);
  httpUrl->setPlaceholderText(tr("https://example.test/taiga"));
  auto httpFormat =
      new QLineEdit(QString::fromStdString(taiga::settings.httpSharingFormat()), this);
  httpFormat->setPlaceholderText(tr("%title% - Episode %episode%"));
  auto httpForm = new QFormLayout;
  httpForm->addRow(httpEnabled);
  httpForm->addRow(tr("POST URL:"), httpUrl);
  httpForm->addRow(tr("Message format:"), httpFormat);
  sharing->addLayout(httpForm);

  auto ircEnabled = new QCheckBox(tr("Enable IRC announcements"), this);
  ircEnabled->setChecked(taiga::settings.ircSharingEnabled());
  auto ircServer = new QLineEdit(QString::fromStdString(taiga::settings.ircServer()), this);
  ircServer->setPlaceholderText(tr("irc.example.test"));
  auto ircPort = new QSpinBox(this);
  ircPort->setRange(1, 65535);
  ircPort->setValue(taiga::settings.ircPort());
  auto ircNickname = new QLineEdit(QString::fromStdString(taiga::settings.ircNickname()), this);
  auto ircChannel = new QLineEdit(QString::fromStdString(taiga::settings.ircChannel()), this);
  auto ircFormat = new QLineEdit(QString::fromStdString(taiga::settings.ircFormat()), this);
  auto ircAction = new QCheckBox(tr("Send as /me action"), this);
  ircAction->setChecked(taiga::settings.ircUseAction());
  auto ircForm = new QFormLayout;
  ircForm->addRow(ircEnabled);
  ircForm->addRow(tr("Server:"), ircServer);
  ircForm->addRow(tr("Port:"), ircPort);
  ircForm->addRow(tr("Nickname:"), ircNickname);
  ircForm->addRow(tr("Channel:"), ircChannel);
  ircForm->addRow(tr("Message format:"), ircFormat);
  ircForm->addRow(ircAction);
  sharing->addLayout(ircForm);
  sharing->addWidget(new QLabel(
      tr("Supported placeholders: %title%, %episode%, %total%, and %url%. IRC uses a standard "
         "connection; the Windows-only mIRC DDE protocol is not used on Linux."),
      this));
  sharing->addStretch();

  auto animeList = page("Anime List", tr("Choose the title language used in your anime list. "
                                         "Reopen the application to refresh existing views."));
  auto language = new QComboBox(this);
  language->addItem(tr("Romaji"), static_cast<int>(anime::TitleLanguage::Romaji));
  language->addItem(tr("English"), static_cast<int>(anime::TitleLanguage::English));
  language->addItem(tr("Native"), static_cast<int>(anime::TitleLanguage::Native));
  language->setCurrentIndex(language->findData(static_cast<int>(taiga::settings.titleLanguage())));
  auto titles = new QFormLayout;
  titles->addRow(tr("Title language:"), language);
  animeList->addLayout(titles);
  auto synchronization =
      new QCheckBox(tr("Enable synchronization with the selected service"), this);
  synchronization->setChecked(taiga::settings.syncEnabled());
  animeList->addWidget(synchronization);
  animeList->addStretch();

  auto library =
      page("Library",
           tr("Folders searched for local episodes. Changes apply immediately after saving."));
  auto folders = new QListWidget(this);
  for (const auto& folder : taiga::settings.libraryFolders())
    folders->addItem(QString::fromStdString(folder));
  library->addWidget(folders);
  auto folderActions = new QHBoxLayout;
  auto addFolder = new QPushButton(tr("Add folder..."), this);
  auto removeFolder = new QPushButton(tr("Remove selected"), this);
  folderActions->addWidget(addFolder);
  folderActions->addWidget(removeFolder);
  folderActions->addStretch();
  library->addLayout(folderActions);
  connect(addFolder, &QPushButton::clicked, this, [this, folders] {
    const auto folder = QFileDialog::getExistingDirectory(this, tr("Library folder"));
    if (!folder.isEmpty() && folders->findItems(folder, Qt::MatchExactly).isEmpty())
      folders->addItem(QDir::cleanPath(folder));
  });
  connect(removeFolder, &QPushButton::clicked, this,
          [folders] { delete folders->takeItem(folders->currentRow()); });

  auto recognition = page("Recognition", tr("Detect episodes from media players. On Linux, "
                                            "players must expose an MPRIS interface; local files "
                                            "and supported streaming pages can be recognized."));
  auto interval = new QSpinBox(this);
  interval->setRange(250, 60000);
  interval->setSingleStep(250);
  interval->setSuffix(tr(" ms"));
  interval->setValue(static_cast<int>(taiga::settings.mediaDetectionInterval().count()));
  auto detectionForm = new QFormLayout;
  detectionForm->addRow(tr("Check interval:"), interval);
  recognition->addLayout(detectionForm);
  recognition->addStretch();

  auto playersLayout =
      page("Media players",
           tr("Uncheck players to exclude them from recognition. "
              "For an unlisted MPRIS player, add its identity or service name to the exclusions."));
  auto playersList = new QListWidget(this);
  playersLayout->addWidget(playersList);
  playersLayout->addWidget(new QLabel(tr("Browsers and streaming pages:"), this));
  auto browsersList = new QListWidget(this);
  playersLayout->addWidget(browsersList);
  const auto disabled = taiga::settings.disabledMediaPlayers();
  std::vector<track::media::Player> players;
  track::media::parsePlayersData(players);
  for (const auto& player : players) {
    auto list = player.type == anisthesia::PlayerType::WebBrowser ? browsersList : playersList;
    const auto name = QString::fromStdString(player.name);
    auto item = new QListWidgetItem(name, list);
    const bool excluded = std::ranges::any_of(disabled, [&name](const auto& value) {
      return name.compare(QString::fromStdString(value), Qt::CaseInsensitive) == 0;
    });
    item->setCheckState(excluded ? Qt::Unchecked : Qt::Checked);
  }
  auto exclusions = new QListWidget(this);
  for (const auto& name : disabled) {
    if (!std::ranges::any_of(players, [&name](const auto& player) {
          return QString::fromStdString(name).compare(QString::fromStdString(player.name),
                                                      Qt::CaseInsensitive) == 0;
        }))
      exclusions->addItem(QString::fromStdString(name));
  }
  playersLayout->addWidget(new QLabel(tr("Additional exclusions:"), this));
  playersLayout->addWidget(exclusions);
  auto exclusionsActions = new QHBoxLayout;
  auto addExclusion = new QPushButton(tr("Exclude player..."), this);
  auto removeExclusion = new QPushButton(tr("Remove exclusion"), this);
  exclusionsActions->addWidget(addExclusion);
  exclusionsActions->addWidget(removeExclusion);
  playersLayout->addLayout(exclusionsActions);
  connect(addExclusion, &QPushButton::clicked, this, [this, exclusions] {
    const auto name =
        QInputDialog::getText(this, tr("Exclude player"), tr("MPRIS identity or service name:"))
            .trimmed();
    if (!name.isEmpty() && exclusions->findItems(name, Qt::MatchFixedString).isEmpty())
      exclusions->addItem(name);
  });
  connect(removeExclusion, &QPushButton::clicked, this,
          [exclusions] { delete exclusions->takeItem(exclusions->currentRow()); });

  auto advanced =
      page("Advanced",
           tr("Configure an HTTP proxy. Leave the address empty to use "
              "the system default. Proxy changes take effect after restarting the application."));
  auto proxy = new QLineEdit(QString::fromStdString(taiga::settings.proxyHost()), this);
  proxy->setPlaceholderText(tr("http://host:8080"));
  auto proxyUser = new QLineEdit(QString::fromStdString(taiga::settings.proxyUsername()), this);
  auto proxyPassword = new QLineEdit(QString::fromStdString(taiga::settings.proxyPassword()), this);
  proxyPassword->setEchoMode(QLineEdit::Password);
  auto proxyForm = new QFormLayout;
  proxyForm->addRow(tr("Proxy address:"), proxy);
  proxyForm->addRow(tr("Username:"), proxyUser);
  proxyForm->addRow(tr("Password:"), proxyPassword);
  advanced->addLayout(proxyForm);
  advanced->addStretch();

  auto cache = page("Cache", tr("Rebuild the recognition index and release cached images from "
                                "memory. These actions run immediately."));
  auto rebuild = new QPushButton(tr("Rebuild recognition index"), this);
  auto clearImages = new QPushButton(tr("Clear images from memory"), this);
  auto openCache = new QPushButton(tr("Open image cache folder"), this);
  auto cacheStatus = new QLabel(this);
  cacheStatus->setWordWrap(true);
  cache->addWidget(rebuild);
  cache->addWidget(clearImages);
  cache->addWidget(openCache);
  cache->addWidget(cacheStatus);
  cache->addStretch();
  connect(rebuild, &QPushButton::clicked, this, [cacheStatus] {
    track::recognition::cache()->clear();
    track::recognition::cache()->init();
    cacheStatus->setText(tr("Recognition index rebuilt."));
  });
  connect(clearImages, &QPushButton::clicked, this, [cacheStatus] {
    QPixmapCache::clear();
    cacheStatus->setText(tr("Images released from memory."));
  });
  connect(openCache, &QPushButton::clicked, this, [cacheStatus] {
    const auto path = QDir(QString::fromStdString(taiga::get_data_path())).filePath("cache");
    if (!QDir().mkpath(path) || !QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
      cacheStatus->setText(tr("Could not open the cache folder."));
  });

  // Validate before accepting: Cancel never applies edited settings.
  disconnect(ui_->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(ui_->buttonBox, &QDialogButtonBox::accepted, this, [this, proxy, httpEnabled, httpUrl] {
    auto address = proxy->text().trimmed();
    if (!address.isEmpty()) {
      if (!address.contains("://")) address.prepend("http://");
      const QUrl url(address);
      if (!url.isValid() || url.scheme() != "http" || url.host().isEmpty() || url.port(8080) < 1 ||
          !url.userInfo().isEmpty() || (!url.path().isEmpty() && url.path() != "/") ||
          url.hasQuery() || url.hasFragment()) {
        QMessageBox::warning(this, tr("Invalid proxy"),
                             tr("Enter an HTTP proxy as host:port or http://host:port."));
        return;
      }
    }
    if (httpEnabled->isChecked()) {
      const QUrl url{httpUrl->text().trimmed()};
      if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https") ||
          url.host().isEmpty()) {
        QMessageBox::warning(this, tr("Invalid HTTP sharing URL"),
                             tr("Enter an HTTP or HTTPS URL before enabling HTTP sharing."));
        return;
      }
    }
    accept();
  });
  connect(this, &QDialog::accepted, this, [=] {
    taiga::settings.setAppColorScheme(static_cast<Qt::ColorScheme>(colors->currentData().toInt()));
    qApp->styleHints()->setColorScheme(taiga::settings.appColorScheme());
    taiga::settings.setTitleLanguage(
        static_cast<anime::TitleLanguage>(language->currentData().toInt()));
    taiga::settings.setSyncEnabled(synchronization->isChecked());
    taiga::settings.setSharingEnabled(sharingEnabled->isChecked());
    taiga::settings.setDiscordSharingEnabled(discordEnabled->isChecked());
    taiga::settings.setDiscordApplicationId(discordId->text().trimmed().toStdString());
    taiga::settings.setHttpSharingEnabled(httpEnabled->isChecked());
    taiga::settings.setHttpSharingUrl(httpUrl->text().trimmed().toStdString());
    taiga::settings.setHttpSharingFormat(httpFormat->text().toStdString());
    taiga::settings.setIrcSharingEnabled(ircEnabled->isChecked());
    taiga::settings.setIrcServer(ircServer->text().trimmed().toStdString());
    taiga::settings.setIrcPort(ircPort->value());
    taiga::settings.setIrcNickname(ircNickname->text().trimmed().toStdString());
    taiga::settings.setIrcChannel(ircChannel->text().trimmed().toStdString());
    taiga::settings.setIrcUseAction(ircAction->isChecked());
    taiga::settings.setIrcFormat(ircFormat->text().toStdString());
    std::vector<std::string> paths;
    for (int i = 0; i < folders->count(); ++i)
      paths.push_back(folders->item(i)->text().toStdString());
    taiga::settings.setLibraryFolders(std::move(paths));
    taiga::settings.setMediaDetectionInterval(std::chrono::milliseconds(interval->value()));
    std::vector<std::string> excluded;
    for (auto list : {playersList, browsersList}) {
      for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->checkState() == Qt::Unchecked)
          excluded.push_back(list->item(i)->text().toStdString());
    }
    for (int i = 0; i < exclusions->count(); ++i)
      excluded.push_back(exclusions->item(i)->text().toStdString());
    taiga::settings.setDisabledMediaPlayers(std::move(excluded));
    taiga::settings.setProxyHost(proxy->text().trimmed().toStdString());
    taiga::settings.setProxyUsername(proxyUser->text().toStdString());
    taiga::settings.setProxyPassword(proxyPassword->text().toStdString());
    track::media::detection()->init();
    if (auto* window = mainWindow()) {
      if (!taiga::settings.sharingEnabled() || !taiga::settings.discordSharingEnabled()) {
        track::sharing::clear();
      } else if (const auto episode = track::media::detection()->getCurrentEpisode()) {
        track::sharing::update(*episode);
      }
      window->refreshLibrary();
      window->refreshPage(MainWindowPage::Home);
      window->refreshPage(MainWindowPage::Profile);
    }
  });

  connect(configure, &QPushButton::clicked, this, [this] {
    hide();
    mainWindow()->configureTorrents();
    QDialog::show();
  });

  connect(ui_->treeWidget, &QTreeWidget::currentItemChanged, this,
          [this, pages](QTreeWidgetItem* current, QTreeWidgetItem*) {
            if (current) {
              ui_->stackedWidget->setCurrentWidget(pages.value(current->text(0)));
              auto text = current->text(0);
              if (current->parent()) {
                text = u"%1 / %2"_s.arg(current->parent()->text(0), text);
              }
              ui_->titleLabel->setText(text);
            }
          });
  ui_->treeWidget->setCurrentItem(ui_->treeWidget->topLevelItem(0));
}

SettingsDialog::~SettingsDialog() {
  delete ui_;
}

void SettingsDialog::show(QWidget* parent) {
  auto dlg = new SettingsDialog(parent);
  dlg->setAttribute(Qt::WA_DeleteOnClose);
  dlg->setModal(true);
  dlg->QDialog::show();
}

}  // namespace gui
