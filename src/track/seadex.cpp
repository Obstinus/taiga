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

#include "seadex.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
#include <limits>
#include <utility>

namespace track {

namespace {

constexpr int kMaximumHashesPerRequest = 75;
constexpr qint64 kMaximumResponseBytes = 2LL * 1024 * 1024;
constexpr int kRequestTimeoutMilliseconds = 10'000;
constexpr int kMaximumRedirects = 5;

const auto kSeaDexEndpoint =
    QUrl(QStringLiteral("https://releases.moe/api/collections/torrents/records"));

bool isHex(const QChar character) {
  return (character >= QChar{'0'} && character <= QChar{'9'}) ||
         (character >= QChar{'a'} && character <= QChar{'f'}) ||
         (character >= QChar{'A'} && character <= QChar{'F'});
}

bool isBase32(const QChar character) {
  const auto upper = character.toUpper();
  return (upper >= QChar{'A'} && upper <= QChar{'Z'}) ||
         (upper >= QChar{'2'} && upper <= QChar{'7'});
}

bool isHttpUrl(const QUrl& url) {
  return url.isValid() && !url.host().isEmpty() &&
         (url.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) == 0 ||
          url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0);
}

QString normalizedInfoHash(const QString& value) {
  const auto hash = value.trimmed().toLower();
  if ((hash.size() == 40 && std::ranges::all_of(hash, isHex)) ||
      (hash.size() == 32 && std::ranges::all_of(hash, isBase32))) {
    return hash;
  }
  return {};
}

QString responseError(const QNetworkReply& reply) {
  if (reply.error() != QNetworkReply::NetworkError::NoError) return reply.errorString();

  const auto status = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute);
  bool ok = false;
  const auto statusCode = status.toInt(&ok);
  return ok && (statusCode < 200 || statusCode >= 300)
             ? QStringLiteral("SeaDex request returned HTTP %1").arg(statusCode)
             : QString{};
}

}  // namespace

SeaDexReleases parseSeaDexReleases(const QByteArray& data, QString* error) {
  if (error) error->clear();

  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(data, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    if (error) *error = QStringLiteral("SeaDex returned invalid JSON");
    return {};
  }

  const auto items = document.object().value(QStringLiteral("items"));
  if (!items.isArray()) {
    if (error) *error = QStringLiteral("SeaDex response has no items array");
    return {};
  }

  SeaDexReleases releases;
  for (const auto& value : items.toArray()) {
    if (!value.isObject()) continue;
    const auto object = value.toObject();
    const auto infoHash = normalizedInfoHash(object.value(QStringLiteral("infoHash")).toString());
    if (infoHash.isEmpty()) continue;

    SeaDexRelease release;
    release.status = object.value(QStringLiteral("isBest")).toBool()
                         ? SeaDexReleaseStatus::Best
                         : SeaDexReleaseStatus::Alternative;

    QStringList fileNames;
    qint64 totalSize = 0;
    bool hasSize = false;
    const auto files = object.value(QStringLiteral("files"));
    if (files.isArray()) {
      for (const auto& file : files.toArray()) {
        if (!file.isObject()) continue;
        const auto fileObject = file.toObject();
        const auto name = fileObject.value(QStringLiteral("name")).toString().simplified();
        if (!name.isEmpty()) fileNames.append(name);

        const auto length = fileObject.value(QStringLiteral("length"));
        if (!length.isDouble()) continue;
        const auto bytes = length.toInteger(-1);
        if (bytes < 0 || totalSize > std::numeric_limits<qint64>::max() - bytes) {
          hasSize = false;
          totalSize = 0;
          break;
        }
        totalSize += bytes;
        hasSize = true;
      }
    }

    if (!fileNames.isEmpty()) {
      release.title = fileNames.front();
      if (fileNames.size() > 1) {
        release.title += QStringLiteral(" (+%1 files)").arg(fileNames.size() - 1);
      }
    }
    const auto releaseGroup = object.value(QStringLiteral("releaseGroup")).toString().trimmed();
    if (!releaseGroup.isEmpty() && !release.title.isEmpty()) {
      release.title = QStringLiteral("[%1] %2").arg(releaseGroup, release.title);
    }
    release.size = hasSize ? totalSize : -1;

    const auto infoUrl =
        QUrl{object.value(QStringLiteral("url")).toString().trimmed(), QUrl::StrictMode};
    if (isHttpUrl(infoUrl)) release.infoUrl = infoUrl;

    releases.insert(infoHash, std::move(release));
  }
  return releases;
}

