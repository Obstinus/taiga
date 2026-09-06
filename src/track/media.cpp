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

#include "media.hpp"

#include <QDebug>
#include <algorithm>
#include <ranges>

#include "media/anime.hpp"
#include "media/anime_db.hpp"
#include "media/anime_list_utils.hpp"
#include "sync/service.hpp"
#include "taiga/settings.hpp"
#include "track/episode.hpp"
#include "track/media_player.hpp"
#include "track/recognition.hpp"
#include "track/sharing.hpp"
#ifdef Q_OS_LINUX
#include "track/mpris.hpp"
#endif

namespace track::media {

Detection::Detection(QObject* parent) : QObject(parent) {
  pollTimer_ = new QTimer(this);
  connect(pollTimer_, &QTimer::timeout, this, &Detection::poll);
}

const std::optional<Episode> Detection::getCurrentEpisode() const {
  return currentEpisode_;
}

const std::optional<Detection::media_t> Detection::getCurrentMedia() const {
  return currentMedia_;
}

const std::optional<Detection::player_t> Detection::getCurrentPlayer() const {
  return currentPlayer_;
}

bool Detection::isEnabled() const {
  return enabled_;
}

bool Detection::init() {
  if (!parsePlayersData(players_)) {
    return false;
  }

#if defined(Q_OS_WINDOWS) || defined(Q_OS_LINUX)
  setEnabled(taiga::settings.detectionEnabled());
#endif

  return true;
}

void Detection::setEnabled(const bool enabled) {
  enabled_ = enabled;

  if (!enabled_) {
    pollTimer_->stop();
    reset();
    return;
  }

#if defined(Q_OS_WINDOWS) || defined(Q_OS_LINUX)
  pollTimer_->start(taiga::settings.mediaDetectionInterval());
#endif
}

void Detection::poll() {
  if (!enabled_) return;

#ifdef Q_OS_WINDOWS
  const auto players = getEnabledPlayers(players_);

  static const auto media_proc = [](const anisthesia::MediaInfo&) {
    return true;  // Accept all media
  };

  std::vector<anisthesia::win::Result> results;
  if (!anisthesia::win::GetResults(players, media_proc, results)) {
    reset();
    return;
  }

  if (results.empty()) {
    reset();
    return;
  }

  const auto resultIt = std::ranges::find_if(results, [this](const anisthesia::win::Result& r) {
    return r.window.handle == currentWindowHandle_;
  });
  const auto& result = resultIt != results.end() ? *resultIt : results.front();

  if (result.media.empty()) {
    reset();
    return;
  }

  currentPlayer_ = result.player;
  currentMedia_ = result.media.front();
  currentWindowHandle_ = result.window.handle;
#elif defined(Q_OS_LINUX)
  const auto results = mpris::getResults(taiga::settings.disabledMediaPlayers());
  if (results.empty()) {
    reset();
    return;
  }

  const auto samePriority = [](const mpris::Result& lhs, const mpris::Result& rhs) {
    const auto lhsFile = !lhs.media.information.empty() &&
                         lhs.media.information.front().type == anisthesia::MediaInfoType::File;
    const auto rhsFile = !rhs.media.information.empty() &&
                         rhs.media.information.front().type == anisthesia::MediaInfoType::File;
    return lhs.media.state == rhs.media.state && lhsFile == rhsFile;
  };
  const auto& preferred = results.front();
  const auto resultIt = std::ranges::find_if(results, [this](const mpris::Result& result) {
    return result.service == currentMprisService_;
  });
  const auto& result =
      resultIt != results.end() && samePriority(*resultIt, preferred) ? *resultIt : preferred;

  currentPlayer_ = result.player;
  currentMedia_ = result.media;
  currentMprisService_ = result.service;
#else
  return;
#endif

  if (!currentMedia_ || currentMedia_->information.empty()) {
    reset();
    return;
  }

  const auto mediaInfoIt = std::ranges::find_if(currentMedia_->information, [](const auto& info) {
    return info.type == anisthesia::MediaInfoType::File;
  });
  const auto titleInfoIt = std::ranges::find_if(currentMedia_->information, [](const auto& info) {
    return info.type == anisthesia::MediaInfoType::Title;
  });
  const auto urlInfoIt = std::ranges::find_if(currentMedia_->information, [](const auto& info) {
    return info.type == anisthesia::MediaInfoType::Url;
  });

  // Prefer a local file, then the page title, then the remote URL. MPRIS
  // browser adapters commonly expose both title and URL for the same page.
  const auto& mediaInfo = mediaInfoIt != currentMedia_->information.end()
                              ? *mediaInfoIt
                              : (titleInfoIt != currentMedia_->information.end()
                                     ? *titleInfoIt
                                     : (urlInfoIt != currentMedia_->information.end()
                                            ? *urlInfoIt
                                            : currentMedia_->information.front()));
  Episode episode;
  if (mediaInfo.type == anisthesia::MediaInfoType::File) {
    episode = track::recognition::parseFileInfo(QFileInfo{QString::fromStdString(mediaInfo.value)});
  } else {
    const auto remoteUrl = urlInfoIt != currentMedia_->information.end() ? urlInfoIt->value : "";
    const auto title = titleInfoIt != currentMedia_->information.end() ? titleInfoIt->value : "";
    episode =
        track::recognition::parseRemote(remoteUrl.empty() ? mediaInfo.value : remoteUrl, title);
  }

  if (mediaInfo.type == anisthesia::MediaInfoType::File &&
      !track::recognition::isVideoFile(episode)) {
    reset();
    return;
  }
  if (mediaInfo.type != anisthesia::MediaInfoType::File &&
      !episode.contains(anitomy::ElementKind::Title)) {
    reset();
    return;
  }

  const auto animeId = track::recognition::identify(episode);
  episode.setAnimeId(animeId);

  if (hasEpisodeChanged(episode)) {
    qDebug() << "Detected episode:"
             << QString::fromStdString(episode.element(anitomy::ElementKind::Title))
             << QString::fromStdString(episode.element(anitomy::ElementKind::Episode))
             << "anime ID:" << animeId;
    currentEpisode_ = episode;
    emit currentEpisodeChanged(episode);
    track::sharing::update(episode);
  }

  // Apply completion to the episode identified above, including short videos.
  // Keep it visible while the player remains registered at the end.
  const auto duration = currentMedia_->duration;
  if (currentEpisode_ && duration.count() > 0 &&
      currentMedia_->position >= listUpdatePosition(duration)) {
    saveCurrentEpisode();
  }
}

bool Detection::isMediaIdentified() const {
  return currentEpisode_.has_value() && currentEpisode_->animeId() != anime::kUnknownId;
}

void Detection::setCurrentEpisodeAnimeId(int animeId) {
  if (!currentEpisode_) return;

  currentEpisode_->setAnimeId(animeId);
  emit currentEpisodeChanged(currentEpisode_);
}

void Detection::reset() {
  const bool hadCurrentMedia =
      currentEpisode_.has_value() || currentMedia_.has_value() || currentPlayer_.has_value();
  currentPlayer_.reset();
  currentMedia_.reset();
  currentWindowHandle_ = nullptr;
  currentMprisService_.clear();

  if (currentEpisode_) {
    currentEpisode_.reset();
    emit currentEpisodeChanged(std::nullopt);
  }
  if (hadCurrentMedia) track::sharing::clear();
}

void Detection::saveCurrentEpisode() {
  if (!currentEpisode_ || !taiga::settings.syncEnabled()) return;

  const auto animeId = currentEpisode_->animeId();
  if (animeId == anime::kUnknownId) return;

  int episodeNumber = 0;
  for (const auto& value : currentEpisode_->elements(anitomy::ElementKind::Episode)) {
    bool ok = false;
    const auto number = QString::fromStdString(value).toInt(&ok);
    if (ok) episodeNumber = std::max(episodeNumber, number);
  }
  if (episodeNumber <= 0) return;

  const auto existing = anime::db.entry(animeId);
  auto entry = existing ? *existing : ListEntry{.anime_id = animeId};
  if (entry.status == anime::list::Status::Completed && !entry.rewatching) return;
  if (episodeNumber <= entry.watched_episodes) return;

  if (entry.status == anime::list::Status::NotInList) {
    entry.status = anime::list::Status::Watching;
  }
  entry.watched_episodes = episodeNumber;
  anime::list::save(entry);
  sync_service::synchronize();
  track::sharing::announce(*currentEpisode_);
}

bool Detection::hasEpisodeChanged(const Episode& episode) const {
  if (!currentEpisode_) return true;
  if (currentEpisode_->animeId() != episode.animeId()) return true;
  if (currentEpisode_->element(anitomy::ElementKind::Title) !=
      episode.element(anitomy::ElementKind::Title))
    return true;

  return currentEpisode_->elements(anitomy::ElementKind::Episode) !=
         episode.elements(anitomy::ElementKind::Episode);
}

}  // namespace track::media
