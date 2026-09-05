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

#include "torrents_widget.hpp"

#include <QAction>
#include <QCheckBox>
#include <QColor>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QPalette>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>
#include <algorithm>
#include <limits>

#include "track/torrent_download.hpp"

namespace {

constexpr int kTitleColumn = 0;
constexpr int kSizeColumn = 1;
constexpr int kSeedersColumn = 2;
constexpr int kPublishedColumn = 3;
constexpr int kColumnCount = 4;

constexpr int kTorrentItemRole = Qt::UserRole + 1;

QString numberSortKey(const int number) {
  if (number < 0) return QStringLiteral("000000000000");
  return QString::number(number).rightJustified(12, QLatin1Char('0'));
}

QString dateSortKey(const QDateTime& date) {
  if (!date.isValid()) return QStringLiteral("00000000000000000000");
  return QString::number(date.toMSecsSinceEpoch()).rightJustified(20, QLatin1Char('0'));
}

bool isHttpUrl(const QUrl& url) {
  return url.isValid() && !url.host().isEmpty() &&
         (url.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) == 0 ||
          url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0);
}

QString resolutionFilterValue(QString value) {
  value = value.trimmed().toLower();
  if (value.endsWith(QLatin1Char('p'))) value.chop(1);
  value.remove(QLatin1Char(' '));
  return value;
}

bool matchesReleaseGroup(const QString& title, const QString& filter) {
  const auto wanted = filter.trimmed();
  if (wanted.isEmpty()) return true;

  const auto escaped = QRegularExpression::escape(wanted);
  const QRegularExpression exactWord(
      QStringLiteral("(?i)(?:^|[\\[\\]()._ -])%1(?:$|[\\[\\]()._ -])").arg(escaped));

  auto bracketMatches = QRegularExpression(QStringLiteral(R"(\[([^\]]+)\])")).globalMatch(title);
  while (bracketMatches.hasNext()) {
    const auto match = bracketMatches.next();
    if (match.captured(1).trimmed().compare(wanted, Qt::CaseInsensitive) == 0) return true;
  }

  return exactWord.match(title).hasMatch();
}

bool matchesResolution(const QString& title, const QString& filter) {
  const auto wanted = resolutionFilterValue(filter);
  if (wanted.isEmpty()) return true;

  // Resolution is intentionally an exact numeric match.  This keeps a 720p
  // filter from matching 1720p or another number containing the same digits.
  if (!QRegularExpression(QStringLiteral("^[0-9]+$")).match(wanted).hasMatch()) {
    return title.contains(filter.trimmed(), Qt::CaseInsensitive);
  }

  const auto expression = QRegularExpression(
      QStringLiteral("(?<![0-9])%1p?(?![0-9])").arg(QRegularExpression::escape(wanted)),
      QRegularExpression::CaseInsensitiveOption);
  return expression.match(title).hasMatch();
}

// QTableWidget sorts items using operator<.  Keeping an explicit key here
// means dates and peer counts sort numerically while the row's ID and item
// snapshot stay attached to the item when Qt moves the row.
class TorrentTableItem final : public QTableWidgetItem {
public:
  TorrentTableItem(const QString& text, const QString& sortKey)
      : QTableWidgetItem(text), m_sortKey(sortKey) {}

  bool operator<(const QTableWidgetItem& other) const override {
    const auto* otherItem = dynamic_cast<const TorrentTableItem*>(&other);
    if (!otherItem) return QTableWidgetItem::operator<(other);
    return m_sortKey < otherItem->m_sortKey;
  }

private:
  QString m_sortKey;
};

QString torrentCountText(const int count) {
  return QObject::tr("%n torrent(s)", "torrent count", count);
}

}  // namespace

