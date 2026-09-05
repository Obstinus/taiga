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

#include "torrent_feed.hpp"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QTimer>
#include <QUrlQuery>
#include <QXmlStreamReader>
#include <algorithm>
#include <utility>

namespace track {

namespace {

constexpr qint64 kMaximumResponseBytes = 8LL * 1024 * 1024;
constexpr int kRequestTimeoutMilliseconds = 10'000;
constexpr int kMaximumRedirects = 5;

bool isHttpUrl(const QUrl& url) {
  if (!url.isValid() || url.host().isEmpty()) return false;

  const auto scheme = url.scheme();
  return scheme.compare(QStringLiteral("http"), Qt::CaseInsensitive) == 0 ||
         scheme.compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0;
}

bool isName(const QString& name, const QString& expected) {
  return name.compare(expected, Qt::CaseInsensitive) == 0;
}

QUrl resolveUrl(const QString& value, const QUrl& source) {
  const auto text = value.trimmed();
  if (text.isEmpty()) return {};

  QUrl url{text, QUrl::StrictMode};
  if (!url.isValid()) return {};

  if (url.isRelative()) {
    if (!source.isValid()) return {};
    url = source.resolved(url);
  }

  return url.isValid() ? url : QUrl{};
}

QString readElementText(QXmlStreamReader& xml) {
  return xml.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
}

int parseCount(const QString& text) {
  bool ok = false;
  const int count = text.trimmed().toInt(&ok);
  return ok && count >= 0 ? count : -1;
}

QDateTime parseDate(const QString& text) {
  auto value = text.trimmed();
  if (value.isEmpty()) return {};

  // Qt's RFC 2822 parser intentionally accepts numeric offsets but not the
  // GMT/UTC abbreviations commonly emitted by RSS producers.
  for (const auto& suffix : {QStringLiteral(" GMT"), QStringLiteral(" UTC")}) {
    if (value.endsWith(suffix, Qt::CaseInsensitive)) {
      value.chop(suffix.size());
      value += QStringLiteral(" +0000");
      break;
    }
  }

  for (const auto format : {Qt::RFC2822Date, Qt::ISODateWithMs, Qt::ISODate, Qt::TextDate}) {
    const auto date = QDateTime::fromString(value, format);
    if (date.isValid()) return date;
  }

  return {};
}

QString normalizedInfoHash(const QString& text) {
  auto hash = text.trimmed();
  const auto prefix = QStringLiteral("urn:btih:");
  if (hash.startsWith(prefix, Qt::CaseInsensitive)) hash = hash.sliced(prefix.size());

  if (hash.size() == 40 && std::ranges::all_of(hash, [](const QChar c) {
        return (c >= QChar{'0'} && c <= QChar{'9'}) || (c >= QChar{'a'} && c <= QChar{'f'}) ||
               (c >= QChar{'A'} && c <= QChar{'F'});
      })) {
    return hash.toUpper();
  }

  // A BitTorrent v1 info hash may also be represented as a 32-character
  // base32 value in a magnet URI.
  if (hash.size() == 32 && std::ranges::all_of(hash, [](const QChar c) {
        const auto upper = c.toUpper();
        return (upper >= QChar{'A'} && upper <= QChar{'Z'}) ||
               (upper >= QChar{'2'} && upper <= QChar{'7'});
      })) {
    return hash.toUpper();
  }

  return {};
}

bool isHex(const QString& value) {
  return !value.isEmpty() && std::ranges::all_of(value, [](const QChar c) {
    return (c >= QChar{'0'} && c <= QChar{'9'}) || (c >= QChar{'a'} && c <= QChar{'f'}) ||
           (c >= QChar{'A'} && c <= QChar{'F'});
  });
}

bool isValidMagnetXt(const QString& value) {
  const auto text = value.trimmed();
  const auto btihPrefix = QStringLiteral("urn:btih:");
  if (text.startsWith(btihPrefix, Qt::CaseInsensitive)) {
    return !normalizedInfoHash(text.sliced(btihPrefix.size())).isEmpty();
  }

  // BitTorrent v2 uses a multihash in the form urn:btmh:1220<sha256>.
  const auto btmhPrefix = QStringLiteral("urn:btmh:");
  if (!text.startsWith(btmhPrefix, Qt::CaseInsensitive)) return false;

  const auto multihash = text.sliced(btmhPrefix.size());
  return multihash.size() == 68 &&
         multihash.startsWith(QStringLiteral("1220"), Qt::CaseInsensitive) && isHex(multihash);
}

bool isValidMagnet(const QUrl& url) {
  if (!url.isValid() || url.scheme().compare(QStringLiteral("magnet"), Qt::CaseInsensitive) != 0)
    return false;

  const QUrlQuery query{url};
  for (const auto& [key, value] : query.queryItems(QUrl::FullyDecoded)) {
    if (key.compare(QStringLiteral("xt"), Qt::CaseInsensitive) == 0 && isValidMagnetXt(value)) {
      return true;
    }
  }
  return false;
}

bool looksLikeTorrentLink(const QUrl& url) {
  if (!url.isValid()) return false;
  if (url.scheme().compare(QStringLiteral("magnet"), Qt::CaseInsensitive) == 0) {
    return isValidMagnet(url);
  }
  if (!isHttpUrl(url)) return false;

  if (url.path(QUrl::FullyDecoded).endsWith(QStringLiteral(".torrent"), Qt::CaseInsensitive)) {
    return true;
  }

  // A number of trackers use a download endpoint whose path has no .torrent
  // suffix but put the filename or format in the query string.
  const QUrlQuery query{url};
  for (const auto& [key, value] : query.queryItems(QUrl::FullyDecoded)) {
    const auto lowerKey = key.toLower();
    const auto lowerValue = value.trimmed().toLower();
    if ((lowerKey == QStringLiteral("file") || lowerKey == QStringLiteral("filename") ||
         lowerKey == QStringLiteral("name")) &&
        lowerValue.endsWith(QStringLiteral(".torrent"))) {
      return true;
    }
    if ((lowerKey == QStringLiteral("format") || lowerKey == QStringLiteral("type")) &&
        lowerValue.contains(QStringLiteral("torrent"))) {
      return true;
    }
  }

  return false;
}

QUrl magnetUrl(const QString& infoHash, const QString& title) {
  if (infoHash.isEmpty()) return {};

  QUrl magnet{QStringLiteral("magnet:")};
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("xt"), QStringLiteral("urn:btih:") + infoHash);
  if (!title.isEmpty()) query.addQueryItem(QStringLiteral("dn"), title);
  magnet.setQuery(query);
  return magnet;
}

bool hasTorrentMimeType(const QUrl& url, const QString& type) {
  if (!isHttpUrl(url)) return false;
  const auto mime = type.trimmed().toLower();
  return mime.contains(QStringLiteral("bittorrent")) ||
         mime == QStringLiteral("application/torrent") ||
         mime == QStringLiteral("application/x-torrent");
}

struct RawTorrentItem {
  QString title;
  QString link;
  QString guid;
  QString enclosureUrl;
  QString enclosureLength;
  QString enclosureType;
  QString size;
  QString infoHash;
  QString seeders;
  QString leechers;
  QString pubDate;
  QString fallbackDate;
};

void appendRawItem(const RawTorrentItem& raw, const QUrl& source, QList<TorrentItem>& items,
                   QSet<QString>& ids) {
  const auto link = resolveUrl(raw.link, source);
  const auto enclosure = resolveUrl(raw.enclosureUrl, source);
  const auto infoHash = normalizedInfoHash(raw.infoHash);

  TorrentItem item;
  item.title = raw.title;
  item.size = raw.size.isEmpty() ? raw.enclosureLength : raw.size;
  item.seeders = parseCount(raw.seeders);
  item.leechers = parseCount(raw.leechers);
  item.published = parseDate(raw.pubDate.isEmpty() ? raw.fallbackDate : raw.pubDate);

  if (looksLikeTorrentLink(enclosure) || hasTorrentMimeType(enclosure, raw.enclosureType)) {
    item.downloadUrl = enclosure;
  } else if (looksLikeTorrentLink(link)) {
    item.downloadUrl = link;
  } else if (!infoHash.isEmpty()) {
    item.downloadUrl = magnetUrl(infoHash, item.title);
  }

  // RSS links conventionally point at the item's web page.  A link that is
  // itself a torrent is used only as the download target.
  if (isHttpUrl(link) && !looksLikeTorrentLink(link)) item.infoUrl = link;

  if (item.infoUrl.isEmpty()) {
    const auto guidUrl = resolveUrl(raw.guid, source);
    if (isHttpUrl(guidUrl) && !looksLikeTorrentLink(guidUrl)) item.infoUrl = guidUrl;
  }

  // Items without an enclosure, magnet, or torrent link are ordinary feed
  // entries and are not useful to a torrent picker.
  if (item.downloadUrl.isEmpty()) return;

  const auto normalizedGuid = raw.guid.trimmed();
  QString origin;
  if (source.isValid() && !source.host().isEmpty()) {
    QUrl sourceOrigin;
    sourceOrigin.setScheme(source.scheme().toLower());
    sourceOrigin.setHost(source.host().toLower());
    sourceOrigin.setPort(source.port());
    origin = sourceOrigin.toString(QUrl::FullyEncoded);
  }
  if (origin.isEmpty()) origin = source.toString(QUrl::FullyEncoded);

  const auto sourceGuid =
      normalizedGuid.isEmpty() ? QString{} : origin + QChar{0x1f} + normalizedGuid;
  const auto dedupId =
      sourceGuid.isEmpty()
          ? (infoHash.isEmpty() ? item.downloadUrl.toString(QUrl::FullyEncoded) : infoHash)
          : sourceGuid;
  if (dedupId.isEmpty() || ids.contains(dedupId)) return;

  // Info hashes are globally stable.  GUIDs are only scoped to the feed
  // origin because arbitrary GUID strings are often reused by different
  // trackers.
  item.id = infoHash.isEmpty() ? dedupId : infoHash;
  if (item.id.isEmpty()) item.id = item.infoUrl.toString(QUrl::FullyEncoded);
  if (item.id.isEmpty())
    item.id = item.title + QChar{0x1f} + item.published.toString(Qt::ISODateWithMs);

  if (ids.contains(item.id)) return;
  ids.insert(dedupId);
  ids.insert(item.id);
  items.push_back(std::move(item));
}

bool parseItem(QXmlStreamReader& xml, const QUrl& source, QList<TorrentItem>& items,
               QSet<QString>& ids) {
  RawTorrentItem raw;

  while (xml.readNextStartElement()) {
    const auto name = xml.name().toString();

    if (isName(name, QStringLiteral("title"))) {
      if (raw.title.isEmpty()) raw.title = readElementText(xml);
    } else if (isName(name, QStringLiteral("link"))) {
      const auto attributes = xml.attributes();
      auto value = readElementText(xml);
      if (value.isEmpty()) value = attributes.value(QStringLiteral("href")).toString().trimmed();
      if (raw.link.isEmpty()) raw.link = value;
    } else if (isName(name, QStringLiteral("guid"))) {
      if (raw.guid.isEmpty()) raw.guid = readElementText(xml);
    } else if (isName(name, QStringLiteral("enclosure"))) {
      const auto attributes = xml.attributes();
      if (raw.enclosureUrl.isEmpty()) {
        raw.enclosureUrl = attributes.value(QStringLiteral("url")).toString().trimmed();
      }
      if (raw.enclosureLength.isEmpty()) {
        raw.enclosureLength = attributes.value(QStringLiteral("length")).toString().trimmed();
      }
      if (raw.enclosureType.isEmpty()) {
        raw.enclosureType = attributes.value(QStringLiteral("type")).toString().trimmed();
      }
      xml.skipCurrentElement();
    } else if (isName(name, QStringLiteral("size"))) {
      if (raw.size.isEmpty()) raw.size = readElementText(xml);
    } else if (isName(name, QStringLiteral("infoHash")) ||
               isName(name, QStringLiteral("infohash"))) {
      if (raw.infoHash.isEmpty()) raw.infoHash = readElementText(xml);
    } else if (isName(name, QStringLiteral("seeders"))) {
      if (raw.seeders.isEmpty()) raw.seeders = readElementText(xml);
    } else if (isName(name, QStringLiteral("leechers"))) {
      if (raw.leechers.isEmpty()) raw.leechers = readElementText(xml);
    } else if (isName(name, QStringLiteral("pubDate")) ||
               isName(name, QStringLiteral("published"))) {
      if (raw.pubDate.isEmpty()) raw.pubDate = readElementText(xml);
    } else if (isName(name, QStringLiteral("date"))) {
      if (raw.fallbackDate.isEmpty()) raw.fallbackDate = readElementText(xml);
    } else {
      xml.skipCurrentElement();
    }
  }

  if (xml.hasError()) return false;
  appendRawItem(raw, source, items, ids);
  return true;
}

bool parseChannel(QXmlStreamReader& xml, const QUrl& source, QList<TorrentItem>& items,
                  QSet<QString>& ids) {
  while (xml.readNextStartElement()) {
    if (isName(xml.name().toString(), QStringLiteral("item"))) {
      if (!parseItem(xml, source, items, ids)) return false;
    } else {
      xml.skipCurrentElement();
    }
  }

  return !xml.hasError();
}

TorrentFeedResult parseError(const QString& error) {
  return TorrentFeedResult{{}, error.isEmpty() ? QStringLiteral("Invalid torrent feed") : error};
}

QString replyErrorString(const QNetworkReply& reply) {
  const auto error = reply.errorString().trimmed();
  return error.isEmpty() ? QStringLiteral("Torrent feed request failed") : error;
}

}  // namespace

