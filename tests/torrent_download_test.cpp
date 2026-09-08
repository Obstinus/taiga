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

#include "track/torrent_download.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

#include "taiga/path.hpp"
#include "track/torrent_settings.hpp"

namespace {

int failures = 0;
QString dataPath;

void check(const bool condition, const QString& message) {
  if (condition) return;
  std::fprintf(stderr, "torrent tests: %s\n", message.toUtf8().constData());
  ++failures;
}

QByteArray bencodeString(const QByteArray& value) {
  return QByteArray::number(value.size()) + ':' + value;
}

QByteArray bencodeInteger(const qint64 value) {
  return 'i' + QByteArray::number(value) + 'e';
}

QByteArray makeSingleFileTorrent() {
  const QByteArray pieces(20, 'a');
  QByteArray info = "d";
  info += bencodeString("length") + bencodeInteger(12345);
  info += bencodeString("name") + bencodeString("episode.mkv");
  info += bencodeString("piece length") + bencodeInteger(16384);
  info += bencodeString("pieces") + bencodeString(pieces);
  info += 'e';

  QByteArray torrent = "d";
  torrent += bencodeString("announce") + bencodeString("https://tracker.test/announce");
  torrent += bencodeString("info") + info;
  torrent += 'e';
  return torrent;
}

QByteArray makeMultiFileTorrent() {
  const auto file = [](const qint64 length, const QByteArray& path) {
    QByteArray value = "d";
    value += bencodeString("length") + bencodeInteger(length);
    value += bencodeString("path") + 'l' + bencodeString(path) + 'e';
    value += 'e';
    return value;
  };

  QByteArray files = "l";
  files += file(123, "part-01.mkv");
  files += file(456, "part-02.mkv");
  files += 'e';

  QByteArray info = "d";
  info += bencodeString("files") + files;
  info += bencodeString("name") + bencodeString("season");
  info += bencodeString("piece length") + bencodeInteger(16384);
  info += bencodeString("pieces") + bencodeString(QByteArray(20, 'b'));
  info += 'e';

  QByteArray torrent = "d";
  torrent += bencodeString("announce") + bencodeString("https://tracker.test/announce");
  torrent += bencodeString("info") + info;
  torrent += 'e';
  return torrent;
}

QByteArray makeTooDeepValue() {
  QByteArray value = 'd' + bencodeString("info") + 'd' + bencodeString("extra");
  value += QByteArray(65, 'l');
  value += bencodeString("x");
  value += QByteArray(65, 'e');
  value += "ee";
  return value;
}

void testMetainfo() {
  check(track::isTorrentMetainfo(makeSingleFileTorrent()),
        QStringLiteral("valid v1 single-file metainfo was rejected"));
  check(track::isTorrentMetainfo(makeMultiFileTorrent()),
        QStringLiteral("valid v1 multi-file metainfo was rejected"));
  check(track::isTorrentMetainfo(
            "d4:infod6:lengthi0e4:name5:empty12:piece lengthi16384e6:pieces0:ee"),
        QStringLiteral("valid empty file metainfo was rejected"));
  check(!track::isTorrentMetainfo(
            "d4:infod6:lengthi1e4:name5:empty12:piece lengthi16384e6:pieces0:ee"),
        QStringLiteral("missing piece hashes were accepted for a nonempty file"));
  check(
      !track::isTorrentMetainfo("d4:infod5:filesle4:name5:empty12:piece lengthi16384e6:pieces0:ee"),
      QStringLiteral("empty multi-file list was accepted"));

  check(!track::isTorrentMetainfo("<html><body>not a torrent</body></html>"),
        QStringLiteral("HTML was accepted as torrent metainfo"));

  QByteArray truncatedInteger = 'd' + bencodeString("info") + 'd';
  truncatedInteger += bencodeString("name") + bencodeString("episode.mkv");
  truncatedInteger += bencodeString("piece length") + "i16384";
  check(!track::isTorrentMetainfo(truncatedInteger),
        QStringLiteral("truncated bencoded integer was accepted"));

  check(!track::isTorrentMetainfo(makeTooDeepValue()),
        QStringLiteral("overly deep bencoded value was accepted"));

  auto duplicateKey = makeSingleFileTorrent();
  const auto name = bencodeString("name") + bencodeString("episode.mkv");
  check(track::isTorrentMetainfo(duplicateKey),
        QStringLiteral("duplicate-key control fixture must be valid"));
  duplicateKey.replace(name, name + bencodeString("name") + bencodeString("episode-copy.mkv"));
  check(!track::isTorrentMetainfo(duplicateKey),
        QStringLiteral("corrupt metainfo with duplicate keys was accepted"));
}

class HttpFixture final {
public:
  HttpFixture(QByteArray body, const int status) : body_(std::move(body)), status_(status) {
    QObject::connect(&server_, &QTcpServer::newConnection, &server_, [this] {
      while (server_.hasPendingConnections()) {
        auto* socket = server_.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);

        QByteArray response = "HTTP/1.1 " + QByteArray::number(status_) + " ";
        response += status_ >= 200 && status_ < 300 ? "OK" : "Error";
        response += "\r\nContent-Type: application/octet-stream\r\nContent-Length: ";
        response += QByteArray::number(body_.size());
        response += "\r\nConnection: close\r\n\r\n";
        response += body_;
        socket->write(response);
        socket->disconnectFromHost();
      }
    });
  }