namespace gui {

TorrentsWidget::TorrentsWidget(QWidget* parent)
    : PageWidget(parent),
      m_feedClient(new track::TorrentFeedClient(this)),
      m_downloader(new track::TorrentDownloader(this)),
      m_refreshTimer(new QTimer(this)),
      m_settings(track::loadTorrentSettings()) {
  for (const auto& id : track::loadTorrentArchive()) {
    m_archivedIds.insert(id);
  }

  // Feed and search controls live next to the inherited toolbar.  They are
  // deliberately ordinary widgets instead of a dependency on MainWindow's
  // global search box, making this page usable in isolation as well.
  m_feedUrlEdit = new QLineEdit(this);
  m_feedUrlEdit->setObjectName(QStringLiteral("torrentFeedUrl"));
  m_feedUrlEdit->setText(m_settings.feedUrl);
  m_feedUrlEdit->setPlaceholderText(tr("RSS feed URL"));
  m_feedUrlEdit->setClearButtonEnabled(true);
  m_feedUrlEdit->setMinimumWidth(240);
  m_feedUrlEdit->setToolTip(tr("RSS feed URL"));
  m_toolbarLayout->insertWidget(0, m_feedUrlEdit);

  m_searchEdit = new QLineEdit(this);
  m_searchEdit->setObjectName(QStringLiteral("torrentSearch"));
  m_searchEdit->setPlaceholderText(tr("Search torrents"));
  m_searchEdit->setClearButtonEnabled(true);
  m_searchEdit->setMinimumWidth(180);
  m_searchEdit->setToolTip(tr("Search torrents using the configured search URL"));
  m_toolbarLayout->insertWidget(1, m_searchEdit);

  initToolbar();

  // Filters are intentionally visible on the page.  Their values are saved
  // as the user edits them, so opening Settings is never required just to
  // retain a filter.
  auto* filterWidget = new QWidget(this);
  filterWidget->setObjectName(QStringLiteral("torrentFiltersWidget"));
  auto* filtersLayout = new QHBoxLayout(filterWidget);
  filtersLayout->setContentsMargins(16, 0, 16, 8);
  filtersLayout->setSpacing(4);

  m_titleFilterEdit = new QLineEdit(filterWidget);
  m_titleFilterEdit->setObjectName(QStringLiteral("torrentTitleFilter"));
  m_titleFilterEdit->setPlaceholderText(tr("Title filter"));
  m_titleFilterEdit->setClearButtonEnabled(true);
  m_titleFilterEdit->setText(m_settings.titleFilter);
  m_titleFilterEdit->setToolTip(tr("Keep torrents whose title contains this text"));
  filtersLayout->addWidget(m_titleFilterEdit);

  m_groupFilterEdit = new QLineEdit(filterWidget);
  m_groupFilterEdit->setObjectName(QStringLiteral("torrentGroupFilter"));
  m_groupFilterEdit->setPlaceholderText(tr("Release group"));
  m_groupFilterEdit->setClearButtonEnabled(true);
  m_groupFilterEdit->setText(m_settings.releaseGroup);
  m_groupFilterEdit->setToolTip(tr("Exact release group filter"));
  filtersLayout->addWidget(m_groupFilterEdit);

  m_resolutionFilterEdit = new QLineEdit(filterWidget);
  m_resolutionFilterEdit->setObjectName(QStringLiteral("torrentResolutionFilter"));
  m_resolutionFilterEdit->setPlaceholderText(tr("Resolution (e.g. 1080p)"));
  m_resolutionFilterEdit->setClearButtonEnabled(true);
  m_resolutionFilterEdit->setText(m_settings.resolution);
  m_resolutionFilterEdit->setToolTip(tr("Exact numeric resolution filter"));
  filtersLayout->addWidget(m_resolutionFilterEdit);

  filtersLayout->addStretch();

  m_showArchivedCheck = new QCheckBox(tr("Show archived"), filterWidget);
  m_showArchivedCheck->setObjectName(QStringLiteral("torrentShowArchived"));
  m_showArchivedCheck->setChecked(!m_settings.hideArchived);
  m_showArchivedCheck->setToolTip(tr("Include torrents discarded in an earlier session"));
  filtersLayout->addWidget(m_showArchivedCheck);

  layout()->addWidget(filterWidget);

  m_statusLabel = new QLabel(this);
  m_statusLabel->setObjectName(QStringLiteral("torrentStatusLabel"));
  m_statusLabel->setTextFormat(Qt::PlainText);
  m_statusLabel->setWordWrap(true);
  m_statusLabel->setMaximumHeight(80);
  m_statusLabel->setContentsMargins(16, 0, 16, 8);
  m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout()->addWidget(m_statusLabel);

  m_table = new QTableWidget(this);
  m_table->setObjectName(QStringLiteral("torrentTable"));
  m_table->setColumnCount(kColumnCount);
  m_table->setHorizontalHeaderLabels({tr("Title"), tr("Size"), tr("Seeds"), tr("Published")});
  m_table->setFrameShape(QFrame::Shape::NoFrame);
  m_table->setAlternatingRowColors(true);
  m_table->setSelectionBehavior(QAbstractItemView::SelectionBehavior::SelectRows);
  m_table->setSelectionMode(QAbstractItemView::SelectionMode::ExtendedSelection);
  m_table->setEditTriggers(QAbstractItemView::EditTrigger::NoEditTriggers);
  m_table->setSortingEnabled(true);
  m_table->setContextMenuPolicy(Qt::ContextMenuPolicy::CustomContextMenu);
  m_table->verticalHeader()->setVisible(false);
  m_table->horizontalHeader()->setSectionsMovable(false);
  m_table->horizontalHeader()->setStretchLastSection(false);
  m_table->horizontalHeader()->setTextElideMode(Qt::TextElideMode::ElideRight);
  m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeMode::ResizeToContents);
  m_table->horizontalHeader()->setSectionResizeMode(kTitleColumn, QHeaderView::ResizeMode::Stretch);
  layout()->addWidget(m_table);