bool isTorrentDownloadUrl(const QUrl& url) {
  if (!url.isValid()) return false;
  if (url.scheme().compare(QStringLiteral("magnet"), Qt::CaseInsensitive) == 0) {
    return isValidMagnet(url);
  }
  if (!isHttpUrl(url)) return false;
  return url.path(QUrl::FullyDecoded).endsWith(QStringLiteral(".torrent"), Qt::CaseInsensitive);
}

TorrentFeedResult parseTorrentFeed(const QByteArray& data, const QUrl& source) {
  QXmlStreamReader xml{data};
  QList<TorrentItem> items;
  QSet<QString> ids;

  if (!xml.readNextStartElement()) {
    return parseError(xml.hasError() ? xml.errorString() : QStringLiteral("Torrent feed is empty"));
  }

  const auto rootName = xml.name().toString();
  const bool isRss = isName(rootName, QStringLiteral("rss"));
  const bool isRdf = isName(rootName, QStringLiteral("RDF"));
  if (!isRss && !isRdf) {
    return parseError(QStringLiteral("Torrent feed root must be RSS"));
  }

  bool hasChannel = false;
  while (xml.readNextStartElement()) {
    const auto name = xml.name().toString();
    if (isName(name, QStringLiteral("channel"))) {
      hasChannel = true;
      if (!parseChannel(xml, source, items, ids)) {
        return parseError(xml.errorString());
      }
    } else if (isRdf && isName(name, QStringLiteral("item"))) {
      if (!parseItem(xml, source, items, ids)) return parseError(xml.errorString());
    } else {
      xml.skipCurrentElement();
    }
  }

  if (xml.hasError()) return parseError(xml.errorString());
  if (!hasChannel) return parseError(QStringLiteral("Torrent RSS feed has no channel"));

  // Consume the remainder so QXmlStreamReader can report a second root
  // element or other trailing malformed XML.  Parsed items are returned only
  // after this complete-document check succeeds.
  while (!xml.atEnd()) {
    const auto token = xml.readNext();
    if (token == QXmlStreamReader::StartElement) {
      return parseError(QStringLiteral("Torrent feed contains multiple root elements"));
    }
  }
  if (xml.hasError()) return parseError(xml.errorString());

  return TorrentFeedResult{std::move(items), {}};
}