  bool listen() {
    return server_.listen(QHostAddress::LocalHost, 0);
  }

  QString errorString() const {
    return server_.errorString();
  }

  int serverError() const {
    return static_cast<int>(server_.serverError());
  }

  QUrl url(const QString& path = QStringLiteral("/download.torrent")) const {
    return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(server_.serverPort()).arg(path));
  }

private:
  QTcpServer server_;
  QByteArray body_;
  int status_ = 200;
};

struct DownloadResult {
  bool completed = false;
  bool succeeded = false;
  QString id;
  QUrl url;
  QString error;
};

DownloadResult downloadAndWait(const track::TorrentItem& item, const QString& directory) {
  track::TorrentDownloader downloader;
  DownloadResult result;
  QEventLoop loop;
  QTimer timeout;
  timeout.setSingleShot(true);

  QObject::connect(&downloader, &track::TorrentDownloader::succeeded, &loop,
                   [&](const QString& id, const QUrl& url) {
                     result.completed = true;
                     result.succeeded = true;
                     result.id = id;
                     result.url = url;
                     loop.quit();
                   });
  QObject::connect(&downloader, &track::TorrentDownloader::failed, &loop,
                   [&](const QString& id, const QString& error) {
                     result.completed = true;
                     result.succeeded = false;
                     result.id = id;
                     result.error = error;
                     loop.quit();
                   });
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

  downloader.download(item, directory);
  if (!result.completed) {
    timeout.start(5000);
    loop.exec();
  }
  if (!result.completed) result.error = QStringLiteral("timed out waiting for downloader signal");
  return result;
}

track::TorrentItem makeItem(const QString& id, const QString& title, const QUrl& url) {
  track::TorrentItem item;
  item.id = id;
  item.title = title;
  item.downloadUrl = url;
  return item;
}

QStringList torrentFiles(const QString& directory) {
  return QDir(directory).entryList({QStringLiteral("*.torrent")}, QDir::Files, QDir::Name);
}