  connect(m_feedClient, &track::TorrentFeedClient::finished, this,
          &TorrentsWidget::handleFeedFinished);
  connect(m_feedClient, &track::TorrentFeedClient::failed, this, &TorrentsWidget::handleFeedFailed);
  connect(m_downloader, &track::TorrentDownloader::succeeded, this,
          &TorrentsWidget::handleDownloadSucceeded);
  connect(m_downloader, &track::TorrentDownloader::failed, this,
          &TorrentsWidget::handleDownloadFailed);

  connect(m_refreshTimer, &QTimer::timeout, this, &TorrentsWidget::refresh);
  connect(m_actionRefresh, &QAction::triggered, this, &TorrentsWidget::refresh);
  connect(m_actionCancel, &QAction::triggered, this, &TorrentsWidget::cancelRefresh);
  connect(m_actionOpen, &QAction::triggered, this, [this]() { openItems(checkedOrSelectedIds()); });
  connect(m_actionDiscard, &QAction::triggered, this, &TorrentsWidget::discardSelected);
  connect(m_actionRestore, &QAction::triggered, this, &TorrentsWidget::restoreSelected);
  connect(m_actionSettings, &QAction::triggered, this, &TorrentsWidget::showSettings);

  connect(m_feedUrlEdit, &QLineEdit::returnPressed, this, &TorrentsWidget::refresh);
  connect(m_searchEdit, &QLineEdit::returnPressed, this,
          [this]() { search(m_searchEdit->text()); });

  const auto filterChanged = [this] {
    applyFilters();
    const auto settings = settingsFromUi();
    QString error;
    if (!saveSettings(settings, &error)) setStatus(error, true);
  };
  connect(m_titleFilterEdit, &QLineEdit::textChanged, this, filterChanged);
  connect(m_groupFilterEdit, &QLineEdit::textChanged, this, filterChanged);
  connect(m_resolutionFilterEdit, &QLineEdit::textChanged, this, filterChanged);
  connect(m_showArchivedCheck, &QCheckBox::toggled, this, filterChanged);

  connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
    if (m_updatingTable || !item || item->column() != kTitleColumn) return;

    const auto key = item->data(Qt::ItemDataRole::UserRole).toString();
    if (key.isEmpty()) return;
    if (item->checkState() == Qt::CheckState::Checked) {
      m_checkedIds.insert(key);
    } else {
      m_checkedIds.remove(key);
    }
    updateActionState();
  });
  connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this,
          [this] { updateActionState(); });
  connect(m_table, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem* item) {
    if (!item) return;
    const auto key = item->data(Qt::ItemDataRole::UserRole).toString();
    if (!key.isEmpty()) openItems({key});
  });
  connect(m_table, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& point) {
    const auto index = m_table->indexAt(point);
    if (!index.isValid()) return;

    m_table->setCurrentCell(index.row(), index.column(),
                            QItemSelectionModel::SelectionFlag::ClearAndSelect |
                                QItemSelectionModel::SelectionFlag::Rows);

    const auto item = itemAtRow(index.row());
    if (!item) return;
    const auto key = itemKey(*item);

    QMenu menu(m_table);
    if (item->infoUrl.isValid() && !item->infoUrl.isEmpty()) {
      menu.addAction(tr("Open torrent page"), this,
                     [url = item->infoUrl] { QDesktopServices::openUrl(url); });
      menu.addSeparator();
    }
    menu.addAction(tr("Open selected"), this, [this] { openItems(checkedOrSelectedIds()); });
    menu.addAction(tr("Discard selected"), this, &TorrentsWidget::discardSelected);
    menu.addAction(tr("Restore selected"), this, &TorrentsWidget::restoreSelected);
    if (key.isEmpty()) {
      menu.actions().back()->setEnabled(false);
    }
    menu.exec(m_table->viewport()->mapToGlobal(point));
  });

  setStatus(tr("Ready."));
  renderItems();
  updateRefreshTimer();
  updateActionState();
}

