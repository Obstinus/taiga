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

#include <QString>
#include <optional>

namespace track::recognition::stream {

// Returns the provider name when the URL belongs to a known streaming web app.
std::optional<QString> providerForUrl(const QString& url);

// Removes provider-specific browser noise and turns the MPRIS title into a
// string Anitomy can recognize. Unknown providers skip provider-specific rules.
QString normalizeTitle(const QString& url, const QString& title);

}  // namespace track::recognition::stream