void testDownloader() {
  const QByteArray torrent = makeSingleFileTorrent();

  {
    HttpFixture server(torrent, 200);
    QTemporaryDir directory;
    check(directory.isValid(), QStringLiteral("could not create success temporary directory"));
    check(server.listen(),
          QStringLiteral("could not start success HTTP fixture (%1): ").arg(server.serverError()) +
              server.errorString());
    const auto result = downloadAndWait(
        makeItem(QStringLiteral("success"), QStringLiteral("A: title/episode"), server.url()),
        directory.path());
    check(result.completed && result.succeeded && result.id == QStringLiteral("success"),
          QStringLiteral("successful torrent download did not emit succeeded"));
    check(result.url.isLocalFile(), QStringLiteral("successful download URL is not local"));
    const auto files = torrentFiles(directory.path());
    check(files.size() == 1, QStringLiteral("successful download did not save one torrent file"));
    if (files.size() == 1) {
      QFile saved(QDir(directory.path()).filePath(files.front()));
      check(saved.open(QIODevice::ReadOnly), QStringLiteral("saved torrent could not be opened"));
      check(saved.readAll() == torrent, QStringLiteral("saved torrent bytes differ from response"));
    }
    const auto endpointResult =
        downloadAndWait(makeItem(QStringLiteral("endpoint"), QStringLiteral("A: title/episode"),
                                 server.url(QStringLiteral("/download.php?id=42"))),
                        directory.path());
    check(endpointResult.completed && endpointResult.succeeded,
          QStringLiteral("torrent enclosure endpoint without .torrent suffix was rejected"));
    check(torrentFiles(directory.path()).size() == 2 && endpointResult.url != result.url,
          QStringLiteral("downloading the same title overwrote an existing file"));
  }

  {
    HttpFixture server("<html>server error</html>", 500);
    QTemporaryDir directory;
    check(directory.isValid(), QStringLiteral("could not create HTTP error temporary directory"));
    check(server.listen(),
          QStringLiteral("could not start HTTP error fixture (%1): ").arg(server.serverError()) +
              server.errorString());
    const auto result = downloadAndWait(
        makeItem(QStringLiteral("http-error"), QStringLiteral("HTTP error"), server.url()),
        directory.path());
    check(result.completed && !result.succeeded && result.id == QStringLiteral("http-error") &&
              !result.error.isEmpty(),
          QStringLiteral("HTTP error did not emit failed with an error"));
    check(torrentFiles(directory.path()).isEmpty(),
          QStringLiteral("HTTP error incorrectly saved a torrent file"));
  }

  {
    HttpFixture server("<html>not a torrent</html>", 200);
    QTemporaryDir directory;
    check(directory.isValid(), QStringLiteral("could not create HTML temporary directory"));
    check(server.listen(),
          QStringLiteral("could not start HTML fixture (%1): ").arg(server.serverError()) +
              server.errorString());
    const auto result = downloadAndWait(
        makeItem(QStringLiteral("html-error"), QStringLiteral("HTML error"), server.url()),
        directory.path());
    check(result.completed && !result.succeeded && result.id == QStringLiteral("html-error") &&
              !result.error.isEmpty(),
          QStringLiteral("HTML response did not emit failed with an error"));
    check(torrentFiles(directory.path()).isEmpty(),
          QStringLiteral("HTML response incorrectly saved a torrent file"));
  }

  {
    QTemporaryDir directory;
    const auto magnet = QUrl(
        QStringLiteral("magnet:?xt=urn:btih:0123456789ABCDEF0123456789ABCDEF01234567&dn=Test"));
    const auto result = downloadAndWait(
        makeItem(QStringLiteral("magnet"), QStringLiteral("Magnet"), magnet), directory.path());
    check(result.completed && result.succeeded && result.id == QStringLiteral("magnet") &&
              result.url == magnet,
          QStringLiteral("magnet link did not emit succeeded without network access"));
    check(torrentFiles(directory.path()).isEmpty(),
          QStringLiteral("magnet link unexpectedly saved a torrent file"));
  }
}

QString stateFilePath() {
  return QDir(dataPath).filePath(QStringLiteral("torrents.json"));
}

bool writeStateBytes(const QByteArray& bytes) {
  if (!QDir().mkpath(dataPath)) return false;
  QFile file(stateFilePath());
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
  return file.write(bytes) == bytes.size();
}

bool readStateBytes(QByteArray& bytes) {
  QFile file(stateFilePath());
  if (!file.open(QIODevice::ReadOnly)) return false;
  bytes = file.readAll();
  return true;
}