void TorrentsWidget::initToolbar() {
  m_actionRefresh = m_toolbar->addAction(tr("Refresh"));
  m_actionRefresh->setObjectName(QStringLiteral("torrentRefresh"));
  m_actionRefresh->setToolTip(tr("Refresh the RSS feed"));

  m_actionCancel = m_toolbar->addAction(tr("Cancel"));
  m_actionCancel->setObjectName(QStringLiteral("torrentCancel"));
  m_actionCancel->setToolTip(tr("Cancel the RSS request"));

  m_toolbar->addSeparator();

  m_actionOpen = m_toolbar->addAction(tr("Open selected"));
  m_actionOpen->setObjectName(QStringLiteral("torrentOpen"));
  m_actionOpen->setToolTip(tr("Download and open checked or selected torrents"));

  m_actionDiscard = m_toolbar->addAction(tr("Discard selected"));
  m_actionDiscard->setObjectName(QStringLiteral("torrentDiscard"));
  m_actionDiscard->setToolTip(tr("Hide checked or selected torrents permanently"));

  m_actionRestore = m_toolbar->addAction(tr("Restore selected"));
  m_actionRestore->setObjectName(QStringLiteral("torrentRestore"));
  m_actionRestore->setToolTip(tr("Remove checked or selected torrents from the archive"));

  m_toolbar->addSeparator();

  m_actionSettings = m_toolbar->addAction(tr("Settings"));
  m_actionSettings->setObjectName(QStringLiteral("torrentSettings"));
  m_actionSettings->setToolTip(tr("Configure torrent feeds and downloads"));
}

void TorrentsWidget::refresh() {
  if (m_fetching) return;

  const auto settings = settingsFromUi();
  QString error;
  if (!saveSettings(settings, &error)) {
    setStatus(error, true);
    return;
  }

  const auto url = QUrl::fromUserInput(settings.feedUrl);
  fetch(url);
}

void TorrentsWidget::cancelRefresh() {
  if (!m_fetching) return;

  m_feedClient->cancel();
  m_fetching = false;
  updateActionState();
  setStatus(tr("Refresh canceled."));
}

void TorrentsWidget::search(const QString& query) {
  const auto trimmedQuery = query.trimmed();
  m_searchEdit->setText(query);

  const auto settings = settingsFromUi();
  QString error;
  if (!saveSettings(settings, &error)) {
    setStatus(error, true);
    return;
  }

  const auto url = trimmedQuery.isEmpty()
                       ? QUrl::fromUserInput(settings.feedUrl)
                       : track::torrentSearchUrl(settings.searchUrl, trimmedQuery);
  fetch(url);
}

void TorrentsWidget::fetch(const QUrl& url) {
  if (!isHttpUrl(url)) {
    setStatus(tr("Use a valid HTTP(S) torrent feed URL."), true);
    return;
  }

  m_fetching = true;
  setStatus(tr("Loading torrents…"));
  updateActionState();
  m_feedClient->fetch(url);
}

void TorrentsWidget::handleFeedFinished(const QList<track::TorrentItem>& items) {
  m_fetching = false;
  m_items = items;

  // Keep checked state only for items that still exist in the new feed.  The
  // state itself is keyed by ID, never by a table row (sorting changes rows).
  QSet<QString> validIds;
  for (const auto& item : m_items) validIds.insert(itemKey(item));
  for (auto it = m_checkedIds.begin(); it != m_checkedIds.end();) {
    if (!validIds.contains(*it)) {
      it = m_checkedIds.erase(it);
    } else {
      ++it;
    }
  }

  m_statusIsError = false;
  renderItems();
  updateActionState();
}

void TorrentsWidget::handleFeedFailed(const QString& error) {
  m_fetching = false;
  updateActionState();
  setStatus(error.isEmpty() ? tr("Could not load the torrent feed.") : error, true);
}

