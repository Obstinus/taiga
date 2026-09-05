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

#include "track/torrent_feed.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <cstdio>
#include <utility>

namespace {

int failures = 0;

void check(const bool condition, const QString& message) {
  if (condition) return;
  std::fprintf(stderr, "torrent feed tests: %s\n", message.toUtf8().constData());
  ++failures;
}

QByteArray validFeed() {
  return R"xml(<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0" xmlns:nyaa="https://nyaa.si/xmlns/nyaa">
  <channel>
    <title>Example</title>
    <item>
      <title>[Group] Episode &amp; One 1080p</title>
      <link>/view/42</link>
      <guid isPermaLink="false">episode-42</guid>
      <pubDate>Wed, 02 Oct 2002 08:00:00 GMT</pubDate>
      <nyaa:size>1.2 GiB</nyaa:size>
      <nyaa:seeders>12</nyaa:seeders>
      <nyaa:leechers>3</nyaa:leechers>
      <nyaa:infoHash>0123456789abcdef0123456789abcdef01234567</nyaa:infoHash>
    </item>
    <item>
      <title>Enclosure</title>
      <link>https://tracker.test/view/43</link>
      <guid>episode-43</guid>
      <enclosure url="/api/download.php?id=43" length="1234"
                 type="application/x-bittorrent" />
    </item>
    <item>
      <title>Duplicate</title>
      <link>https://tracker.test/duplicate.torrent</link>
      <guid>episode-42</guid>
    </item>
  </channel>
</rss>)xml";
}

void testHelpers() {
  check(track::isTorrentDownloadUrl(QUrl("https://tracker.test/file.torrent")),
        QStringLiteral(".torrent URL was rejected"));
  check(track::isTorrentDownloadUrl(
            QUrl("magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567")),
        QStringLiteral("valid magnet URL was rejected"));
  check(track::isTorrentDownloadUrl(QUrl("magnet:?xt=urn:btih:not-a-valid-info-hash")) == false,
        QStringLiteral("invalid magnet URL was accepted"));
  check(!track::isTorrentDownloadUrl(QUrl("file:///tmp/file.torrent")),
        QStringLiteral("file URL was accepted as a download"));
  check(track::isTorrentDownloadUrl(
            QUrl(QStringLiteral("magnet:?xt=urn:btmh:1220") + QString(64, QLatin1Char('a')))),
        QStringLiteral("valid BitTorrent v2 magnet URL was rejected"));

  const auto search = track::torrentSearchUrl(QStringLiteral("https://tracker.test/?q=%title%"),
                                              QStringLiteral("A & B + 100% #日本"));
  check(search.isValid() &&
            search.toString(QUrl::FullyEncoded) ==
                QStringLiteral(
                    "https://tracker.test/?q=A%20%26%20B%20%2B%20100%25%20%23%E6%97%A5%E6%9C%AC"),
        QStringLiteral("search query was not encoded in the URL"));
}

void testParser() {
  const auto result = track::parseTorrentFeed(validFeed(), QUrl("https://tracker.test/rss"));
  check(result.error.isEmpty(), QStringLiteral("valid RSS feed returned an error"));
  check(result.items.size() == 2, QStringLiteral("RSS items were not filtered and deduplicated"));
  if (result.items.size() != 2) return;

  const auto& magnet = result.items.front();
  check(magnet.title == QStringLiteral("[Group] Episode & One 1080p"),
        QStringLiteral("RSS title/entity was not decoded"));
  check(magnet.downloadUrl.scheme() == QStringLiteral("magnet"),
        QStringLiteral("nyaa infoHash did not produce a magnet URL"));
  check(magnet.infoUrl == QUrl("https://tracker.test/view/42"),
        QStringLiteral("relative RSS link was not resolved as info URL"));
  check(magnet.size == QStringLiteral("1.2 GiB") && magnet.seeders == 12 && magnet.leechers == 3,
        QStringLiteral("namespaced torrent metadata was not parsed"));
  check(magnet.published.isValid(), QStringLiteral("RSS publication date was not parsed"));

  const auto& enclosure = result.items.back();
  check(enclosure.downloadUrl == QUrl("https://tracker.test/api/download.php?id=43"),
        QStringLiteral("torrent enclosure URL was not resolved"));
  check(enclosure.infoUrl == QUrl("https://tracker.test/view/43"),
        QStringLiteral("enclosure item info URL was not retained"));
  check(enclosure.size == QStringLiteral("1234"),
        QStringLiteral("enclosure length was not used as a size fallback"));

  const auto malformed = track::parseTorrentFeed(validFeed() + QByteArrayLiteral("<broken>"),
                                                 QUrl("https://tracker.test/rss"));
  check(!malformed.error.isEmpty() && malformed.items.isEmpty(),
        QStringLiteral("malformed XML returned partial torrent results"));

  auto duplicateHashFeed = validFeed();
  duplicateHashFeed.replace(
      "</channel>",
      "<item><title>Another GUID, same torrent</title><guid>another-guid</guid>"
      "<nyaa:infoHash>0123456789abcdef0123456789abcdef01234567</nyaa:infoHash>"
      "</item></channel>");
  const auto duplicateHash =
      track::parseTorrentFeed(duplicateHashFeed, QUrl("https://tracker.test/rss"));
  check(duplicateHash.error.isEmpty() && duplicateHash.items.size() == 2,
        QStringLiteral("same info hash produced duplicate row IDs"));
  const auto otherSource = track::parseTorrentFeed(validFeed(), QUrl("https://other.test/rss"));
  check(otherSource.items.size() == 2 && otherSource.items.back().id != enclosure.id,
        QStringLiteral("unrelated feed origins share arbitrary GUID archive IDs"));
}

