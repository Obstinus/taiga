/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
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

#include "taiga/application.hpp"

int main(int argc, char* argv[]) {
#ifdef Q_OS_LINUX
  // Qt's GTK platform theme loads the ATK bridge even when no assistive
  // technology is in use.  In a session with a stale or unavailable AT-SPI
  // socket that produces a noisy dbind warning before the application starts.
  // Respect explicit accessibility configuration and only disable the GTK
  // bridge for the usual, non-accessible case.
  if (!qEnvironmentVariableIsSet("NO_AT_BRIDGE") &&
      !qEnvironmentVariableIsSet("QT_ACCESSIBILITY")) {
    qputenv("NO_AT_BRIDGE", "1");
  }
#endif

  taiga::Application app(argc, argv);
  return app.run();
}