void TorrentsWidget::renderItems() {
  if (!m_table) return;

  m_updatingTable = true;
  const auto sortingEnabled = m_table->isSortingEnabled();
  m_table->setSortingEnabled(false);
  m_table->setRowCount(0);

  int row = 0;
  for (const auto& item : m_items) {
    if (!matchesFilters(item)) continue;

    const auto key = itemKey(item);
    m_table->insertRow(row);

    auto* titleItem = new TorrentTableItem(item.title, item.title.toCaseFolded());
    titleItem->setData(Qt::ItemDataRole::UserRole, key);
    titleItem->setData(kTorrentItemRole, QVariant::fromValue(item));
    titleItem->setFlags((titleItem->flags() | Qt::ItemFlag::ItemIsUserCheckable) &
                        ~Qt::ItemFlag::ItemIsEditable);
    titleItem->setCheckState(m_checkedIds.contains(key) ? Qt::CheckState::Checked
                                                        : Qt::CheckState::Unchecked);
    if (item.infoUrl.isValid() && !item.infoUrl.isEmpty()) {
      titleItem->setToolTip(item.infoUrl.toString());
    }

    const auto archived = isArchived(item);
    if (archived) {
      const auto color =
          m_table->palette().color(QPalette::ColorGroup::Disabled, QPalette::ColorRole::Text);
      titleItem->setForeground(color);
    }
    m_table->setItem(row, kTitleColumn, titleItem);

    auto* sizeItem = new TorrentTableItem(item.size, item.size.toCaseFolded());
    if (archived) sizeItem->setForeground(titleItem->foreground());
    m_table->setItem(row, kSizeColumn, sizeItem);

    const auto seedText = item.seeders >= 0 ? QString::number(item.seeders) : QStringLiteral("—");
    auto* seedItem = new TorrentTableItem(seedText, numberSortKey(item.seeders));
    if (archived) seedItem->setForeground(titleItem->foreground());
    m_table->setItem(row, kSeedersColumn, seedItem);

    const auto publishedText =
        item.published.isValid()
            ? QLocale().toString(item.published.toLocalTime(), QLocale::FormatType::ShortFormat)
            : QStringLiteral("—");
    auto* publishedItem = new TorrentTableItem(publishedText, dateSortKey(item.published));
    if (archived) publishedItem->setForeground(titleItem->foreground());
    m_table->setItem(row, kPublishedColumn, publishedItem);

    ++row;
  }

  m_table->setSortingEnabled(sortingEnabled);
  m_updatingTable = false;
  updateActionState();

  if (!m_fetching && !m_statusIsError) setStatus(torrentCountText(row));
}

void TorrentsWidget::applyFilters() {
  renderItems();
}

bool TorrentsWidget::matchesFilters(const track::TorrentItem& item) const {
  if (isArchived(item) && !m_showArchivedCheck->isChecked()) return false;

  const auto titleFilter = m_titleFilterEdit->text().trimmed();
  if (!titleFilter.isEmpty() && !item.title.contains(titleFilter, Qt::CaseInsensitive))
    return false;

  if (!matchesReleaseGroup(item.title, m_groupFilterEdit->text())) return false;
  if (!matchesResolution(item.title, m_resolutionFilterEdit->text())) return false;

  return true;
}

bool TorrentsWidget::isArchived(const track::TorrentItem& item) const {
  const auto key = itemKey(item);
  return !key.isEmpty() && m_archivedIds.contains(key);
}

QString TorrentsWidget::itemKey(const track::TorrentItem& item) const {
  if (!item.id.trimmed().isEmpty()) return item.id.trimmed();
  if (item.infoUrl.isValid() && !item.infoUrl.isEmpty()) {
    return item.infoUrl.toString(QUrl::FullyEncoded);
  }
  if (item.downloadUrl.isValid() && !item.downloadUrl.isEmpty()) {
    return item.downloadUrl.toString(QUrl::FullyEncoded);
  }
  return item.title.trimmed();
}

std::optional<track::TorrentItem> TorrentsWidget::itemAtRow(const int row) const {
  if (!m_table || row < 0 || row >= m_table->rowCount()) return std::nullopt;
  const auto* item = m_table->item(row, kTitleColumn);
  if (!item) return std::nullopt;

  const auto data = item->data(kTorrentItemRole);
  if (!data.canConvert<track::TorrentItem>()) return std::nullopt;
  return data.value<track::TorrentItem>();
}

QList<QString> TorrentsWidget::checkedOrSelectedIds() const {
  QList<QString> ids;
  QSet<QString> seen;

  for (int row = 0; row < m_table->rowCount(); ++row) {
    const auto* item = m_table->item(row, kTitleColumn);
    if (!item || item->checkState() != Qt::CheckState::Checked) continue;
    const auto key = item->data(Qt::ItemDataRole::UserRole).toString();
    if (!key.isEmpty() && !seen.contains(key)) {
      seen.insert(key);
      ids.append(key);
    }
  }

  if (!ids.isEmpty()) return ids;

  for (const auto& index : m_table->selectionModel()->selectedRows(kTitleColumn)) {
    const auto* item = m_table->item(index.row(), kTitleColumn);
    if (!item) continue;
    const auto key = item->data(Qt::ItemDataRole::UserRole).toString();
    if (!key.isEmpty() && !seen.contains(key)) {
      seen.insert(key);
      ids.append(key);
    }
  }

  return ids;
}

