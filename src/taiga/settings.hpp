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

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "base/settings.hpp"
#include "media/anime.hpp"

namespace taiga {

class Settings final : public base::Settings {
public:
  void init() const;

  Qt::ColorScheme appColorScheme() const;
  bool detectionEnabled() const;
  std::vector<std::string> disabledMediaPlayers() const;
  bool sharingEnabled() const;
  bool discordSharingEnabled() const;
  std::string discordApplicationId() const;
  bool httpSharingEnabled() const;
  std::string httpSharingUrl() const;
  std::string httpSharingFormat() const;
  bool ircSharingEnabled() const;
  std::string ircServer() const;
  int ircPort() const;
  std::string ircNickname() const;
  std::string ircChannel() const;
  bool ircUseAction() const;
  std::string ircFormat() const;
  std::string proxyHost() const;
  std::string proxyUsername() const;
  std::string proxyPassword() const;
  std::string service() const;
  std::vector<std::string> libraryFolders() const;
  std::chrono::milliseconds mediaDetectionInterval() const;
  bool syncEnabled() const;
  anime::TitleLanguage titleLanguage() const;

  void setAppColorScheme(const Qt::ColorScheme scheme) const;
  void setDetectionEnabled(const bool enabled) const;
  void setDisabledMediaPlayers(std::vector<std::string> players) const;
  void setSharingEnabled(const bool enabled) const;
  void setDiscordSharingEnabled(const bool enabled) const;
  void setDiscordApplicationId(const std::string& applicationId) const;
  void setHttpSharingEnabled(const bool enabled) const;
  void setHttpSharingUrl(const std::string& url) const;
  void setHttpSharingFormat(const std::string& format) const;
  void setIrcSharingEnabled(const bool enabled) const;
  void setIrcServer(const std::string& server) const;
  void setIrcPort(const int port) const;
  void setIrcNickname(const std::string& nickname) const;
  void setIrcChannel(const std::string& channel) const;
  void setIrcUseAction(const bool enabled) const;
  void setIrcFormat(const std::string& format) const;
  void setProxyHost(const std::string& host) const;
  void setProxyUsername(const std::string& username) const;
  void setProxyPassword(const std::string& password) const;
  void setService(const std::string& service) const;
  void setLibraryFolders(std::vector<std::string> folders) const;
  void setMediaDetectionInterval(const std::chrono::milliseconds interval) const;
  void setSyncEnabled(const bool enabled) const;
  void setTitleLanguage(const anime::TitleLanguage language) const;

private:
  QString fileName() const override;

  mutable std::optional<anime::TitleLanguage> titleLanguageCache_;
};

inline Settings settings;

}  // namespace taiga
