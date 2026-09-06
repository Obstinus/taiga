/**
 * Taiga
 * Copyright (C) 2010-2026, Eren Okka
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "recognition_relations.hpp"

#include <climits>
#include <map>
#include <mutex>

#include "base/file.hpp"
#include "sync/service.hpp"
#include "track/episode.hpp"

namespace track::recognition {

namespace {

struct Range {
  int first = 0;
  int last = 0;
};

struct Rule {
  int destination_id = 0;
  Range source;
  Range destination;
};

std::map<int, std::vector<Rule>> rules;
std::mutex rulesMutex;
int loadedService = -2;

int serviceIndex() {
  switch (sync::currentServiceId()) {
    case sync::ServiceId::MyAnimeList: return 0;
    case sync::ServiceId::Kitsu: return 1;
    case sync::ServiceId::AniList: return 2;
    case sync::ServiceId::Unknown: return -1;
  }
  return -1;
}

bool parseRange(const QString& text, Range& range) {
  const auto values = text.split('-', Qt::KeepEmptyParts);
  if (values.isEmpty() || values.size() > 2) return false;

  bool ok = false;
  range.first = values.front().toInt(&ok);
  if (!ok || range.first < 0) return false;

  if (values.size() == 1) {
    range.last = range.first;
  } else if (values.back() == u"?") {
    range.last = INT_MAX;
  } else {
    range.last = values.back().toInt(&ok);
    if (!ok || range.last < range.first) return false;
  }
  return true;
}

bool parseRule(const QString& line, const int service, Rule& rule, int& sourceId,
               bool& selfRule) {
  const auto sides = line.split("->", Qt::KeepEmptyParts);
  if (sides.size() != 2) return false;

  const auto source = sides.front().trimmed().split(':', Qt::KeepEmptyParts);
  auto destinationText = sides.back().trimmed();
  selfRule = destinationText.endsWith('!');
  if (selfRule) {
    destinationText.chop(1);
    destinationText = destinationText.trimmed();
  }
  const auto destination = destinationText.split(':', Qt::KeepEmptyParts);
  if (source.size() != 2 || destination.size() != 2) return false;

  const auto sourceIds = source.front().split('|', Qt::KeepEmptyParts);
  const auto destinationIds = destination.front().split('|', Qt::KeepEmptyParts);
  if (sourceIds.size() != 3 || destinationIds.size() != 3 || service < 0) return false;

  bool ok = false;
  sourceId = sourceIds.at(service).toInt(&ok);
  if (!ok || sourceId <= 0) return false;

  int destinationId = 0;
  const auto destinationValue = destinationIds.at(service);
  if (destinationValue == u"~") {
    destinationId = sourceId;
  } else if (destinationValue != u"?") {
    destinationId = destinationValue.toInt(&ok);
    if (!ok || destinationId <= 0) return false;
  }
  if (destinationId == 0) destinationId = sourceId;

  rule.destination_id = destinationId;
  return parseRange(source.back(), rule.source) &&
         parseRange(destination.back(), rule.destination);
}

void loadRules(const int service) {
  rules.clear();
  if (service < 0) return;

  const auto contents = base::readFile(":/anime-relations.txt");
  if (contents.isEmpty()) return;

  bool inRules = false;
  for (auto line : contents.split('\n')) {
    line = line.trimmed();
    if (line.isEmpty() || line.startsWith('#')) continue;
    if (line.startsWith("::")) {
      inRules = line == "::rules";
      continue;
    }
    if (!inRules) continue;
    if (line.startsWith('-')) line = line.sliced(1).trimmed();

    Rule rule;
    int sourceId = 0;
    bool selfRule = false;
    if (!parseRule(line, service, rule, sourceId, selfRule)) continue;

    rules[sourceId].push_back(rule);
    if (selfRule) rules[rule.destination_id].push_back(rule);
  }
}

std::optional<std::pair<int, int>> redirectNumber(const int animeId, const int number) {
  const auto it = rules.find(animeId);
  if (it == rules.end()) return std::nullopt;

  for (const auto& rule : it->second) {
    if (number < rule.source.first || number > rule.source.last) continue;

    const auto distance = number - rule.source.first;
    const auto destination = rule.destination.first == rule.destination.last
                                 ? rule.destination.first
                                 : rule.destination.first + distance;
    if (destination <= rule.destination.last) {
      return std::pair{rule.destination_id, destination};
    }
  }
  return std::nullopt;
}

}  // namespace

std::optional<EpisodeRedirection> findEpisodeRedirection(const int anime_id,
                                                         const Episode& episode) {
  const std::scoped_lock lock(rulesMutex);
  const auto service = serviceIndex();
  if (service != loadedService) {
    loadedService = service;
    loadRules(service);
  }

  const auto numbers = episode.elements(anitomy::ElementKind::Episode);
  if (numbers.empty()) return std::nullopt;

  EpisodeRedirection result;
  for (const auto& value : numbers) {
    bool ok = false;
    const int number = QString::fromStdString(value).toInt(&ok);
    if (!ok) return std::nullopt;

    const auto redirected = redirectNumber(anime_id, number);
    if (!redirected) return std::nullopt;
    if (result.anime_id != 0 && result.anime_id != redirected->first) return std::nullopt;

    result.anime_id = redirected->first;
    result.episode_numbers.push_back(std::to_string(redirected->second));
  }

  return result.anime_id != 0 ? std::optional{result} : std::nullopt;
}

}  // namespace track::recognition