void TorrentsWidget::openItems(const QList<QString>& ids) {
  if (m_currentDownload || !m_downloadQueue.isEmpty()) return;
  m_downloadErrors.clear();
  if (ids.isEmpty()) {
    setStatus(tr("Select at least one torrent."), true);
    return;
  }

  QSet<QString> wanted(ids.cbegin(), ids.cend());
  QSet<QString> queued;
  for (int row = 0; row < m_table->rowCount(); ++row) {
    const auto item = itemAtRow(row);
    if (!item) continue;
    const auto key = itemKey(*item);
    if (wanted.contains(key) && !queued.contains(key)) {
      queued.insert(key);
      m_downloadQueue.enqueue(*item);
    }
  }

  if (m_downloadQueue.isEmpty()) {
    setStatus(tr("The selected torrent is no longer available."), true);
    return;
  }

  startNextDownload();
}

void TorrentsWidget::startNextDownload() {
  if (m_currentDownload || m_downloadQueue.isEmpty()) {
    updateActionState();
    if (!m_currentDownload && !m_downloadErrors.isEmpty()) {
      setStatus(tr("Finished with %1 error(s): %2")
                    .arg(m_downloadErrors.size())
                    .arg(m_downloadErrors.join("; ")),
                true);
    }
    return;
  }

  m_currentDownload = m_downloadQueue.dequeue();
  updateActionState();
  setStatus(tr("Opening %1…").arg(m_currentDownload->title));
  m_downloader->download(*m_currentDownload, m_settings.downloadDirectory);
}

void TorrentsWidget::handleDownloadSucceeded(const QString& id, const QUrl& url) {
  if (!m_currentDownload) return;
  if (!id.isEmpty() && !m_currentDownload->id.isEmpty() && id != m_currentDownload->id) return;

  const auto item = *m_currentDownload;
  const auto archiveId = id.isEmpty() ? itemKey(item) : id;
  m_currentDownload.reset();

  // Opening is part of the explicit user action.  Only an actual successful
  // QDesktopServices hand-off is allowed to archive the item.
  if (!url.isValid() || url.isEmpty() || !QDesktopServices::openUrl(url)) {
    const auto location =
        url.isValid() && !url.isEmpty() ? url.toDisplayString() : tr("the downloaded file");
    const auto error =
        tr("Could not open %1 (%2). Check your default torrent client.").arg(item.title, location);
    m_downloadErrors.append(error);
    setStatus(error, true);
    QTimer::singleShot(0, this, &TorrentsWidget::startNextDownload);
    return;
  }

  if (!archiveId.isEmpty()) {
    auto archive = m_archivedIds;
    archive.insert(archiveId);
    if (!saveArchive(archive)) {
      m_downloadErrors.append(
          tr("Opened %1, but could not save its archive entry.").arg(item.title));
      QTimer::singleShot(0, this, &TorrentsWidget::startNextDownload);
      return;
    }
  }

  m_checkedIds.remove(archiveId);
  m_statusIsError = false;
  renderItems();
  setStatus(tr("Opened %1.").arg(item.title));
  QTimer::singleShot(0, this, &TorrentsWidget::startNextDownload);
}

void TorrentsWidget::handleDownloadFailed(const QString& id, const QString& error) {
  if (!m_currentDownload) return;
  if (!id.isEmpty() && !m_currentDownload->id.isEmpty() && id != m_currentDownload->id) return;

  const auto title = m_currentDownload->title;
  m_currentDownload.reset();
  const auto message = tr("Could not open %1: %2").arg(title, error);
  m_downloadErrors.append(message);
  setStatus(message, true);
  QTimer::singleShot(0, this, &TorrentsWidget::startNextDownload);
}

void TorrentsWidget::discardSelected() {
  const auto ids = checkedOrSelectedIds();
  if (ids.isEmpty()) {
    setStatus(tr("Select at least one torrent to discard."), true);
    return;
  }

  auto archive = m_archivedIds;
  for (const auto& id : ids) archive.insert(id);
  if (!saveArchive(archive)) return;

  for (const auto& id : ids) m_checkedIds.remove(id);
  m_statusIsError = false;
  renderItems();
  setStatus(tr("Discarded %n torrent(s).", "discarded torrent count", ids.size()));
}

void TorrentsWidget::restoreSelected() {
  const auto ids = checkedOrSelectedIds();
  if (ids.isEmpty()) {
    setStatus(tr("Select at least one archived torrent to restore."), true);
    return;
  }

  auto archive = m_archivedIds;
  int restored = 0;
  for (const auto& id : ids) {
    if (archive.remove(id)) ++restored;
  }
  if (!saveArchive(archive)) return;

  for (const auto& id : ids) m_checkedIds.remove(id);
  m_statusIsError = false;
  renderItems();
  setStatus(tr("Restored %n torrent(s).", "restored torrent count", restored));
}

