/**
 * Taiga
 * Copyright (C) 2010-2026, Eren Okka
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

#include "main_window.hpp"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QPointer>
#include <QRandomGenerator>
#include <QtWidgets>
#include <algorithm>
#include <optional>
#include <vector>

#include "base/string.hpp"
#include "gui/common/spinner_widget.hpp"
#include "gui/history/history_widget.hpp"
#include "gui/library/library_widget.hpp"
#include "gui/list/list_widget.hpp"
#include "gui/main/about_dialog.hpp"
#include "gui/main/navigation_widget.hpp"
#include "gui/main/now_playing_widget.hpp"
#include "gui/main/status_bar.hpp"
#include "gui/main/status_bar_controller.hpp"
#include "gui/search/search_widget.hpp"
#include "gui/settings/settings_dialog.hpp"
#include "gui/torrents/torrents_widget.hpp"
#include "gui/utils/format.hpp"
#include "gui/utils/theme.hpp"
#include "gui/utils/tray_icon.hpp"
#include "gui/utils/widgets.hpp"
#include "media/anime_db.hpp"
#include "media/anime_list.hpp"
#include "media/anime_list_export.hpp"
#include "media/anime_utils.hpp"
#include "sync/anilist/anilist.hpp"
#include "sync/kitsu/kitsu.hpp"
#include "sync/myanimelist/myanimelist.hpp"
#include "sync/queue.hpp"
#include "sync/service.hpp"
#include "taiga/accounts.hpp"
#include "taiga/application.hpp"
#include "taiga/session.hpp"
#include "taiga/settings.hpp"
#include "track/media.hpp"
#include "track/play.hpp"
#include "track/scanner.hpp"
#include "track/sharing.hpp"
#include "ui_main_window.h"

#ifdef Q_OS_WINDOWS
#include "gui/platforms/windows.hpp"
#endif

namespace gui {

namespace {

template <typename ExportFunction>
void exportList(MainWindow* window, const QString& extension, const QString& filter,
                ExportFunction function) {
  const auto timestamp = QDateTime::currentDateTime().toSecsSinceEpoch();
  const auto suggested = u"animelist_%1.%2"_s.arg(timestamp).arg(extension);
  auto path =
      QFileDialog::getSaveFileName(window, QObject::tr("Export anime list"), suggested, filter);
  if (path.isEmpty()) return;

  if (QFileInfo{path}.suffix().isEmpty()) path += u".%1"_s.arg(extension);

  const auto success = function(path.toStdString());
  window->statusBarController()->showMessage({
      .source = StatusBarController::Source::Export,
      .text = success ? QObject::tr("Exported list to %1.").arg(path)
                      : QObject::tr("Could not export list to %1.").arg(path),
      .spin = false,
  });
}

std::optional<int> selectedAnimeId(const MainWindow* window) {
  if (window->ui()->stackedWidget->currentWidget() == window->ui()->libraryPage) {
    if (const auto* library = window->findChild<LibraryWidget*>()) {
      if (const auto id = library->currentAnimeId()) return id;
    }
  }

  if (const auto* list = window->findChild<ListWidget*>()) return list->currentAnimeId();
  return std::nullopt;
}

}  // namespace

MainWindow::MainWindow() : QMainWindow(), ui_(new Ui::MainWindow) {
  ui_->setupUi(this);

  ui_->menubar->hide();

#ifdef Q_OS_WINDOWS
  enableMicaBackground(this);
#endif

  if (const auto geometry = taiga::session.mainWindowGeometry(); !geometry.isEmpty()) {
    restoreGeometry(geometry);
    centerWidgetToScreen(this);
  }

  // Do not call `init()` here, as it relies on the main window pointer being
  // available through the application instance.
}

MainWindow* mainWindow() {
  return taiga::app()->mainWindow();
}

NavigationWidget* MainWindow::navigation() const {
  return m_navigationWidget;
}

NowPlayingWidget* MainWindow::nowPlaying() const {
  return m_nowPlayingWidget;
}

QLineEdit* MainWindow::searchBox() const {
  return m_searchBox;
}

StatusBarController* MainWindow::statusBarController() const {
  return m_statusBarController;
}

Ui::MainWindow* MainWindow::ui() const {
  return ui_;
}

void MainWindow::init() {
  initActions();
  initIcons();
  initTrayIcon();
  initToolbar();
  initStatusbar();
  initNavigation();
  initNowPlaying();
  updateTitle();
}

void MainWindow::initActions() {
  ui_->actionProfile->setToolTip(tr("Profile"));
  ui_->actionSynchronize->setToolTip(
      tr("Synchronize with %1").arg(sync_service::serviceName(sync_service::currentServiceId())));

  connect(ui_->actionAddNewFolder, &QAction::triggered, this, &MainWindow::addNewFolder);
  connect(ui_->actionExit, &QAction::triggered, this, &QApplication::quit, Qt::QueuedConnection);
  connect(ui_->actionSettings, &QAction::triggered, this, [this]() { SettingsDialog::show(this); });
  connect(ui_->actionAbout, &QAction::triggered, this, &MainWindow::about);
  connect(ui_->actionDonate, &QAction::triggered, this, &MainWindow::donate);
  connect(ui_->actionSupport, &QAction::triggered, this, &MainWindow::support);
  connect(ui_->actionProfile, &QAction::triggered, this, &MainWindow::profile);
  connect(ui_->actionDisplayWindow, &QAction::triggered, this, &MainWindow::displayWindow);
  connect(ui_->actionSynchronize, &QAction::triggered, this, &MainWindow::synchronize);
  connect(ui_->actionScanAvailableEpisodes, &QAction::triggered, this,
          &MainWindow::scanAvailableEpisodes);
  connect(ui_->actionPlayNextEpisode, &QAction::triggered, this, &MainWindow::playNextEpisode);
  connect(ui_->actionPlayRandomAnime, &QAction::triggered, this, &MainWindow::playRandomAnime);
  connect(ui_->actionExportListAsMarkdown, &QAction::triggered, this,
          &MainWindow::exportListAsMarkdown);
  connect(ui_->actionExportListAsMyAnimeListXML, &QAction::triggered, this,
          &MainWindow::exportListAsMyAnimeListXml);

  ui_->actionToggleDetection->setChecked(taiga::settings.detectionEnabled());
  connect(ui_->actionToggleDetection, &QAction::toggled, this, [](const bool checked) {
    taiga::settings.setDetectionEnabled(checked);
    track::media::detection()->setEnabled(checked);
  });

  ui_->actionToggleSharing->setChecked(taiga::settings.sharingEnabled());
  connect(ui_->actionToggleSharing, &QAction::toggled, this, [](const bool checked) {
    taiga::settings.setSharingEnabled(checked);
    if (!checked) {
      track::sharing::clear();
    } else if (const auto episode = track::media::detection()->getCurrentEpisode()) {
      track::sharing::update(*episode);
    }
  });

  ui_->actionToggleSynchronization->setChecked(taiga::settings.syncEnabled());
  connect(ui_->actionToggleSynchronization, &QAction::toggled, this,
          [](const bool checked) { taiga::settings.setSyncEnabled(checked); });

  ui_->actionToggleStatusbar->setChecked(ui_->statusbar->isVisible());
  connect(ui_->actionToggleStatusbar, &QAction::toggled, this,
          [this](const bool checked) { statusBar()->setVisible(checked); });

  connect(ui_->actionToggleNowPlaying, &QAction::toggled, this, [this](const bool checked) {
    if (m_nowPlayingWidget) m_nowPlayingWidget->setDisplayEnabled(checked);
  });
}

void MainWindow::initIcons() {
  ui_->menuLibraryFolders->setIcon(theme.getIcon("folder"));
  ui_->menuExport->setIcon(theme.getIcon("export_notes"));

  ui_->actionAddNewFolder->setIcon(theme.getIcon("create_new_folder"));
  ui_->actionAbout->setIcon(theme.getIcon("info"));
  ui_->actionBack->setIcon(theme.getIcon("arrow_back"));
  ui_->actionCheckForUpdates->setIcon(theme.getIcon("cloud_download"));
  ui_->actionDonate->setIcon(theme.getIcon("favorite"));
  ui_->actionExit->setIcon(theme.getIcon("logout"));
  ui_->actionForward->setIcon(theme.getIcon("arrow_forward"));
  ui_->actionLibraryFolders->setIcon(theme.getIcon("folder"));
  ui_->actionMenu->setIcon(theme.getIcon("menu"));
  ui_->actionPlayNextEpisode->setIcon(theme.getIcon("skip_next"));
  ui_->actionPlayRandomAnime->setIcon(theme.getIcon("shuffle"));
  ui_->actionProfile->setIcon(theme.getIcon("account_circle"));
  ui_->actionScanAvailableEpisodes->setIcon(theme.getIcon("pageview"));
  ui_->actionSettings->setIcon(theme.getIcon("settings"));
  ui_->actionSupport->setIcon(theme.getIcon("help"));
  ui_->actionSynchronize->setIcon(theme.getIcon("sync"));
}

void MainWindow::initNavigation() {
  m_navigationWidget = new NavigationWidget(this);

  connect(m_navigationWidget, &NavigationWidget::currentPageChanged, this, &MainWindow::setPage);

  const bool hasWatching = std::ranges::any_of(anime::db.entries(), [](const auto& entry) {
    return entry.status == anime::list::Status::Watching;
  });
  if (hasWatching) {
    navigateToListStatus(anime::list::Status::Watching);
  } else {
    navigateTo(MainWindowPage::List);
  }

  ui_->splitter->insertWidget(0, m_navigationWidget);
}

void MainWindow::initNowPlaying() {
  m_nowPlayingWidget = new NowPlayingWidget(ui_->centralWidget);

  ui_->centralWidget->layout()->addWidget(m_nowPlayingWidget);
  m_nowPlayingWidget->setDisplayEnabled(ui_->actionToggleNowPlaying->isChecked());
}

void MainWindow::initPage(MainWindowPage page) {
  if (initializedPages_.contains(page)) return;

  static const auto init_page = [](QWidget* page, QWidget* widget) {
    const auto layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(widget);
  };

  switch (page) {
    case MainWindowPage::Home: {
      auto home = new QWidget(ui_->homePage);
      auto layout = new QVBoxLayout(home);
      layout->setAlignment(Qt::AlignCenter);

      auto title = new QLabel(tr("Welcome to Taiga"), home);
      auto titleFont = title->font();
      titleFont.setPointSize(titleFont.pointSize() + 4);
      titleFont.setBold(true);
      title->setFont(titleFont);
      title->setAlignment(Qt::AlignCenter);
      layout->addWidget(title);

      auto summary = new QLabel(home);
      summary->setAlignment(Qt::AlignCenter);
      layout->addWidget(summary);
      m_homeSummary = summary;
      updateHomePage();

      auto actions = new QHBoxLayout;
      auto listButton = new QPushButton(tr("Open anime list"), home);
      auto syncButton = new QPushButton(tr("Synchronize"), home);
      actions->addWidget(listButton);
      actions->addWidget(syncButton);
      layout->addLayout(actions);

      connect(listButton, &QPushButton::clicked, this,
              [this] { navigateTo(MainWindowPage::List); });
      connect(syncButton, &QPushButton::clicked, this, &MainWindow::synchronize);
      init_page(ui_->homePage, home);
      break;
    }

    case MainWindowPage::Search:
      m_searchWidget = new SearchWidget(ui_->searchPage);
      init_page(ui_->searchPage, m_searchWidget);
      break;

    case MainWindowPage::List:
      m_listWidget = new ListWidget(ui_->listPage);
      init_page(ui_->listPage, m_listWidget);
      break;

    case MainWindowPage::History:
      m_historyWidget = new HistoryWidget(ui_->historyPage);
      init_page(ui_->historyPage, m_historyWidget);
      break;

    case MainWindowPage::Library:
      m_libraryWidget = new LibraryWidget(ui_->libraryPage);
      init_page(ui_->libraryPage, m_libraryWidget);
      break;

    case MainWindowPage::Torrents:
      m_torrentsWidget = new TorrentsWidget(ui_->torrentsPage);
      init_page(ui_->torrentsPage, m_torrentsWidget);
      connect(m_searchBox, &QLineEdit::returnPressed, m_torrentsWidget, [this] {
        if (ui_->stackedWidget->currentWidget() == ui_->torrentsPage) {
          m_torrentsWidget->search(m_searchBox->text());
        }
      });
      break;

    case MainWindowPage::Profile: {
      auto profile = new QWidget(ui_->profilePage);
      auto layout = new QVBoxLayout(profile);
      layout->setAlignment(Qt::AlignCenter);

      auto title = new QLabel(tr("Profile"), profile);
      auto titleFont = title->font();
      titleFont.setPointSize(titleFont.pointSize() + 4);
      titleFont.setBold(true);
      title->setFont(titleFont);
      title->setAlignment(Qt::AlignCenter);
      layout->addWidget(title);

      auto account = new QLabel(profile);
      account->setAlignment(Qt::AlignCenter);
      layout->addWidget(account);
      m_profileSummary = account;
      updateProfilePage();

      auto actions = new QHBoxLayout;
      auto authenticate = new QPushButton(tr("Authenticate"), profile);
      auto syncButton = new QPushButton(tr("Synchronize"), profile);
      auto settingsButton = new QPushButton(tr("Settings"), profile);
      actions->addWidget(authenticate);
      actions->addWidget(syncButton);
      actions->addWidget(settingsButton);
      layout->addLayout(actions);

      connect(authenticate, &QPushButton::clicked, this, &MainWindow::authenticateFromProfile);
      connect(syncButton, &QPushButton::clicked, this, &MainWindow::synchronize);
      connect(settingsButton, &QPushButton::clicked, this, [this] { SettingsDialog::show(this); });
      init_page(ui_->profilePage, profile);
      break;
    }
  }

  initializedPages_.insert(page);
}

void MainWindow::refreshPage(const MainWindowPage page) {
  switch (page) {
    case MainWindowPage::Home:
      updateHomePage();
      break;
    case MainWindowPage::Profile:
      updateProfilePage();
      break;
    default:
      break;
  }
}

void MainWindow::refreshLibrary() {
  if (m_libraryWidget) m_libraryWidget->reloadFolders();
}

void MainWindow::updateHomePage() {
  if (!m_homeSummary) return;

  m_homeSummary->setText(tr("%1 anime in your list · %2 configured library folder(s)")
                             .arg(anime::db.entries().size())
                             .arg(taiga::settings.libraryFolders().size()));
}

void MainWindow::updateProfilePage() {
  if (!m_profileSummary) return;

  const auto service = sync_service::currentServiceId();
  const auto username = taiga::accounts.serviceUsername(sync_service::serviceSlug(service).toStdString());
  const auto accountName =
      username.empty() ? tr("Not configured") : QString::fromStdString(username);

  m_profileSummary->setText(
      tr("%1\nAccount: %2\nStatus: %3")
          .arg(sync_service::serviceName(service), accountName,
               sync_service::isUserAuthenticated() ? tr("Authenticated") : tr("Not authenticated")));
}

void MainWindow::authenticateFromProfile() {
  if (sync_service::currentServiceId() != sync_service::ServiceId::AniList) {
    sync_service::authenticateUser();
    return;
  }

  QPointer<MainWindow> guard{this};
  taiga::accounts.loadAnilistToken([guard] {
    if (!guard || sync_service::currentServiceId() != sync_service::ServiceId::AniList) return;

    if (taiga::accounts.anilistToken().empty()) {
      // The token-entry flow is owned by the Accounts page. Opening it here
      // keeps the profile action usable for a first-time AniList login.
      SettingsDialog::show(guard.data());
    } else {
      sync_service::authenticateUser();
    }
  });
}

void MainWindow::initStatusbar() {
  const auto statusbar = new StatusBar(this);
  statusbar->setObjectName(ui_->statusbar->objectName());
  setStatusBar(statusbar);
  ui_->statusbar = statusbar;

  const auto spinner = new SpinnerWidget(this);
  const auto spinnerContainer = new QWidget(this);
  const auto spinnerLayout = new QVBoxLayout(spinnerContainer);
  spinnerLayout->setContentsMargins(0, 2, 0, 0);
  spinnerLayout->addWidget(spinner);
  ui_->statusbar->addPermanentWidget(spinnerContainer);

  m_statusBarController = new StatusBarController(this, statusbar, spinner);

  connect(&taiga::accounts, &taiga::Accounts::authenticationChanged, this,
          [this](const bool) { updateProfilePage(); });
  connect(&anime::db, &anime::Database::itemUpdated, this, [this](const int) { updateHomePage(); });
  connect(&anime::db, &anime::Database::entryUpdated, this,
          [this](const int) { updateHomePage(); });
  connect(&anime::db, &anime::Database::itemDeleted, this,
          [this](const int, const QString&) { updateHomePage(); });
  connect(&anime::db, &anime::Database::entryDeleted, this,
          [this](const int) { updateHomePage(); });

  const QList<sync_service::Service*> services{
      sync_service::anilist::Service::instance(),
      sync_service::kitsu::Service::instance(),
      sync_service::myanimelist::Service::instance(),
  };
  for (auto* service : services) {
    connect(service, &sync_service::Service::authenticationCompleted, this,
            [this](const bool authenticated) {
              if (!authenticated) return;

              const auto sender_service = qobject_cast<sync_service::Service*>(sender());
              const auto slug = sync_service::serviceSlug(sender_service->id()).toStdString();
              const auto username = taiga::accounts.serviceUsername(slug);

              m_statusBarController->showMessage({
                  .source = StatusBarController::Source::Sync,
                  .text = tr("Logged in as %1.").arg(QString::fromStdString(username)),
                  .spin = false,
              });
            });
    connect(service, &sync_service::Service::listEntriesFetched, this, [this]() {
      m_statusBarController->clearMessage(StatusBarController::Source::Sync);
      setEnabled(true);
    });
    connect(service, &sync_service::Service::errorOccurred, this, [this](const QString& message) {
      const auto sender_service = qobject_cast<sync_service::Service*>(sender());
      m_statusBarController->showMessage({
          .source = StatusBarController::Source::Sync,
          .text = sync_service::tagMessage(sender_service->id(), message),
          .spin = false,
      });
      setEnabled(true);
    });
    connect(service, &sync_service::Service::transferProgress, this,
            [this](const qint64 current, const qint64 total) {
              m_statusBarController->showMessage({
                  .source = StatusBarController::Source::Sync,
                  .text = tr("Synchronizing with %1... (%2)")
                              .arg(sync_service::serviceName(sync_service::currentServiceId()))
                              .arg(gui::formatTransferProgress(current, total)),
              });
            });
  }

  connect(&sync_service::queue, &sync_service::Queue::changed, this, [this]() {
    if (sync_service::queue.count() == 0) {
      m_statusBarController->clearMessage(StatusBarController::Source::Sync);
      setEnabled(true);
    }
  });

  connect(&sync_service::queue, &sync_service::Queue::processing, this, [this](const int animeId) {
    const auto item = anime::db.item(animeId);
    const auto entry = anime::db.entry(animeId);
    if (!item || !entry) return;

    const auto title = QString::fromStdString(anime::preferredTitle(*item));

    QString text;
    if (entry->pending_delete) {
      text = tr("Deleting list entry... (%1)").arg(title);
    } else if (entry->id == anime::list::kUnknownId) {
      text = tr("Adding to list... (%1)").arg(title);
    } else {
      text = tr("Updating list entry... (%1)").arg(title);
    }

    m_statusBarController->showMessage({
        .source = StatusBarController::Source::Sync,
        .text = text,
    });
  });

  connect(&sync_service::queue, &sync_service::Queue::queuedWhileUnauthenticated, this, [this](const int animeId) {
    const auto item = anime::db.item(animeId);
    if (!item) return;

    m_statusBarController->showMessage({
        .source = StatusBarController::Source::Sync,
        .text = tr("%1 is queued for update.")
                    .arg(QString::fromStdString(anime::preferredTitle(*item))),
        .spin = false,
    });
  });

  connect(&anime::db, &anime::Database::itemDeleted, this, [this](const int, const QString& title) {
    if (title.isEmpty()) return;

    m_statusBarController->showMessage({
        .source = StatusBarController::Source::Sync,
        .text = tr("Anime removed from database: %1").arg(title),
        .spin = false,
    });
  });
}

void MainWindow::initToolbar() {
  ui_->toolbar->setIconSize(QSize{24, 24});

  // Menu
  {
    const auto button = static_cast<QToolButton*>(ui_->toolbar->widgetForAction(ui_->actionMenu));
    button->setPopupMode(QToolButton::InstantPopup);
    button->setMenu([this]() {
      auto menu = new QMenu(this);
      menu->addAction(ui_->actionToggleDetection);
      menu->addAction(ui_->actionToggleSharing);
      menu->addAction(ui_->actionToggleSynchronization);
      menu->addAction(ui_->actionToggleNowPlaying);
      menu->addAction(ui_->actionToggleStatusbar);
      menu->addSeparator();
      menu->addMenu(ui_->menuHelp);
      menu->addSeparator();
      menu->addAction(ui_->actionExit);
      return menu;
    }());
  }

  // Search box
  {
    m_searchBox = new QLineEdit();
    m_searchBox->setClearButtonEnabled(true);
    m_searchBox->setFixedWidth(320);
    m_searchBox->setPlaceholderText(tr("Search"));

    const auto before = ui_->actionSettings;
    const auto insertSpacer = [this](QAction* before) {
      ui_->toolbar->insertWidget(before, [this]() {
        auto spacer = new QWidget(this);
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        return spacer;
      }());
    };

    insertSpacer(before);
    ui_->toolbar->insertWidget(before, m_searchBox);
    insertSpacer(before);
  }
}

void MainWindow::initTrayIcon() {
  auto menu = new QMenu(this);
  menu->addAction(ui_->actionDisplayWindow);
  menu->setDefaultAction(ui_->actionDisplayWindow);
  menu->addSeparator();
  menu->addAction(ui_->actionSettings);
  menu->addSeparator();
  menu->addAction(ui_->actionExit);

  m_trayIcon = new TrayIcon(this, windowIcon(), menu);

  connect(m_trayIcon, &TrayIcon::activated, this, &MainWindow::displayWindow);
  connect(m_trayIcon, &TrayIcon::messageClicked, this,
          []() { QMessageBox::information(nullptr, "Taiga", tr("Clicked message")); });
}

void MainWindow::closeEvent(QCloseEvent* event) {
  taiga::session.setMainWindowGeometry(saveGeometry());
  if (m_listWidget) m_listWidget->saveState();
  if (m_searchWidget) m_searchWidget->saveState();
  event->accept();
}

void MainWindow::addNewFolder() {
  constexpr auto options =
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks | QFileDialog::ReadOnly;

  const auto directory = QFileDialog::getExistingDirectory(this, tr("Add New Folder"), "", options);

  if (directory.isEmpty()) return;

  const auto path = QDir::cleanPath(QFileInfo{directory}.absoluteFilePath());
  auto folders = taiga::settings.libraryFolders();
  const auto exists = std::ranges::any_of(folders, [&path](const auto& folder) {
    return QDir::cleanPath(QString::fromStdString(folder)) == path;
  });

  if (exists) {
    statusBarController()->showMessage({
        .source = StatusBarController::Source::Library,
        .text = tr("Library folder is already configured: %1").arg(path),
        .spin = false,
    });
    return;
  }

  folders.push_back(path.toStdString());
  taiga::settings.setLibraryFolders(std::move(folders));
  refreshLibrary();
  statusBarController()->showMessage({
      .source = StatusBarController::Source::Library,
      .text = tr("Added library folder: %1").arg(path),
      .spin = false,
  });
}

void MainWindow::scanAvailableEpisodes() {
  const auto folders = taiga::settings.libraryFolders();
  if (folders.empty()) {
    QMessageBox::information(this, tr("Scan library"),
                             tr("Add at least one library folder before scanning."));
    SettingsDialog::show(this);
    return;
  }

  statusBarController()->showMessage({
      .source = StatusBarController::Source::Library,
      .text = tr("Scanning available episodes..."),
  });
  QApplication::setOverrideCursor(Qt::WaitCursor);
  const auto result = track::scanLibrary(folders);
  QApplication::restoreOverrideCursor();
  refreshLibrary();

  statusBarController()->showMessage({
      .source = StatusBarController::Source::Library,
      .text = tr("Scanned %1 folder(s): %2 video file(s), %3 recognized episode(s).")
                  .arg(result.folders)
                  .arg(result.files)
                  .arg(result.recognized),
      .spin = false,
  });
}

void MainWindow::exportListAsMarkdown() {
  exportList(this, "md", tr("Markdown files (*.md);;All files (*)"),
             &anime::list::exportAsMarkdown);
}

void MainWindow::exportListAsMyAnimeListXml() {
  exportList(this, "xml", tr("XML files (*.xml);;All files (*)"), &anime::list::exportAsXml);
}

void MainWindow::playNextEpisode() {
  auto animeId = selectedAnimeId(this);
  if (!animeId) {
    for (const auto& entry : anime::db.entries()) {
      if (entry.status == anime::list::Status::Watching && anime::db.item(entry.anime_id)) {
        animeId = entry.anime_id;
        break;
      }
    }
  }

  if (!animeId) {
    statusBarController()->showMessage({
        .source = StatusBarController::Source::Playback,
        .text = tr("Select an anime or add one to your list before playing."),
        .spin = false,
    });
    return;
  }

  const auto number = track::nextEpisodeNumber(*animeId);
  const auto item = anime::db.item(*animeId);
  if (!number || !item || !track::playEpisode(*animeId, *number)) {
    const auto message = item ? tr("Could not find episode #%1 (%2).")
                                    .arg(number.value_or(0))
                                    .arg(QString::fromStdString(anime::preferredTitle(*item)))
                              : tr("Could not find the selected anime episode.");
    statusBarController()->showMessage({
        .source = StatusBarController::Source::Playback,
        .text = message,
        .spin = false,
    });
    return;
  }

  statusBarController()->clearMessage(StatusBarController::Source::Playback);
}

void MainWindow::playRandomAnime() {
  std::vector<int> candidates;
  for (const auto& entry : anime::db.entries()) {
    if (anime::db.item(entry.anime_id) && entry.status != anime::list::Status::NotInList)
      candidates.push_back(entry.anime_id);
  }

  if (candidates.empty()) {
    statusBarController()->showMessage({
        .source = StatusBarController::Source::Playback,
        .text = tr("Add an anime to your list before playing a random episode."),
        .spin = false,
    });
    return;
  }

  const auto animeId =
      candidates.at(QRandomGenerator::global()->bounded(static_cast<int>(candidates.size())));
  const auto number = track::randomEpisodeNumber(animeId);
  const auto item = anime::db.item(animeId);
  if (!number || !item || !track::playEpisode(animeId, *number)) {
    const auto message =
        item ? tr("Could not find a playable episode for %1.")
                   .arg(QString::fromStdString(anime::preferredTitle(*item)))
             : tr("Could not find a playable anime.");
    statusBarController()->showMessage({
        .source = StatusBarController::Source::Playback,
        .text = message,
        .spin = false,
    });
    return;
  }

  statusBarController()->clearMessage(StatusBarController::Source::Playback);
}

void MainWindow::navigateTo(MainWindowPage page) {
  if (const auto item = m_navigationWidget->findItemByPage(page)) {
    m_navigationWidget->setCurrentItem(item);
  }
}

void MainWindow::navigateToListStatus(anime::list::Status status) {
  if (const auto item = m_navigationWidget->findListStatusItem(status)) {
    m_navigationWidget->setCurrentItem(item);
  }
}

void MainWindow::searchTorrents(const QString& title) {
  navigateTo(MainWindowPage::Torrents);
  m_searchBox->setText(title);
  if (m_torrentsWidget) m_torrentsWidget->search(title);
}

void MainWindow::configureTorrents() {
  navigateTo(MainWindowPage::Torrents);
  if (m_torrentsWidget) m_torrentsWidget->showSettings();
}

void MainWindow::setPage(MainWindowPage page) {
  initPage(page);
  refreshPage(page);
  m_statusBarController->clearMessage(StatusBarController::Source::Selection);
  ui_->stackedWidget->setCurrentIndex(static_cast<int>(page));
}

void MainWindow::updateTitle() {
  auto title = u"Taiga"_s;

  if (taiga::app()->isDebug()) {
    title += u" [debug]"_s;
  }

  setWindowTitle(title);
}

void MainWindow::displayWindow() {
  setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
  activateWindow();
}

void MainWindow::about() {
  displayAboutDialog(this);
}

void MainWindow::donate() const {
  QDesktopServices::openUrl(QUrl("https://taiga.moe/#donate"));
}

void MainWindow::support() const {
  QDesktopServices::openUrl(QUrl("https://taiga.moe/#support"));
}

void MainWindow::synchronize() {
  setEnabled(false);

  const auto serviceName = sync_service::serviceName(sync_service::currentServiceId());
  const auto text = sync_service::willAuthenticate() ? tr("Authenticating with %1...").arg(serviceName)
                                             : tr("Synchronizing with %1...").arg(serviceName);

  m_statusBarController->showMessage({
      .source = StatusBarController::Source::Sync,
      .text = text,
  });

  if (!sync_service::synchronize()) {
    m_statusBarController->clearMessage(StatusBarController::Source::Sync);
    setEnabled(true);
  }
}

void MainWindow::profile() {
  setPage(MainWindowPage::Profile);
  m_navigationWidget->setCurrentIndex({});
}

}  // namespace gui
