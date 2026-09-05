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

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace track {

struct TorrentItem {
  QString id;
  QString title;
  QUrl downloadUrl;
  QUrl infoUrl;
  QString size;
  int seeders = -1;
  int leechers = -1;
  QDateTime published;
};

struct TorrentFeedResult {
  QList<TorrentItem> items;
  QString error;
};

TorrentFeedResult parseTorrentFeed(const QByteArray& data, const QUrl& source);
bool isTorrentDownloadUrl(const QUrl& url);
QUrl torrentSearchUrl(const QString& urlTemplate, const QString& query);

class TorrentFeedClient final : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(TorrentFeedClient)

public:
  explicit TorrentFeedClient(QObject* parent = nullptr);
  ~TorrentFeedClient() override;

  void fetch(const QUrl& url);
  void cancel();

signals:
  void finished(const QList<track::TorrentItem>& items);
  void failed(const QString& error);

private:
  void failActiveRequest(const QString& error);
  void handleFinished(QNetworkReply* reply, quint64 generation);
  void handleReadyRead(QNetworkReply* reply, quint64 generation);
  bool checkResponseSize(QNetworkReply* reply);

  QNetworkAccessManager* manager_ = nullptr;
  QNetworkReply* reply_ = nullptr;
  QTimer* timeout_ = nullptr;
  QByteArray response_;
  quint64 generation_ = 0;
};

}  // namespace track

Q_DECLARE_METATYPE(track::TorrentItem)
Q_DECLARE_METATYPE(QList<track::TorrentItem>)