SeaDexReleaseStatuses parseSeaDexResponse(const QByteArray& data, QString* error) {
  const auto releases = parseSeaDexReleases(data, error);
  SeaDexReleaseStatuses statuses;
  for (auto it = releases.cbegin(); it != releases.cend(); ++it) {
    statuses.insert(it.key(), it.value().status);
  }
  return statuses;
}

SeaDexClient::SeaDexClient(QObject* parent, QNetworkAccessManager* manager) : QObject{parent} {
  manager_ = manager ? manager : new QNetworkAccessManager{this};
  if (!manager) manager_->setTransferTimeout(kRequestTimeoutMilliseconds);

  qRegisterMetaType<SeaDexReleaseStatus>();
  qRegisterMetaType<SeaDexReleaseStatuses>();
  qRegisterMetaType<SeaDexRelease>();
  qRegisterMetaType<SeaDexReleases>();
}

SeaDexClient::~SeaDexClient() {
  cancel();
}

void SeaDexClient::fetch(const QStringList& infoHashes) {
  cancel();

  QSet<QString> seen;
  for (const auto& value : infoHashes) {
    const auto hash = normalizedInfoHash(value);
    if (!hash.isEmpty() && !seen.contains(hash)) {
      seen.insert(hash);
      infoHashes_.append(hash);
    }
  }

  if (infoHashes_.isEmpty()) {
    emit finished(SeaDexReleases{});
    return;
  }

  fetchNextChunk();
}

void SeaDexClient::cancel() {
  ++generation_;
  nextHash_ = 0;
  infoHashes_.clear();
  releases_.clear();

  if (!reply_) return;

  auto* reply = std::exchange(reply_, nullptr);
  reply->abort();
  reply->deleteLater();
}

void SeaDexClient::fetchNextChunk() {
  if (reply_ || nextHash_ >= infoHashes_.size()) return;

  const auto count = std::min<qsizetype>(kMaximumHashesPerRequest, infoHashes_.size() - nextHash_);
  QStringList filters;
  filters.reserve(static_cast<qsizetype>(count));
  for (qsizetype index = 0; index < count; ++index) {
    filters.append(QStringLiteral("infoHash=\"") + infoHashes_.at(nextHash_ + index) +
                   QStringLiteral("\""));
  }

  auto url = kSeaDexEndpoint;
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("filter"), filters.join(QStringLiteral("||")));
  query.addQueryItem(QStringLiteral("fields"),
                     QStringLiteral("infoHash,isBest,files,releaseGroup,url"));
  query.addQueryItem(QStringLiteral("perPage"), QString::number(count));
  query.addQueryItem(QStringLiteral("skipTotal"), QStringLiteral("true"));
  url.setQuery(query);

  QNetworkRequest request{url};
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setMaximumRedirectsAllowed(kMaximumRedirects);
  request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Taiga SeaDexClient"));

  const auto generation = generation_;
  reply_ = manager_->get(request);
  reply_->setReadBufferSize(kMaximumResponseBytes + 1);
  auto* reply = reply_;
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, generation] { handleFinished(reply, generation); });
}

void SeaDexClient::handleFinished(QNetworkReply* reply, const quint64 generation) {
  if (reply != reply_ || generation != generation_) return;

  auto* finishedReply = std::exchange(reply_, nullptr);
  const auto data = finishedReply->readAll();
  finishedReply->deleteLater();

  if (data.size() > kMaximumResponseBytes) {
    failActiveRequest(QStringLiteral("SeaDex response is too large"));
    return;
  }

  const auto networkError = responseError(*finishedReply);
  if (!networkError.isEmpty()) {
    failActiveRequest(networkError);
    return;
  }

  QString parseError;
  const auto chunkReleases = parseSeaDexReleases(data, &parseError);
  if (!parseError.isEmpty()) {
    failActiveRequest(parseError);
    return;
  }
  for (auto it = chunkReleases.cbegin(); it != chunkReleases.cend(); ++it) {
    releases_.insert(it.key(), it.value());
  }

  nextHash_ += std::min<qsizetype>(kMaximumHashesPerRequest, infoHashes_.size() - nextHash_);
  if (nextHash_ < infoHashes_.size()) {
    fetchNextChunk();
    return;
  }

  const auto releases = releases_;
  ++generation_;
  infoHashes_.clear();
  releases_.clear();
  emit finished(releases);
}

void SeaDexClient::failActiveRequest(const QString& error) {
  ++generation_;
  nextHash_ = 0;
  infoHashes_.clear();
  releases_.clear();

  if (reply_) {
    auto* reply = std::exchange(reply_, nullptr);
    reply->abort();
    reply->deleteLater();
  }
  emit failed(error.isEmpty() ? QStringLiteral("SeaDex request failed") : error);
}

}  // namespace track
