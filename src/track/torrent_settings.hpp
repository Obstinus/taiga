#pragma once

#include <QString>
#include <QStringList>

namespace track {

struct TorrentSettings {
  QString feedUrl;
  QString searchUrl;
  QString downloadDirectory;
  QString titleFilter;
  QString releaseGroup;
  QString resolution;
  bool hideArchived = true;
  bool autoRefresh = false;
  int refreshMinutes = 30;
};

TorrentSettings loadTorrentSettings();
bool saveTorrentSettings(const TorrentSettings& settings, QString* error = nullptr);
QStringList loadTorrentArchive();
bool saveTorrentArchive(const QStringList& ids, QString* error = nullptr);

}  // namespace track
