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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <QList>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QStringList>
#include <optional>

#include "gui/common/page_widget.hpp"
#include "track/seadex.hpp"
#include "track/torrent_feed.hpp"
#include "track/torrent_settings.hpp"

class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;
class QTimer;

namespace track {
class SeaDexClient;
class TorrentDownloader;
}  // namespace track

namespace gui {

class TorrentsWidget final : public PageWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(TorrentsWidget)

public:
  explicit TorrentsWidget(QWidget* parent);
  ~TorrentsWidget() = default;

  void search(const QString& query);
  void showSettings();

private:
  void initToolbar();
  void refresh();
  void cancelRefresh();
  void fetch(const QUrl& url);
  void handleFeedFinished(const QList<track::TorrentItem>& items);
  void handleFeedFailed(const QString& error);
  void handleSeaDexFinished(const track::SeaDexReleases& releases);

  void renderItems();
  void applyFilters();
  bool matchesFilters(const track::TorrentItem& item) const;
  bool isArchived(const track::TorrentItem& item) const;
  QString itemKey(const track::TorrentItem& item) const;
  std::optional<track::TorrentItem> itemAtRow(int row) const;

  QList<QString> checkedOrSelectedIds() const;
  void openItems(const QList<QString>& ids);
  void startNextDownload();
  void handleDownloadSucceeded(const QString& id, const QUrl& url);
  void handleDownloadFailed(const QString& id, const QString& error);

  void discardSelected();
  void restoreSelected();
  bool saveArchive(const QSet<QString>& ids);
  void openInfoForRow(int row);

  track::TorrentSettings settingsFromUi() const;
  bool saveSettings(const track::TorrentSettings& settings, QString* error = nullptr);
  void updateFeedSelector();
  void updateRefreshTimer();
  void updateActionState();
  void setStatus(const QString& text, bool error = false);

  track::TorrentFeedClient* m_feedClient = nullptr;
  track::SeaDexClient* m_seadexClient = nullptr;
  track::TorrentDownloader* m_downloader = nullptr;

  QComboBox* m_feedSelector = nullptr;
  QLineEdit* m_searchEdit = nullptr;
  QLineEdit* m_titleFilterEdit = nullptr;
  QLineEdit* m_groupFilterEdit = nullptr;
  QLineEdit* m_resolutionFilterEdit = nullptr;
  QCheckBox* m_showArchivedCheck = nullptr;
  QLabel* m_statusLabel = nullptr;
  QTableWidget* m_table = nullptr;
  QTimer* m_refreshTimer = nullptr;

  QAction* m_actionRefresh = nullptr;
  QAction* m_actionCancel = nullptr;
  QAction* m_actionOpen = nullptr;
  QAction* m_actionDiscard = nullptr;
  QAction* m_actionRestore = nullptr;
  QAction* m_actionSettings = nullptr;

  QList<track::TorrentItem> m_items;
  track::SeaDexReleases m_seadexReleases;
  QSet<QString> m_archivedIds;
  QSet<QString> m_checkedIds;
  QQueue<track::TorrentItem> m_downloadQueue;
  std::optional<track::TorrentItem> m_currentDownload;
  QStringList m_downloadErrors;

  track::TorrentSettings m_settings;
  bool m_fetching = false;
  bool m_updatingTable = false;
  bool m_statusIsError = false;
};

}  // namespace gui
