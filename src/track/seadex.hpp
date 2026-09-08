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

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace track {

enum class SeaDexReleaseStatus {
  Unknown,
  Best,
  Alternative,
};

using SeaDexReleaseStatuses = QHash<QString, SeaDexReleaseStatus>;

struct SeaDexRelease {
  SeaDexReleaseStatus status = SeaDexReleaseStatus::Unknown;
  QString title;
  qint64 size = -1;
  QUrl infoUrl;
};

using SeaDexReleases = QHash<QString, SeaDexRelease>;

SeaDexReleases parseSeaDexReleases(const QByteArray& data, QString* error = nullptr);
SeaDexReleaseStatuses parseSeaDexResponse(const QByteArray& data, QString* error = nullptr);

class SeaDexClient final : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(SeaDexClient)

public:
  // An injected manager is borrowed and must outlive this client.
  explicit SeaDexClient(QObject* parent = nullptr, QNetworkAccessManager* manager = nullptr);
  ~SeaDexClient() override;

  void fetch(const QStringList& infoHashes);
  void cancel();

signals:
  void finished(const track::SeaDexReleases& releases);
  void failed(const QString& error);

private:
  void fetchNextChunk();
  void handleFinished(QNetworkReply* reply, quint64 generation);
  void failActiveRequest(const QString& error);

  QNetworkAccessManager* manager_ = nullptr;
  QNetworkReply* reply_ = nullptr;
  QStringList infoHashes_;
  qsizetype nextHash_ = 0;
  SeaDexReleases releases_;
  quint64 generation_ = 0;
};

}  // namespace track

Q_DECLARE_METATYPE(track::SeaDexReleaseStatus)
Q_DECLARE_METATYPE(track::SeaDexReleaseStatuses)
Q_DECLARE_METATYPE(track::SeaDexRelease)
Q_DECLARE_METATYPE(track::SeaDexReleases)