bool TorrentsWidget::saveArchive(const QSet<QString>& ids) {
  auto values = ids.values();
  std::sort(values.begin(), values.end());

  QString error;
  if (!track::saveTorrentArchive(values, &error)) {
    setStatus(error.isEmpty() ? tr("Could not save the torrent archive.") : error, true);
    return false;
  }
  m_archivedIds = ids;
  return true;
}

void TorrentsWidget::openInfoForRow(const int row) {
  const auto item = itemAtRow(row);
  if (!item || !item->infoUrl.isValid() || item->infoUrl.isEmpty()) return;
  QDesktopServices::openUrl(item->infoUrl);
}

track::TorrentSettings TorrentsWidget::settingsFromUi() const {
  auto settings = m_settings;
  settings.feedUrl = m_feedUrlEdit->text().trimmed();
  settings.titleFilter = m_titleFilterEdit->text().trimmed();
  settings.releaseGroup = m_groupFilterEdit->text().trimmed();
  settings.resolution = m_resolutionFilterEdit->text().trimmed();
  settings.hideArchived = !m_showArchivedCheck->isChecked();
  return settings;
}

bool TorrentsWidget::saveSettings(const track::TorrentSettings& settings, QString* error) {
  const auto refreshChanged = settings.autoRefresh != m_settings.autoRefresh ||
                              settings.refreshMinutes != m_settings.refreshMinutes;
  if (!track::saveTorrentSettings(settings, error)) return false;

  m_settings = settings;
  if (refreshChanged) updateRefreshTimer();
  return true;
}

void TorrentsWidget::showSettings() {
  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("torrentSettingsDialog"));
  dialog.setWindowTitle(tr("Torrent settings"));
  dialog.setModal(true);

  auto* dialogLayout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  form->setFieldGrowthPolicy(QFormLayout::FieldGrowthPolicy::ExpandingFieldsGrow);
  dialogLayout->addLayout(form);

  auto* feedEdit = new QLineEdit(m_settings.feedUrl, &dialog);
  feedEdit->setObjectName(QStringLiteral("torrentSettingsFeedUrlEdit"));
  form->addRow(tr("RSS feed URL"), feedEdit);

  auto* searchEdit = new QLineEdit(m_settings.searchUrl, &dialog);
  searchEdit->setObjectName(QStringLiteral("torrentSettingsSearchUrlEdit"));
  form->addRow(tr("Search URL (%title%)"), searchEdit);

  auto* directoryEdit = new QLineEdit(m_settings.downloadDirectory, &dialog);
  directoryEdit->setObjectName(QStringLiteral("torrentSettingsDirectoryEdit"));
  auto* directoryWidget = new QWidget(&dialog);
  auto* directoryLayout = new QHBoxLayout(directoryWidget);
  directoryLayout->setContentsMargins(0, 0, 0, 0);
  directoryLayout->addWidget(directoryEdit);
  auto* browseButton = new QPushButton(tr("Browse…"), directoryWidget);
  browseButton->setObjectName(QStringLiteral("torrentSettingsBrowseButton"));
  directoryLayout->addWidget(browseButton);
  form->addRow(tr("Download directory"), directoryWidget);

  auto* autoRefreshCheck = new QCheckBox(tr("Enable automatic refresh"), &dialog);
  autoRefreshCheck->setObjectName(QStringLiteral("torrentSettingsAutoRefreshCheck"));
  autoRefreshCheck->setChecked(m_settings.autoRefresh);
  form->addRow(QString{}, autoRefreshCheck);

  auto* intervalSpin = new QSpinBox(&dialog);
  intervalSpin->setObjectName(QStringLiteral("torrentSettingsIntervalSpin"));
  intervalSpin->setRange(1, 1440);
  intervalSpin->setValue(std::clamp(m_settings.refreshMinutes, 1, 1440));
  intervalSpin->setSuffix(tr(" min"));
  intervalSpin->setEnabled(autoRefreshCheck->isChecked());
  form->addRow(tr("Refresh interval"), intervalSpin);

  auto* errorLabel = new QLabel(&dialog);
  errorLabel->setObjectName(QStringLiteral("torrentSettingsErrorLabel"));
  errorLabel->setWordWrap(true);
  errorLabel->setTextInteractionFlags(Qt::TextInteractionFlag::TextSelectableByMouse);
  dialogLayout->addWidget(errorLabel);

  auto* buttonBox = new QDialogButtonBox(
      QDialogButtonBox::StandardButton::Ok | QDialogButtonBox::StandardButton::Cancel, &dialog);
  dialogLayout->addWidget(buttonBox);

  connect(browseButton, &QPushButton::clicked, &dialog, [&dialog, directoryEdit] {
    const auto directory = QFileDialog::getExistingDirectory(
        &dialog, QObject::tr("Select torrent download directory"), directoryEdit->text(),
        QFileDialog::Option::ShowDirsOnly | QFileDialog::Option::DontResolveSymlinks);
    if (!directory.isEmpty()) directoryEdit->setText(directory);
  });
  connect(autoRefreshCheck, &QCheckBox::toggled, intervalSpin, &QSpinBox::setEnabled);
  connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  connect(buttonBox, &QDialogButtonBox::accepted, &dialog, [&] {
    const auto feedUrl = QUrl::fromUserInput(feedEdit->text().trimmed());
    const auto searchUrl =
        QUrl::fromUserInput(QString(searchEdit->text().trimmed())
                                .replace(QStringLiteral("%title%"), QStringLiteral("torrent")));
    if (!isHttpUrl(feedUrl)) {
      errorLabel->setText(tr("Use a valid HTTP(S) feed URL."));
      return;
    }
    if (!searchEdit->text().contains(QStringLiteral("%title%")) || !isHttpUrl(searchUrl)) {
      errorLabel->setText(tr("The search URL must be HTTP(S) and contain %title%."));
      return;
    }
    if (!QDir::isAbsolutePath(directoryEdit->text().trimmed())) {
      errorLabel->setText(tr("Choose an absolute download directory."));
      return;
    }

    auto settings = m_settings;
    settings.feedUrl = feedEdit->text().trimmed();
    settings.searchUrl = searchEdit->text().trimmed();
    settings.downloadDirectory = directoryEdit->text().trimmed();
    settings.autoRefresh = autoRefreshCheck->isChecked();
    settings.refreshMinutes = intervalSpin->value();
    settings.titleFilter = m_titleFilterEdit->text().trimmed();
    settings.releaseGroup = m_groupFilterEdit->text().trimmed();
    settings.resolution = m_resolutionFilterEdit->text().trimmed();
    settings.hideArchived = !m_showArchivedCheck->isChecked();

    QString error;
    if (!saveSettings(settings, &error)) {
      errorLabel->setText(error);
      return;
    }

    m_feedUrlEdit->setText(settings.feedUrl);
    m_settings = settings;
    m_statusIsError = false;
    renderItems();
    updateRefreshTimer();
    dialog.accept();
  });

  dialog.exec();
}