QUrl torrentSearchUrl(const QString& urlTemplate, const QString& query) {
  auto text = urlTemplate.trimmed();
  if (!text.contains(QStringLiteral("%title%"))) return {};
  text.replace(QStringLiteral("%title%"), QString::fromLatin1(QUrl::toPercentEncoding(query)));
  const QUrl url{text, QUrl::StrictMode};
  if (!isHttpUrl(url)) return {};
  return url;
}

TorrentFeedClient::TorrentFeedClient(QObject* parent) : QObject{parent} {
  manager_ = new QNetworkAccessManager{this};
  manager_->setTransferTimeout(kRequestTimeoutMilliseconds);

  timeout_ = new QTimer{this};
  timeout_->setSingleShot(true);
  connect(timeout_, &QTimer::timeout, this, [this] {
    if (reply_) failActiveRequest(QStringLiteral("Torrent feed request timed out"));
  });

  qRegisterMetaType<TorrentItem>();
  qRegisterMetaType<QList<TorrentItem>>();
}

TorrentFeedClient::~TorrentFeedClient() {
  cancel();
}

void TorrentFeedClient::fetch(const QUrl& url) {
  cancel();

  if (!isHttpUrl(url)) {
    emit failed(QStringLiteral("Torrent feed URL must use http or https"));
    return;
  }

  QNetworkRequest request{url};
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setMaximumRedirectsAllowed(kMaximumRedirects);
  request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Taiga TorrentFeedClient"));

  response_.clear();
  const auto generation = generation_;
  reply_ = manager_->get(request);
  reply_->setReadBufferSize(kMaximumResponseBytes + 1);
  auto* reply = reply_;

  connect(reply, &QNetworkReply::metaDataChanged, this, [this, reply, generation] {
    if (reply != reply_ || generation != generation_) return;
    checkResponseSize(reply);
  });
  connect(reply, &QNetworkReply::readyRead, this,
          [this, reply, generation] { handleReadyRead(reply, generation); });
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, generation] { handleFinished(reply, generation); });

  timeout_->start(kRequestTimeoutMilliseconds);
}

