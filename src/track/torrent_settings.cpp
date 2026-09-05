#include "torrent_settings.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrl>
#include <algorithm>

#include "taiga/path.hpp"

namespace {

QString statePath() {
  return QDir(QString::fromStdString(taiga::get_data_path())).filePath("torrents.json");
}

bool readState(QJsonObject& object, QString* error) {
  QFile file(statePath());
  if (!file.exists()) return true;
  if (!file.open(QIODevice::ReadOnly)) {
    if (error) *error = file.errorString();
    return false;
  }
  QJsonParseError parseError;
  const auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    if (error) *error = QStringLiteral("Cannot read torrent settings: invalid JSON.");
    return false;
  }
  object = doc.object();
  return true;
}

bool writeState(const QJsonObject& object, QString* error) {
  if (!QDir().mkpath(QString::fromStdString(taiga::get_data_path()))) {
    if (error) *error = QStringLiteral("Cannot create torrent settings directory.");
    return false;
  }
  QSaveFile file(statePath());
  const auto bytes = QJsonDocument(object).toJson();
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
    if (error) *error = file.errorString();
    return false;
  }
  return true;
}

bool validHttpUrl(const QString& text) {
  const QUrl url(text);
  return url.isValid() && !url.host().isEmpty() &&
         (url.scheme() == "https" || url.scheme() == "http");
}

}  // namespace

namespace track {

TorrentSettings loadTorrentSettings() {
  QJsonObject root;
  readState(root, nullptr);
  const auto config = root.value("settings").toObject();
  TorrentSettings settings;
  settings.feedUrl = config.value("feedUrl").toString("https://nyaa.si/?page=rss&c=1_2&f=0");
  settings.searchUrl =
      config.value("searchUrl").toString("https://nyaa.si/?page=rss&c=1_2&f=0&q=%title%");
  settings.downloadDirectory =
      config.value("downloadDirectory")
          .toString(QDir(QString::fromStdString(taiga::get_data_path())).filePath("torrents"));
  settings.titleFilter = config.value("titleFilter").toString();
  settings.releaseGroup = config.value("releaseGroup").toString();
  settings.resolution = config.value("resolution").toString();
  settings.hideArchived = config.value("hideArchived").toBool(true);
  settings.autoRefresh = config.value("autoRefresh").toBool(false);
  settings.refreshMinutes = std::clamp(config.value("refreshMinutes").toInt(30), 1, 1440);
  return settings;
}

bool saveTorrentSettings(const TorrentSettings& settings, QString* error) {
  if (!validHttpUrl(settings.feedUrl) || !settings.searchUrl.contains("%title%") ||
      !validHttpUrl(QString(settings.searchUrl).replace("%title%", "test"))) {
    if (error)
      *error = QStringLiteral("Use HTTP(S) feed URLs and a search URL containing %title%.");
    return false;
  }
  if (!QDir::isAbsolutePath(settings.downloadDirectory)) {
    if (error) *error = QStringLiteral("Choose an absolute directory for torrent files.");
    return false;
  }
  QJsonObject root;
  if (!readState(root, error)) return false;
  auto config = root.value("settings").toObject();
  config.insert("feedUrl", settings.feedUrl);
  config.insert("searchUrl", settings.searchUrl);
  config.insert("downloadDirectory", settings.downloadDirectory);
  config.insert("titleFilter", settings.titleFilter);
  config.insert("releaseGroup", settings.releaseGroup);
  config.insert("resolution", settings.resolution);
  config.insert("hideArchived", settings.hideArchived);
  config.insert("autoRefresh", settings.autoRefresh);
  config.insert("refreshMinutes", std::clamp(settings.refreshMinutes, 1, 1440));
  root.insert("settings", config);
  return writeState(root, error);
}

QStringList loadTorrentArchive() {
  QJsonObject root;
  readState(root, nullptr);
  QStringList ids;
  for (const auto value : root.value("archive").toArray()) {
    if (value.isString() && !value.toString().isEmpty()) ids.append(value.toString());
  }
  ids.removeDuplicates();
  return ids;
}

bool saveTorrentArchive(const QStringList& ids, QString* error) {
  QJsonObject root;
  if (!readState(root, error)) return false;
  auto unique = ids;
  unique.removeDuplicates();
  root.insert("archive", QJsonArray::fromStringList(unique));
  return writeState(root, error);
}

}  // namespace track