void TorrentsWidget::updateRefreshTimer() {
  if (!m_refreshTimer) return;
  if (!m_settings.autoRefresh || m_settings.refreshMinutes <= 0) {
    m_refreshTimer->stop();
    return;
  }

  const auto milliseconds =
      std::clamp<qint64>(static_cast<qint64>(m_settings.refreshMinutes) * 60 * 1000, 1,
                         static_cast<qint64>(std::numeric_limits<int>::max()));
  m_refreshTimer->start(static_cast<int>(milliseconds));
}

void TorrentsWidget::updateActionState() {
  if (!m_actionRefresh) return;
  m_actionRefresh->setEnabled(!m_fetching);
  m_actionCancel->setEnabled(m_fetching);

  const auto ids = checkedOrSelectedIds();
  const auto hasSelection = !ids.isEmpty();
  bool hasArchived = false;
  bool hasUnarchived = false;
  for (const auto& id : ids) {
    for (int row = 0; row < m_table->rowCount(); ++row) {
      const auto item = itemAtRow(row);
      if (!item || itemKey(*item) != id) continue;
      if (isArchived(*item)) {
        hasArchived = true;
      } else {
        hasUnarchived = true;
      }
      break;
    }
  }

  m_actionOpen->setEnabled(hasSelection && !m_currentDownload && m_downloadQueue.isEmpty());
  m_actionDiscard->setEnabled(hasUnarchived);
  m_actionRestore->setEnabled(hasArchived);
}

void TorrentsWidget::setStatus(const QString& text, const bool error) {
  m_statusIsError = error;
  m_statusLabel->setText(text);
  m_statusLabel->setToolTip(text);
  if (error) {
    auto palette = m_statusLabel->palette();
    const auto errorColor = QColor(180, 0, 0);
    palette.setColor(QPalette::ColorGroup::Active, QPalette::ColorRole::WindowText, errorColor);
    palette.setColor(QPalette::ColorGroup::Inactive, QPalette::ColorRole::WindowText, errorColor);
    m_statusLabel->setPalette(palette);
  } else {
    m_statusLabel->setPalette(m_table->palette());
  }
}

}  // namespace gui