void TorrentFeedClient::cancel() {
  ++generation_;
  timeout_->stop();
  response_.clear();

  if (!reply_) return;

  auto* reply = std::exchange(reply_, nullptr);
  reply->abort();
  reply->deleteLater();
}

void TorrentFeedClient::failActiveRequest(const QString& error) {
  if (!reply_) return;

  ++generation_;
  timeout_->stop();
  response_.clear();

  auto* reply = std::exchange(reply_, nullptr);
  reply->abort();
  reply->deleteLater();
  emit failed(error.isEmpty() ? QStringLiteral("Torrent feed request failed") : error);
}

bool TorrentFeedClient::checkResponseSize(QNetworkReply* reply) {
  if (!reply || reply != reply_) return false;

  bool ok = false;
  const auto length = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong(&ok);
  if (ok && length > kMaximumResponseBytes) {
    failActiveRequest(QStringLiteral("Torrent feed response is too large"));
    return false;
  }

  return true;
}

void TorrentFeedClient::handleReadyRead(QNetworkReply* reply, quint64 generation) {
  if (reply != reply_ || generation != generation_) return;
  if (!checkResponseSize(reply)) return;

  const auto chunk = reply->read(kMaximumResponseBytes + 1 - response_.size());
  if (response_.size() + chunk.size() > kMaximumResponseBytes) {
    failActiveRequest(QStringLiteral("Torrent feed response is too large"));
    return;
  }
  response_.append(chunk);
}

