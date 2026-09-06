/**
 * Taiga
 * Copyright (C) 2010-2026, Eren Okka
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "track/stream.hpp"

#include <QCoreApplication>
#include <cstdio>

namespace {

int failures = 0;

void check(const bool condition, const QString& message) {
  if (condition) return;
  std::fprintf(stderr, "stream tests: %s\n", message.toUtf8().constData());
  ++failures;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  const auto youtubeUrl = QStringLiteral("https://www.youtube.com/watch?v=episode");
  check(track::recognition::stream::providerForUrl(youtubeUrl).value_or({}) == "YouTube",
        QStringLiteral("YouTube provider was not recognized"));
  check(track::recognition::stream::normalizeTitle(
            youtubeUrl, QStringLiteral("[Group] Example - 01 - YouTube")) ==
            QStringLiteral("[Group] Example - 01"),
        QStringLiteral("YouTube title suffix was not removed"));

  const auto crunchyrollUrl = QStringLiteral("https://www.crunchyroll.com/watch/episode");
  check(track::recognition::stream::providerForUrl(crunchyrollUrl).value_or({}) == "Crunchyroll",
        QStringLiteral("Crunchyroll provider was not recognized"));
  check(track::recognition::stream::normalizeTitle(
            crunchyrollUrl, QStringLiteral("Example - Crunchyroll")) == QStringLiteral("Example"),
        QStringLiteral("Crunchyroll title suffix was not removed"));

  const auto animeLabUrl = QStringLiteral("https://www.animelab.com/player/episode");
  check(
      track::recognition::stream::normalizeTitle(
          animeLabUrl, QStringLiteral("AnimeLab - Example - 03")) == QStringLiteral("Example - 03"),
      QStringLiteral("AnimeLab title prefix was not removed"));

  const auto jellyfinUrl = QStringLiteral("https://media.test/web/index.html#!/video/episode");
  check(
      track::recognition::stream::normalizeTitle(jellyfinUrl, QStringLiteral("Jellyfin")).isEmpty(),
      QStringLiteral("generic Jellyfin title was not discarded"));

  const auto wakanimUrl = QStringLiteral("https://www.wakanim.tv/en/v2/catalogue/episode/episode");
  check(track::recognition::stream::normalizeTitle(
            wakanimUrl, QStringLiteral("Episode 3 - Example on Wakanim.TV")) ==
            QStringLiteral("Example - Episode 3"),
        QStringLiteral("Wakanim episode title was not normalized"));

  const auto unknownUrl = QStringLiteral("https://example.test/watch/episode");
  check(!track::recognition::stream::providerForUrl(unknownUrl),
        QStringLiteral("unknown provider was incorrectly recognized"));
  check(track::recognition::stream::normalizeTitle(unknownUrl, "Unknown - Title") ==
            QStringLiteral("Unknown - Title"),
        QStringLiteral("unknown title was unexpectedly changed"));

  return failures == 0 ? 0 : 1;
}
