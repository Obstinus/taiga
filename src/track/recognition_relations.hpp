/**
 * Taiga
 * Copyright (C) 2010-2026, Eren Okka
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

namespace track {
class Episode;
}

namespace track::recognition {

struct EpisodeRedirection {
  int anime_id = 0;
  std::vector<std::string> episode_numbers;
};

std::optional<EpisodeRedirection> findEpisodeRedirection(const int anime_id,
                                                         const Episode& episode);

}  // namespace track::recognition