void TorrentFeedClient::handleFinished(QNetworkReply* reply, quint64 generation) {
  if (reply != reply_ || generation != generation_) return;
  if (!checkResponseSize(reply)) return;

  const auto tail = reply->read(kMaximumResponseBytes + 1 - response_.size());
  if (response_.size() + tail.size() > kMaximumResponseBytes) {
    failActiveRequest(QStringLiteral("Torrent feed response is too large"));
    return;
  }
  response_.append(tail);

  if (reply->error() != QNetworkReply::NoError) {
    failActiveRequest(replyErrorString(*reply));
    return;
  }

  const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
  bool statusOk = false;
  const auto statusCode = status.toInt(&statusOk);
  if (statusOk && (statusCode < 200 || statusCode >= 300)) {
    failActiveRequest(QStringLiteral("Torrent feed request returned HTTP %1").arg(statusCode));
    return;
  }

  if (!isHttpUrl(reply->url())) {
    failActiveRequest(QStringLiteral("Torrent feed redirect used an unsupported URL scheme"));
    return;
  }

  const auto source = reply->url();
  const auto response = std::exchange(response_, {});
  timeout_->stop();
  ++generation_;
  auto* finishedReply = std::exchange(reply_, nullptr);
  finishedReply->deleteLater();

  const auto result = parseTorrentFeed(response, source);
  if (!result.error.isEmpty()) {
    emit failed(result.error);
  } else {
    emit finished(result.items);
  }
}

}  // namespace track
