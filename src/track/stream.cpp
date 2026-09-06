/**
 * Taiga
 * Copyright (C) 2010-2026, Eren Okka
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "stream.hpp"

#include <QRegularExpression>
#include <QUrl>
#include <array>
#include <ranges>

namespace track::recognition::stream {

namespace {

struct Provider {
  QString name;
  QRegularExpression url;
  QRegularExpression title;
};

const std::array providers{
    Provider{QStringLiteral("AnimeLab"),
             QRegularExpression(QStringLiteral(R"(animelab\.com/player/)"),
                                QRegularExpression::CaseInsensitiveOption),
             QRegularExpression(QStringLiteral(R"(^AnimeLab\s+-\s+(.+)$)"),
                                QRegularExpression::CaseInsensitiveOption)},
    Provider{QStringLiteral("Anime Digital Network"),
             QRegularExpression(QStringLiteral(R"(animedigitalnetwork\.fr/video/)"),
                                QRegularExpression::CaseInsensitiveOption),
             QRegularExpression(QStringLiteral(R"(^(.+?)\s+-\s+streaming\s*-.*\s+ADN$)"),
                                QRegularExpression::CaseInsensitiveOption)},
    Provider{QStringLiteral("Anime News Network"),
             QRegularExpression(QStringLiteral(R"(animenewsnetwork\.(?:com|cc)/video/)"),
                                QRegularExpression::CaseInsensitiveOption),
             QRegularExpression(QStringLiteral(R"(^(.+?)\s+-\s+Anime News Network$)"),
                                QRegularExpression::CaseInsensitiveOption)},
    Provider{QStringLiteral("Bilibili"),
             QRegularExpression(QStringLiteral(R"(bilibili\.(?:tv|com)/[^/]+/(?:play|video)/)"),
                                QRegularExpression::CaseInsensitiveOption),
             QRegularExpression(QStringLiteral(R"(^(.+?)\s+-\s+Bilibili$)"),
                                QRegularExpression::CaseInsensitiveOption)},
    Provider{QStringLiteral("Crunchyroll"),
             QRegularExpression(QStringLiteral(R"(crunchyroll\.com/watch/)"),
                                QRegularExpression::CaseInsensitiveOption),
             QRegularExpression(QStringLiteral(R"(^(.+?)\s+-\s+Crunchyroll$)"),
                                QRegularExpression::CaseInsensitiveOption)},
    Provider{QStringLiteral("Jellyfin"),
             QRegularExpression(QStringLiteral(R"(/web/(?:index\.html)?#!/video)"),
                                QRegularExpression::CaseInsensitiveOption),
             QRegularExpression(QStringLiteral(R"(^(?:Jellyfin|(.+))$)"),
                                QRegularExpression::CaseInsensitiveOption)},
    Provider{
        QStringLiteral("Plex"),
        QRegularExpression(
            QStringLiteral(
                R"((?:app\.plex\.tv/desktop|[^/]*plex\.tv/web/|localhost:32400/web/|\d{1,3}(?:\.\d{1,3}){3}:32400/web/|plex\.[^/]+/|[^/]+/plex))"),
            QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"(^(?:Plex|(?:▶\s*)?(.+))$)"),
                           QRegularExpression::CaseInsensitiveOption)},
    Provider{QStringLiteral("Roku Channel"),
             QRegularExpression(QStringLiteral(R"(therokuchannel\.roku\.com/watch/)"),
                                QRegularExpression::CaseInsensitiveOption),
             QRegularExpression(
                 QStringLiteral(R"(^Watch (.+) Online for Free \| The Roku Channel \| Roku$)"),
                 QRegularExpression::CaseInsensitiveOption)},
    Provider{QStringLiteral("Tubi"),
             QRegularExpression(QStringLiteral(R"(tubitv\.com/tv-shows/)"),
                                QRegularExpression::CaseInsensitiveOption),
             QRegularExpression(QStringLiteral(R"(^Watch (.+) - Free TV Shows \| Tubi$)"),
                                QRegularExpression::CaseInsensitiveOption)},
    Provider{QStringLiteral("Veoh"),
             QRegularExpression(QStringLiteral(R"(veoh\.com/watch/)"),
                                QRegularExpression::CaseInsensitiveOption),
             QRegularExpression(QStringLiteral(R"(^Watch Videos Online \| (.+) \| Veoh\.com$)"),
                                QRegularExpression::CaseInsensitiveOption)},
    Provider{QStringLiteral("VIZ"),
             QRegularExpression(
                 QStringLiteral(R"(viz\.com/watch/streaming/[^/]+-(?:episode-[0-9]+|movie)/)"),
                 QRegularExpression::CaseInsensitiveOption),
             QRegularExpression(QStringLiteral(R"(^(.+)\s+//\s+VIZ$)"),
                                QRegularExpression::CaseInsensitiveOption)},
    Provider{QStringLiteral("VRV"),
             QRegularExpression(QStringLiteral(R"(vrv\.co/watch/)"),
                                QRegularExpression::CaseInsensitiveOption),
             QRegularExpression(QStringLiteral(R"(^(.+?)\s+-\s+Watch on VRV$)"),
                                QRegularExpression::CaseInsensitiveOption)},
    Provider{QStringLiteral("Wakanim"),
             QRegularExpression(QStringLiteral(R"(wakanim\.tv/[^/]+/v2/catalogue/episode/)"),
                                QRegularExpression::CaseInsensitiveOption),
             QRegularExpression(QStringLiteral(R"(^(.+?)\s+(?:auf|on|sur) Wakanim\.TV.*$)"),
                                QRegularExpression::CaseInsensitiveOption)},
    Provider{QStringLiteral("Yahoo View"),
             QRegularExpression(QStringLiteral(R"(view\.yahoo\.com/show/.+/episode/)"),
                                QRegularExpression::CaseInsensitiveOption),
             QRegularExpression(QStringLiteral(R"(^Watch .+ Free Online - (.+) \| Yahoo View$)"),
                                QRegularExpression::CaseInsensitiveOption)},
    Provider{QStringLiteral("YouTube"),
             QRegularExpression(QStringLiteral(R"((?:youtube\.com/watch|youtu\.be/))"),
                                QRegularExpression::CaseInsensitiveOption),
             QRegularExpression(QStringLiteral(R"(^(?:YouTube|(?:▶\s*)?(.+)\s+-\s+YouTube)$)"),
                                QRegularExpression::CaseInsensitiveOption)},
};

const Provider* findProvider(const QString& url) {
  for (const auto& provider : providers) {
    if (provider.url.match(url).hasMatch()) return &provider;
  }
  return nullptr;
}

}  // namespace

std::optional<QString> providerForUrl(const QString& url) {
  if (const auto* provider = findProvider(url)) return provider->name;
  return std::nullopt;
}

QString normalizeTitle(const QString& url, const QString& title) {
  auto normalized = title.trimmed();
  const auto* provider = findProvider(url);

  if (provider) {
    const auto match = provider->title.match(normalized);
    if (match.hasMatch()) {
      normalized.clear();
      for (qsizetype i = 1; i <= match.lastCapturedIndex(); ++i) {
        if (!match.captured(i).isEmpty()) {
          normalized = match.captured(i);
          break;
        }
      }
    }

    if (provider->name == "Anime Digital Network") {
      normalized.replace(QStringLiteral(" : "), QStringLiteral(" - "));
    } else if (provider->name == "Anime News Network") {
      normalized.remove(QRegularExpression(QStringLiteral(R"( \((?:s|d)(?:, uncut)?\))"),
                                           QRegularExpression::CaseInsensitiveOption));
    } else if (provider->name == "Plex") {
      normalized.remove(QStringLiteral(" · "));
    } else if (provider->name == "Roku Channel" || provider->name == "Tubi") {
      normalized.replace(QRegularExpression(QStringLiteral(R"( S(\d+):E(\d+) )")),
                         QStringLiteral(" S\\1E\\2 "));
    } else if (provider->name == "VRV") {
      normalized.replace(QStringLiteral(": EP "), QStringLiteral(" - EP "));
    } else if (provider->name == "Wakanim") {
      const auto match =
          QRegularExpression(
              QStringLiteral(R"(^(?:Episode (\d+)|Film|Movie) - (?:ENGDUB - )?(.+)$)"))
              .match(normalized);
      if (match.hasMatch()) {
        normalized = match.captured(2);
        if (!match.captured(1).isEmpty())
          normalized += QStringLiteral(" - Episode ") + match.captured(1);
      }
    }
  }

  // Browser MPRIS adapters may expose a generic tab title instead of media.
  static const std::array commonTitles{
      QStringLiteral("Blank Page"),       QStringLiteral("InPrivate"),
      QStringLiteral("New Tab"),          QStringLiteral("Private Browsing"),
      QStringLiteral("Private browsing"), QStringLiteral("Problem loading page"),
      QStringLiteral("Speed Dial"),       QStringLiteral("Untitled"),
  };
  if (std::ranges::any_of(commonTitles, [&normalized](const auto& common) {
        return normalized.compare(common, Qt::CaseInsensitive) == 0;
      })) {
    normalized.clear();
  }

  static const QRegularExpression commonFailureSuffix(
      QStringLiteral(R"(\s+-\s+(?:Crashed|Network error)\s*$)"),
      QRegularExpression::CaseInsensitiveOption);
  if (commonFailureSuffix.match(normalized).hasMatch()) normalized.clear();

  const QUrl remoteUrl{url};
  if (!remoteUrl.host().isEmpty() && normalized.startsWith(remoteUrl.host(), Qt::CaseInsensitive)) {
    normalized.clear();
  }
  if (normalized.startsWith("http://", Qt::CaseInsensitive) ||
      normalized.startsWith("https://", Qt::CaseInsensitive)) {
    normalized.clear();
  }

  return normalized.trimmed();
}

}  // namespace track::recognition::stream
