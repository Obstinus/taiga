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

#include "mpris.hpp"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>
#include <QUrl>
#include <QVariantMap>
#include <algorithm>
#include <chrono>

#include "base/string.hpp"

namespace track::media::mpris {

namespace {

constexpr auto kObjectPath = "/org/mpris/MediaPlayer2";
constexpr auto kPropertiesInterface = "org.freedesktop.DBus.Properties";
constexpr auto kRootInterface = "org.mpris.MediaPlayer2";
constexpr auto kPlayerInterface = "org.mpris.MediaPlayer2.Player";
constexpr auto kServicePrefix = "org.mpris.MediaPlayer2.";

QVariant unwrapVariant(const QVariant& value) {
  if (value.metaType() == QMetaType::fromType<QDBusVariant>()) {
    return qvariant_cast<QDBusVariant>(value).variant();
  }
  if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
    return QVariant::fromValue(qdbus_cast<QVariantMap>(value));
  }
  return value;
}

QVariant readProperty(const QDBusConnection& bus, const QString& service, const char* interfaceName,
                      const char* propertyName) {
  QDBusInterface properties{service, kObjectPath, kPropertiesInterface, bus};
  if (!properties.isValid()) return {};

  const QDBusReply<QVariant> reply =
      properties.call("Get", QString::fromLatin1(interfaceName), QString::fromLatin1(propertyName));
  return reply.isValid() ? reply.value() : QVariant{};
}

QString metadataString(const QVariantMap& metadata, const QString& key) {
  const auto value = unwrapVariant(metadata.value(key));
  if (value.metaType() == QMetaType::fromType<QUrl>()) {
    return value.toUrl().toString();
  }
  return value.toString();
}

bool isDisabled(const QString& candidate, const std::vector<std::string>& disabledPlayers) {
  if (candidate.isEmpty()) return false;

  return std::ranges::any_of(disabledPlayers, [&candidate](const std::string& disabled) {
    return candidate.compare(QString::fromStdString(disabled), Qt::CaseInsensitive) == 0;
  });
}

std::optional<anisthesia::MediaInfo> getMediaInfo(const QVariantMap& metadata) {
  const auto uri = metadataString(metadata, u"xesam:url"_s);
  const auto title = metadataString(metadata, u"xesam:title"_s);

  if (!uri.isEmpty()) {
    const QUrl url{uri};
    if (url.isLocalFile()) {
      return anisthesia::MediaInfo{anisthesia::MediaInfoType::File,
                                   url.toLocalFile().toStdString()};
    }
  }

  // Streaming players generally expose a useful episode title but an opaque URL.
  if (!title.isEmpty()) {
    return anisthesia::MediaInfo{anisthesia::MediaInfoType::Title, title.toStdString()};
  }

  if (!uri.isEmpty()) {
    return anisthesia::MediaInfo{anisthesia::MediaInfoType::Url, uri.toStdString()};
  }

  return std::nullopt;
}

}  // namespace

std::vector<Result> getResults(const std::vector<std::string>& disabledPlayers) {
  std::vector<Result> results;

  const auto bus = QDBusConnection::sessionBus();
  if (!bus.isConnected() || !bus.interface()) return results;

  const auto services = bus.interface()->registeredServiceNames();
  if (!services.isValid()) return results;

  for (const auto& service : services.value()) {
    if (!service.startsWith(kServicePrefix)) continue;

    const auto suffix = service.mid(QString::fromLatin1(kServicePrefix).size());
    const auto identity =
        unwrapVariant(readProperty(bus, service, kRootInterface, "Identity")).toString();
    const auto desktopEntry =
        unwrapVariant(readProperty(bus, service, kRootInterface, "DesktopEntry")).toString();

    if (isDisabled(identity, disabledPlayers) || isDisabled(desktopEntry, disabledPlayers) ||
        isDisabled(service, disabledPlayers) || isDisabled(suffix, disabledPlayers)) {
      continue;
    }

    const auto status =
        unwrapVariant(readProperty(bus, service, kPlayerInterface, "PlaybackStatus")).toString();
    if (status != u"Playing" && status != u"Paused") continue;

    const auto metadata =
        unwrapVariant(readProperty(bus, service, kPlayerInterface, "Metadata")).toMap();
    const auto mediaInfo = getMediaInfo(metadata);
    if (!mediaInfo) continue;

    anisthesia::Media media{};
    media.state =
        status == u"Playing" ? anisthesia::MediaState::Playing : anisthesia::MediaState::Paused;
    media.information.push_back(*mediaInfo);

    const auto position = unwrapVariant(readProperty(bus, service, kPlayerInterface, "Position"));
    bool positionOk = false;
    const auto positionMicroseconds = position.toLongLong(&positionOk);
    if (positionOk && positionMicroseconds >= 0) {
      media.position = std::chrono::milliseconds{positionMicroseconds / 1000};
    }

    const auto duration = metadataString(metadata, u"mpris:length"_s);
    if (!duration.isEmpty()) {
      bool ok = false;
      const auto microseconds = duration.toLongLong(&ok);
      if (ok && microseconds > 0) {
        media.duration = std::chrono::milliseconds{microseconds / 1000};
      }
    }

    anisthesia::Player player{};
    player.name = (identity.isEmpty() ? (desktopEntry.isEmpty() ? suffix : desktopEntry) : identity)
                      .toStdString();

    results.push_back(Result{std::move(player), std::move(media), service.toStdString()});
  }

  const auto isPlaying = [](const Result& result) {
    return result.media.state == anisthesia::MediaState::Playing;
  };
  const auto isLocalFile = [](const Result& result) {
    return !result.media.information.empty() &&
           result.media.information.front().type == anisthesia::MediaInfoType::File;
  };

  std::ranges::sort(results, [&](const Result& lhs, const Result& rhs) {
    if (isPlaying(lhs) != isPlaying(rhs)) return isPlaying(lhs);
    if (isLocalFile(lhs) != isLocalFile(rhs)) return isLocalFile(lhs);
    return lhs.service < rhs.service;
  });
  return results;
}

}  // namespace track::media::mpris