void testSettings() {
  QTemporaryDir directory;
  check(directory.isValid(), QStringLiteral("could not create settings temporary directory"));
  dataPath = directory.path();

  track::TorrentSettings expected;
  expected.feedUrl = QStringLiteral("https://feed.example.test/rss");
  expected.feedUrls = {expected.feedUrl, QStringLiteral("https://releases.moe/rss")};
  expected.searchUrl = QStringLiteral("https://feed.example.test/search?q=%title%");
  expected.downloadDirectory = QDir(directory.path()).filePath(QStringLiteral("downloads"));
  expected.titleFilter = QStringLiteral("My title");
  expected.releaseGroup = QStringLiteral("[Group]");
  expected.resolution = QStringLiteral("1080p");
  expected.hideArchived = false;
  expected.autoRefresh = true;
  expected.refreshMinutes = 77;

  QString error;
  check(track::saveTorrentSettings(expected, &error),
        QStringLiteral("valid torrent settings could not be saved: ") + error);
  const auto actual = track::loadTorrentSettings();
  check(actual.feedUrl == expected.feedUrl && actual.feedUrls == expected.feedUrls &&
            actual.searchUrl == expected.searchUrl &&
            actual.downloadDirectory == expected.downloadDirectory &&
            actual.titleFilter == expected.titleFilter &&
            actual.releaseGroup == expected.releaseGroup &&
            actual.resolution == expected.resolution &&
            actual.hideArchived == expected.hideArchived &&
            actual.autoRefresh == expected.autoRefresh &&
            actual.refreshMinutes == expected.refreshMinutes,
        QStringLiteral("torrent settings roundtrip changed values"));

  const auto legacy = QByteArrayLiteral(
      R"json({"settings":{"feedUrl":"https://legacy.example.test/rss","searchUrl":"https://legacy.example.test/search?q=%title%","downloadDirectory":"/tmp/legacy-torrents"}})json");
  check(writeStateBytes(legacy), QStringLiteral("could not write legacy settings fixture"));
  const auto migrated = track::loadTorrentSettings();
  check(migrated.feedUrl == QStringLiteral("https://legacy.example.test/rss") &&
            migrated.feedUrls == QStringList{QStringLiteral("https://legacy.example.test/rss")},
        QStringLiteral("legacy single-feed settings were not migrated to a feed list"));

  check(track::saveTorrentSettings(expected, &error),
        QStringLiteral("torrent settings could not be restored after migration: ") + error);

  error.clear();
  check(
      track::saveTorrentArchive(
          {QStringLiteral("one"), QStringLiteral("one"), QStringLiteral("two"), QString()}, &error),
      QStringLiteral("torrent archive could not be saved: ") + error);
  check(track::loadTorrentArchive() == QStringList{QStringLiteral("one"), QStringLiteral("two")},
        QStringLiteral("torrent archive roundtrip did not deduplicate/filter IDs"));
  check(track::loadTorrentSettings().feedUrl == expected.feedUrl &&
            track::loadTorrentSettings().feedUrls == expected.feedUrls,
        QStringLiteral("saving torrent archive did not preserve torrent settings"));

  const QByteArray invalid = "{ this is not valid JSON";
  check(writeStateBytes(invalid), QStringLiteral("could not write invalid torrent state fixture"));
  error.clear();
  check(!track::saveTorrentSettings(expected, &error) && !error.isEmpty(),
        QStringLiteral("invalid JSON state was accepted by settings save"));
  QByteArray after;
  check(readStateBytes(after) && after == invalid,
        QStringLiteral("invalid JSON state was overwritten by settings save"));

  error.clear();
  check(!track::saveTorrentArchive({QStringLiteral("new-id")}, &error) && !error.isEmpty(),
        QStringLiteral("invalid JSON state was accepted by archive save"));
  check(readStateBytes(after) && after == invalid,
        QStringLiteral("invalid JSON state was overwritten by archive save"));

  dataPath.clear();
}

}  // namespace

namespace taiga {

std::string get_data_path() {
  return dataPath.toStdString();
}

}  // namespace taiga

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  testMetainfo();
  testDownloader();
  testSettings();
  if (failures != 0) {
    std::fprintf(stderr, "torrent tests: %d check(s) failed\n", failures);
    return EXIT_FAILURE;
  }
  std::puts("Torrent tests passed");
  return EXIT_SUCCESS;
}
