#pragma once

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>

#include "track/torrent_feed.hpp"

namespace track {

// Validates metainfo before saving a network response as a .torrent file.
bool isTorrentMetainfo(const QByteArray& data);

class TorrentDownloader : public QObject {
  Q_OBJECT

public:
  explicit TorrentDownloader(QObject* parent = nullptr);
  void download(const TorrentItem& item, const QString& directory);

signals:
  void succeeded(const QString& id, const QUrl& localOrMagnetUrl);
  void failed(const QString& id, const QString& error);

private:
  QNetworkAccessManager m_network;
  QSet<QString> m_pending;
};

}  // namespace track
