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

namespace track {
class Episode;
}

namespace track::sharing {

// Update the transient Discord activity for the currently detected episode.
void update(const Episode& episode);

// Send a completion announcement to the configured HTTP and IRC endpoints.
void announce(const Episode& episode);

// Clear transient sharing state, such as Discord Rich Presence.
void clear();

}  // namespace track::sharing
