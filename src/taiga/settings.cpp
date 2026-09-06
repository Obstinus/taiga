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

#include "settings.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <algorithm>
#include <ranges>

#include "base/string.hpp"
#include "compat/settings.hpp"
#include "sync/service.hpp"
#include "taiga/accounts.hpp"
#include "taiga/path.hpp"
#include "taiga/version.hpp"
#include "track/torrent_settings.hpp"

namespace taiga {

void Settings::init() const {
  const auto appVersion = taiga::version().to_string();

  const auto migrateTorrentSettings = [&] {
    const auto legacyPath = std::format("{}/v1/settings.xml", get_data_path());
    const auto torrentPath =
        QDir(QString::fromStdString(get_data_path())).filePath(QStringLiteral("torrents.json"));
    if (!QFile::exists(QString::fromStdString(legacyPath)) || QFile::exists(torrentPath)) return;

    if (auto torrentSettings = compat::v1::readTorrentSettings(legacyPath)) {
      if (!QDir::isAbsolutePath(torrentSettings->downloadDirectory)) {
        torrentSettings->downloadDirectory =
            QDir(QString::fromStdString(get_data_path())).filePath(QStringLiteral("torrents"));
      }
      QString error;
      if (!track::saveTorrentSettings(*torrentSettings, &error)) {
        qWarning() << "Could not migrate torrent settings:" << error;
      }
    }
  };

  // v1 to v2
  if (!QFile::exists(fileName())) {
    const auto legacyPath = std::format("{}/v1/settings.xml", get_data_path());
    compat::v1::readSettings(legacyPath, *this, accounts);

    migrateTorrentSettings();

    setValue("meta.version", appVersion);
    return;
  }

  // v2.x
  // Torrent settings are stored separately, so users who already created a
  // v2 settings.json before torrent migration was added still need this step.
  migrateTorrentSettings();

  const auto fileVersion = value("meta.version").toString().toStdString();
  if (fileVersion != appVersion) {
    setValue("meta.version", appVersion);
  }
}

QString Settings::fileName() const {
  return u"%1/settings.json"_s.arg(QString::fromStdString(get_data_path()));
}

////////////////////////////////////////////////////////////////////////////////

Qt::ColorScheme Settings::appColorScheme() const {
  return value("app.colorScheme", static_cast<int>(Qt::ColorScheme::Unknown))
      .value<Qt::ColorScheme>();
}

bool Settings::detectionEnabled() const {
  return value("recognition.enabled", true).toBool();
}

std::vector<std::string> Settings::disabledMediaPlayers() const {
  return value("recognition.mediaPlayers.disabled", QJsonArray{QStringLiteral("Brave")})
             .toJsonArray()
             .toVariantList() |
         std::views::transform([](const QVariant& v) { return v.toString().toStdString(); }) |
         std::ranges::to<std::vector>();
}

bool Settings::sharingEnabled() const {
  return value("sharing.enabled", true).toBool();
}

bool Settings::discordSharingEnabled() const {
  return value("sharing.discord.enabled", false).toBool();
}

std::string Settings::discordApplicationId() const {
  return value("sharing.discord.applicationId", "379871385176244224").toString().toStdString();
}

bool Settings::httpSharingEnabled() const {
  return value("sharing.http.enabled", false).toBool();
}

std::string Settings::httpSharingUrl() const {
  return value("sharing.http.url").toString().toStdString();
}

std::string Settings::httpSharingFormat() const {
  return value("sharing.http.format", "%title% - Episode %episode%").toString().toStdString();
}

bool Settings::ircSharingEnabled() const {
  return value("sharing.irc.enabled", false).toBool();
}

std::string Settings::ircServer() const {
  return value("sharing.irc.server").toString().toStdString();
}

int Settings::ircPort() const {
  return value("sharing.irc.port", 6667).toInt();
}

std::string Settings::ircNickname() const {
  return value("sharing.irc.nickname", "Taiga").toString().toStdString();
}

std::string Settings::ircChannel() const {
  return value("sharing.irc.channel", "#taiga").toString().toStdString();
}

bool Settings::ircUseAction() const {
  return value("sharing.irc.useAction", false).toBool();
}

std::string Settings::ircFormat() const {
  return value("sharing.irc.format", "%title% - Episode %episode%").toString().toStdString();
}

std::string Settings::proxyHost() const {
  return value("network.proxy.host").toString().toStdString();
}

std::string Settings::proxyUsername() const {
  return value("network.proxy.username").toString().toStdString();
}

std::string Settings::proxyPassword() const {
  return value("network.proxy.password").toString().toStdString();
}

std::string Settings::service() const {
  return value("v1.service", sync_service::serviceSlug(sync_service::ServiceId::AniList)).toString().toStdString();
}

std::vector<std::string> Settings::libraryFolders() const {
  return value("library.folders").toJsonArray().toVariantList() |
         std::views::transform([](const QVariant& v) { return v.toString().toStdString(); }) |
         std::ranges::to<std::vector>();
}

std::chrono::milliseconds Settings::mediaDetectionInterval() const {
  const auto interval = value("track.detection.interval", 3000).toInt();
  return std::chrono::milliseconds{interval};
}

bool Settings::syncEnabled() const {
  return value("sync.enabled", true).toBool();
}

anime::TitleLanguage Settings::titleLanguage() const {
  if (!titleLanguageCache_) {
    const auto language = value("library.titleLanguage", u"romaji"_s).toString();
    if (language == u"english") {
      titleLanguageCache_ = anime::TitleLanguage::English;
    } else if (language == u"native") {
      titleLanguageCache_ = anime::TitleLanguage::Native;
    } else {
      titleLanguageCache_ = anime::TitleLanguage::Romaji;
    }
  }
  return *titleLanguageCache_;
}

////////////////////////////////////////////////////////////////////////////////

void Settings::setAppColorScheme(const Qt::ColorScheme scheme) const {
  setValue("app.colorScheme", static_cast<int>(scheme));
}

void Settings::setDetectionEnabled(const bool enabled) const {
  setValue("recognition.enabled", enabled);
}

void Settings::setDisabledMediaPlayers(std::vector<std::string> players) const {
  const auto list =
      players |
      std::views::transform([](const std::string& s) { return QString::fromStdString(s); }) |
      std::ranges::to<QList>();
  setValue("recognition.mediaPlayers.disabled", QJsonArray::fromStringList(list));
}

void Settings::setSharingEnabled(const bool enabled) const {
  setValue("sharing.enabled", enabled);
}

void Settings::setDiscordSharingEnabled(const bool enabled) const {
  setValue("sharing.discord.enabled", enabled);
}

void Settings::setDiscordApplicationId(const std::string& applicationId) const {
  setValue("sharing.discord.applicationId", applicationId);
}

void Settings::setHttpSharingEnabled(const bool enabled) const {
  setValue("sharing.http.enabled", enabled);
}

void Settings::setHttpSharingUrl(const std::string& url) const {
  setValue("sharing.http.url", url);
}

void Settings::setHttpSharingFormat(const std::string& format) const {
  setValue("sharing.http.format", format);
}

void Settings::setIrcSharingEnabled(const bool enabled) const {
  setValue("sharing.irc.enabled", enabled);
}

void Settings::setIrcServer(const std::string& server) const {
  setValue("sharing.irc.server", server);
}

void Settings::setIrcPort(const int port) const {
  setValue("sharing.irc.port", std::clamp(port, 1, 65535));
}

void Settings::setIrcNickname(const std::string& nickname) const {
  setValue("sharing.irc.nickname", nickname);
}

void Settings::setIrcChannel(const std::string& channel) const {
  setValue("sharing.irc.channel", channel);
}

void Settings::setIrcUseAction(const bool enabled) const {
  setValue("sharing.irc.useAction", enabled);
}

void Settings::setIrcFormat(const std::string& format) const {
  setValue("sharing.irc.format", format);
}

void Settings::setProxyHost(const std::string& host) const {
  setValue("network.proxy.host", host);
}

void Settings::setProxyUsername(const std::string& username) const {
  setValue("network.proxy.username", username);
}

void Settings::setProxyPassword(const std::string& password) const {
  setValue("network.proxy.password", password);
}

void Settings::setService(const std::string& service) const {
  setValue("v1.service", service);
}

void Settings::setLibraryFolders(std::vector<std::string> folders) const {
  const auto list =
      folders |
      std::views::transform([](const std::string& s) { return QString::fromStdString(s); }) |
      std::ranges::to<QList>();
  setValue("library.folders", QJsonArray::fromStringList(list));
}

void Settings::setMediaDetectionInterval(const std::chrono::milliseconds interval) const {
  setValue("track.detection.interval", static_cast<qlonglong>(interval.count()));
}

void Settings::setSyncEnabled(const bool enabled) const {
  setValue("sync.enabled", enabled);
}

void Settings::setTitleLanguage(const anime::TitleLanguage language) const {
  const auto slug = [language]() -> std::string {
    switch (language) {
      default:
      case anime::TitleLanguage::Romaji:
        return "romaji";
      case anime::TitleLanguage::English:
        return "english";
      case anime::TitleLanguage::Native:
        return "native";
    }
  }();
  setValue("library.titleLanguage", slug);
  titleLanguageCache_ = language;
}

}  // namespace taiga