class HttpFixture final {
public:
  HttpFixture(QByteArray body, const int status) : body_(std::move(body)), status_(status) {
    QObject::connect(&server_, &QTcpServer::newConnection, &server_, [this] {
      while (server_.hasPendingConnections()) {
        auto* socket = server_.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);

        QByteArray response = "HTTP/1.1 " + QByteArray::number(status_);
        response += status_ >= 200 && status_ < 300 ? " OK\r\n" : " Error\r\n";
        response += "Content-Type: application/rss+xml\r\nContent-Length: ";
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

  bool isListening() const {
    return server_.isListening();
  }

  QUrl url() const {
    return QUrl(QStringLiteral("http://127.0.0.1:%1/feed").arg(server_.serverPort()));
  }

private:
  QTcpServer server_;
  QByteArray body_;
  int status_ = 200;
};

void testClient() {
  HttpFixture server(validFeed(), 200);
  check(server.listen(), QStringLiteral("could not start local HTTP fixture"));
  if (!server.isListening()) return;

  track::TorrentFeedClient client;
  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);
  bool completed = false;
  bool succeeded = false;
  int completions = 0;
  QList<track::TorrentItem> received;
  QString error;
  QObject::connect(&client, &track::TorrentFeedClient::finished, &loop,
                   [&](const QList<track::TorrentItem>& items) {
                     completed = true;
                     ++completions;
                     succeeded = true;
                     received = items;
                     loop.quit();
                   });
  QObject::connect(&client, &track::TorrentFeedClient::failed, &loop, [&](const QString& message) {
    completed = true;
    ++completions;
    error = message;
    loop.quit();
  });
  QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

  client.fetch(server.url());
  timer.start(2'000);
  loop.exec();
  check(completed && succeeded && error.isEmpty() && received.size() == 2,
        QStringLiteral("asynchronous RSS fetch did not finish successfully"));

  HttpFixture errorServer(QByteArrayLiteral("server error"), 500);
  check(errorServer.listen(), QStringLiteral("could not start HTTP error fixture"));
  if (!errorServer.isListening()) return;

  completed = false;
  succeeded = false;
  error.clear();
  client.fetch(errorServer.url());
  timer.start(2'000);
  loop.exec();
  check(completed && !succeeded && !error.isEmpty(),
        QStringLiteral("HTTP error did not emit failed"));

  completed = false;
  client.fetch(server.url());
  client.cancel();
  timer.start(30);
  loop.exec();
  check(!completed, QStringLiteral("canceled request emitted a stale result"));

  completed = false;
  succeeded = false;
  error.clear();
  const auto beforeReplacement = completions;
  client.fetch(server.url());
  client.fetch(errorServer.url());
  timer.start(2'000);
  loop.exec();
  check(completed && !succeeded && completions == beforeReplacement + 1,
        QStringLiteral("superseded request replaced the current result"));

  HttpFixture oversizedServer(QByteArray(8 * 1024 * 1024 + 1, 'x'), 200);
  check(oversizedServer.listen(), QStringLiteral("could not start oversized response fixture"));
  if (!oversizedServer.isListening()) return;
  completed = false;
  succeeded = false;
  error.clear();
  client.fetch(oversizedServer.url());
  timer.start(2'000);
  loop.exec();
  check(completed && !succeeded && error.contains("too large"),
        QStringLiteral("oversized feed was not rejected at the response limit"));

  completed = false;
  error.clear();
  client.fetch(QUrl("file:///tmp/feed.xml"));
  check(completed && !succeeded && !error.isEmpty(),
        QStringLiteral("invalid file URL did not emit failed"));
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  testHelpers();
  testParser();
  testClient();

  if (failures == 0) std::puts("Torrent feed tests passed.");
  return failures == 0 ? 0 : 1;
}
