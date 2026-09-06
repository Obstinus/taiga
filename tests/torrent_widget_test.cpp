#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QLineEdit>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>

#include "gui/torrents/torrents_widget.hpp"
#include "track/torrent_feed.hpp"
#include "track/torrent_settings.hpp"

namespace {
QString dataPath;
int failures = 0;

void check(bool result, const char* message) {
  if (result) return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  ++failures;
}

int visibleRows(QTableWidget* table) {
  int count = 0;
  for (int row = 0; row < table->rowCount(); ++row) {
    if (!table->isRowHidden(row)) ++count;
  }
  return count;
}

int titleRow(QTableWidget* table, const QString& title) {
  for (int row = 0; row < table->rowCount(); ++row) {
    for (int col = 0; col < table->columnCount(); ++col) {
      if (table->item(row, col) && table->item(row, col)->text() == title) return row;
    }
  }
  return -1;
}
}  // namespace

namespace taiga {
std::string get_data_path() {
  return dataPath.toStdString();
}
}  // namespace taiga

class UrlReceiver : public QObject {
  Q_OBJECT
public:
  QList<QUrl> urls;
public slots:
  void open(const QUrl& url) {
    urls.append(url);
  }
};

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QTemporaryDir temporary;
  if (!temporary.isValid()) return 1;
  dataPath = temporary.path();
  auto settings = track::loadTorrentSettings();
  settings.feedUrl = "https://releases.moe/rss";
  settings.feedUrls = {"https://nyaa.si/?page=rss&c=1_2&f=0", settings.feedUrl};
  settings.searchUrl = "http://127.0.0.1:1/rss?q=%title%";
  settings.autoRefresh = false;
  check(track::saveTorrentSettings(settings), "save isolated widget settings");

  const QList<track::TorrentItem> items{
      {.id = "alpha",
       .title = "[Group A] Example Alpha - 01 [1080p]",
       .infoHash = "0123456789abcdef0123456789abcdef01234567",
       .downloadUrl = QUrl("magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567"),
       .size = "",
       .seeders = 100,
       .leechers = 3},
      {.id = "beta",
       .title = "[Group B] Example Beta - 02 [720p]",
       .infoHash = "1123456789abcdef0123456789abcdef01234567",
       .downloadUrl = QUrl("magnet:?xt=urn:btih:1123456789abcdef0123456789abcdef01234567"),
       .size = "",
       .seeders = 12,
       .leechers = 1},
      {.id = "gamma",
       .title = "[Group A] Example Gamma - 03 [1080p]",
       .infoHash = "2123456789abcdef0123456789abcdef01234567",
       .downloadUrl = QUrl("magnet:?xt=urn:btih:2123456789abcdef0123456789abcdef01234567"),
       .size = "900 MiB",
       .seeders = 2,
       .leechers = 0},
  };
  UrlReceiver receiver;
  QDesktopServices::setUrlHandler("magnet", &receiver, "open");
  {
    gui::TorrentsWidget widget(nullptr);
    widget.resize(1200, 650);
    widget.show();
    app.processEvents();
    auto* client = widget.findChild<track::TorrentFeedClient*>();
    auto* seadex = widget.findChild<track::SeaDexClient*>();
    auto* feedSelector = widget.findChild<QComboBox*>("torrentFeedSelector");
    auto* table = widget.findChild<QTableWidget*>("torrentTable");
    auto* title = widget.findChild<QLineEdit*>("torrentTitleFilter");
    auto* group = widget.findChild<QLineEdit*>("torrentGroupFilter");
    auto* resolution = widget.findChild<QLineEdit*>("torrentResolutionFilter");
    auto* showArchived = widget.findChild<QCheckBox*>("torrentShowArchived");
    auto* discard = widget.findChild<QAction*>("torrentDiscard");
    auto* restore = widget.findChild<QAction*>("torrentRestore");
    auto* open = widget.findChild<QAction*>("torrentOpen");
    if (!client || !seadex || !feedSelector || !table || !title || !group || !resolution ||
        !showArchived || !discard || !restore || !open) {
      std::fprintf(stderr, "FAIL: torrent widget controls missing\n");
      return 1;
    }
    check(feedSelector->currentText() == settings.feedUrl, "feed selector loads the active feed");
    check(feedSelector->count() >= 2, "feed selector loads the saved feed list");
    client->finished(items);
    seadex->cancel();
    seadex->finished(track::SeaDexReleases{
        {items[0].infoHash.toLower(),
         track::SeaDexRelease{.status = track::SeaDexReleaseStatus::Best,
                              .title = "[Group A] Enriched Alpha - 01 [1080p]",
                              .size = 1234,
                              .infoUrl = QUrl("https://nyaa.si/view/1")}},
        {items[1].infoHash.toLower(),
         track::SeaDexRelease{.status = track::SeaDexReleaseStatus::Alternative,
                              .title = "[Group B] Enriched Beta - 02 [720p]",
                              .size = 5678,
                              .infoUrl = QUrl("https://nyaa.si/view/2")}},
    });
    check(visibleRows(table) == 3, "render RSS results");
    const auto bestRow = titleRow(table, "[Group A] Enriched Alpha - 01 [1080p]");
    const auto alternativeRow = titleRow(table, "[Group B] Enriched Beta - 02 [720p]");
    check(bestRow >= 0 && table->item(bestRow, 0) &&
              table->item(bestRow, 0)->background().color() == QColor(0, 172, 255, 31),
          "SeaDex best release uses the blue highlight");
    check(alternativeRow >= 0 && table->item(alternativeRow, 0) &&
              table->item(alternativeRow, 0)->background().color() == QColor(255, 172, 0, 31),
          "SeaDex alternative release uses the orange highlight");
    check(bestRow >= 0 && table->item(bestRow, 1) &&
              table->item(bestRow, 1)->text() == QStringLiteral("1.2 KiB"),
          "SeaDex file metadata fills the missing size");
    title->setText("alpha");
    check(visibleRows(table) == 1, "title filter is case insensitive");
    title->clear();
    group->setText("Group A");
    check(visibleRows(table) == 2, "release group filter");
    group->clear();
    resolution->setText("1080");
    check(visibleRows(table) == 2, "resolution filter");
    resolution->setText("80");
    check(visibleRows(table) == 0, "resolution does not match a partial number");
    resolution->clear();

    table->sortItems(0, Qt::DescendingOrder);
    auto row = titleRow(table, "[Group B] Enriched Beta - 02 [720p]");
    check(row >= 0, "find beta after sorting");
    table->selectRow(row);
    discard->trigger();
    check(track::loadTorrentArchive().contains("beta"), "discard persists the sorted item ID");
    check(visibleRows(table) == 2, "discarded item is hidden");
    showArchived->setChecked(true);
    check(visibleRows(table) == 3, "show archived results");
    row = titleRow(table, "[Group B] Enriched Beta - 02 [720p]");
    table->clearSelection();
    table->selectRow(row);
    restore->trigger();
    check(!track::loadTorrentArchive().contains("beta"), "restore removes archive entry");
    showArchived->setChecked(false);

    table->clearSelection();
    table->selectRow(titleRow(table, "[Group A] Enriched Alpha - 01 [1080p]"));
    open->trigger();
    // The downloader completes magnets immediately; the UI may queue the next step.
    for (int i = 0; i < 5; ++i) app.processEvents();
    check(receiver.urls.size() == 1 && receiver.urls.front() == items[0].downloadUrl,
          "open routes the selected magnet to an intercepted desktop handler");
    check(track::loadTorrentArchive().contains("alpha"), "successful open is archived");
    check(visibleRows(table) == 2, "opened item is hidden");

    showArchived->setChecked(true);
    app.processEvents();
    if (argc > 1)
      check(widget.grab().save(QString::fromLocal8Bit(argv[1])), "save widget screenshot");
  }
  {
    gui::TorrentsWidget restored(nullptr);
    restored.findChild<track::TorrentFeedClient*>()->finished(items);
    auto* showArchived = restored.findChild<QCheckBox*>("torrentShowArchived");
    showArchived->setChecked(false);
    check(visibleRows(restored.findChild<QTableWidget*>("torrentTable")) == 2,
          "archive survives widget recreation");
  }
  QDesktopServices::unsetUrlHandler("magnet");
  if (!failures) std::puts("Torrent widget tests passed.");
  return failures ? 1 : 0;
}

#include "torrent_widget_test.moc"
