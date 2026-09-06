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

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QProcess>
#include <QVariantMap>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <ranges>
#include <string>
#include <vector>

#include "track/mpris.hpp"

namespace {

constexpr auto kObjectPath = "/org/mpris/MediaPlayer2";
constexpr auto kPropertiesInterface = "org.freedesktop.DBus.Properties";
constexpr auto kRootInterface = "org.mpris.MediaPlayer2";
constexpr auto kPlayerInterface = "org.mpris.MediaPlayer2.Player";
constexpr auto kServicePrefix = "org.mpris.MediaPlayer2.taiga_probe_";

[[noreturn]] void fail(const char* message) {
  std::fprintf(stderr, "MPRIS probe failed: %s\n", message);
  std::exit(EXIT_FAILURE);
}

void expect(const bool condition, const char* message) {
  if (!condition) fail(message);
}

class FakeMpris final : public QDBusVirtualObject {
public:
  FakeMpris(QString identity, QString status, QVariantMap metadata, const qint64 position)
      : identity_(std::move(identity)),
        status_(std::move(status)),
        metadata_(std::move(metadata)),
        position_(position) {}

  QString introspect(const QString&) const override {
    return QStringLiteral(
        "<node>"
        "<interface name=\"org.freedesktop.DBus.Properties\">"
        "<method name=\"Get\"><arg type=\"s\" direction=\"in\"/>"
        "<arg type=\"s\" direction=\"in\"/><arg type=\"v\" direction=\"out\"/>"
        "</method></interface></node>");
  }

  bool handleMessage(const QDBusMessage& message, const QDBusConnection& connection) override {
    if (message.interface() == "org.freedesktop.DBus.Introspectable" &&
        message.member() == "Introspect") {
      connection.send(message.createReply(introspect(message.path())));
      return true;
    }

    if (message.interface() != kPropertiesInterface || message.member() != "Get" ||
        message.arguments().size() != 2) {
      connection.send(message.createErrorReply("org.freedesktop.DBus.Error.UnknownMethod",
                                               "Unsupported fake MPRIS method"));
      return true;
    }

    const auto interfaceName = message.arguments().at(0).toString();
    const auto propertyName = message.arguments().at(1).toString();
    QVariant value;

    if (interfaceName == kRootInterface && propertyName == "Identity") {
      value = identity_;
    } else if (interfaceName == kRootInterface && propertyName == "DesktopEntry") {
      value = identity_.toLower();
    } else if (interfaceName == kPlayerInterface && propertyName == "PlaybackStatus") {
      value = status_;
    } else if (interfaceName == kPlayerInterface && propertyName == "Metadata") {
      value = metadata_;
    } else if (interfaceName == kPlayerInterface && propertyName == "Position") {
      value = QVariant::fromValue<qlonglong>(position_);
    } else {
      connection.send(message.createErrorReply("org.freedesktop.DBus.Error.UnknownProperty",
                                               "Unsupported fake MPRIS property"));
      return true;
    }

    connection.send(message.createReply(QVariant::fromValue(QDBusVariant{value})));
    return true;
  }

private:
  QString identity_;
  QString status_;
  QVariantMap metadata_;
  qint64 position_ = 0;
};

struct Service {
  QString connectionName;
  QDBusConnection bus;
  QString name;
  std::unique_ptr<FakeMpris> object;
};

Service registerService(const QString& name, const QString& identity, const QString& status,
                        QVariantMap metadata, const qint64 position = 0) {
  const auto connectionName = name + "_connection";
  auto bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, connectionName);
  expect(bus.isConnected(), "could not connect to the session bus");

  Service service{connectionName, bus, name,
                  std::make_unique<FakeMpris>(identity, status, std::move(metadata), position)};
  expect(service.bus.registerService(service.name), "could not register the fake service");
  expect(service.bus.registerVirtualObject(kObjectPath, service.object.get()),
         "could not register the fake object");
  return service;
}

const track::media::mpris::Result* findResult(
    const std::vector<track::media::mpris::Result>& results, const QString& suffix) {
  const auto it = std::ranges::find_if(results, [&suffix](const auto& result) {
    return QString::fromStdString(result.service) == kServicePrefix + suffix;
  });
  return it == results.end() ? nullptr : &*it;
}

std::vector<Service> registerFakeServices() {
  QVariantMap localMetadata{
      {QStringLiteral("xesam:url"), QStringLiteral("file:///tmp/Anime%20Title%20-%2001.mkv")},
      {QStringLiteral("mpris:length"), QVariant::fromValue<qlonglong>(1234567)},
  };
  QVariantMap streamMetadata{
      {QStringLiteral("xesam:url"), QStringLiteral("https://example.test/video")},
      {QStringLiteral("xesam:title"), QStringLiteral("Streamed Anime - 02")},
  };

  std::vector<Service> services;
  services.push_back(registerService(kServicePrefix + QStringLiteral("local"), "LocalPlayer",
                                     "Playing", localMetadata, 600000));
  services.push_back(registerService(kServicePrefix + QStringLiteral("stream"), "StreamPlayer",
                                     "Playing", streamMetadata, 3456789));
  services.push_back(registerService(kServicePrefix + QStringLiteral("paused"), "PausedPlayer",
                                     "Paused", localMetadata, 2222000));
  services.push_back(registerService(kServicePrefix + QStringLiteral("disabled"), "DisabledPlayer",
                                     "Playing", localMetadata, 1000000));
  services.push_back(registerService(kServicePrefix + QStringLiteral("browser"), "brave-origin",
                                     "Playing", streamMetadata));
  services.push_back(
      registerService(kServicePrefix + QStringLiteral("titleOnly"), "Browser", "Playing",
                      {{QStringLiteral("xesam:title"), QStringLiteral("Anime Title - 01")}}));
  return services;
}

void runClient() {
  const auto unfiltered = track::media::mpris::getResults({});
  expect(findResult(unfiltered, "disabled"), "disabled-player fixture has no usable metadata");
  const auto results = track::media::mpris::getResults({"DisabledPlayer"});

  const auto* local = findResult(results, "local");
  expect(local, "local player was not detected");
  expect(local->player.name == "LocalPlayer", "local player name is incorrect");
  expect(local->media.information.size() == 1, "local metadata count is incorrect");
  expect(local->media.information.front().type == anisthesia::MediaInfoType::File,
         "local media type is incorrect");
  expect(local->media.information.front().value == "/tmp/Anime Title - 01.mkv",
         "local file URL was not decoded");
  expect(local->media.duration == std::chrono::milliseconds{1234},
         "media duration was not converted");
  expect(local->media.position == std::chrono::milliseconds{600},
         "media position was not converted");

  const auto* stream = findResult(unfiltered, "stream");
  expect(stream, "remote stream was not detected");
  if (stream) {
    expect(stream->media.information.size() == 2, "remote metadata count is incorrect");
    expect(stream->media.information.front().type == anisthesia::MediaInfoType::Title,
           "remote title was not preferred");
    expect(stream->media.information.back().type == anisthesia::MediaInfoType::Url,
           "remote URL was not retained");
  }

  expect(findResult(unfiltered, "browser"), "browser video was not detected");
  expect(findResult(unfiltered, "titleOnly"), "title-only media was not detected");

  const auto* paused = findResult(results, "paused");
  expect(paused, "paused player was not detected");
  expect(paused->media.state == anisthesia::MediaState::Paused, "paused player state is incorrect");
  expect(paused->media.position == std::chrono::milliseconds{2222},
         "paused media position was not converted");
  expect(!findResult(results, "disabled"), "disabled player was incorrectly detected");
  std::puts("MPRIS probe passed");
}

void runRealClient() {
  const auto results = track::media::mpris::getResults({});
  if (results.empty()) {
    std::puts("No active MPRIS services found");
    return;
  }

  for (const auto& result : results) {
    std::printf("%s | %s | %s\n", result.service.c_str(), result.player.name.c_str(),
                result.media.information.front().value.c_str());
  }
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  if (argc > 1 && QString::fromUtf8(argv[1]) == "--server") {
    const auto services = registerFakeServices();
    std::puts("MPRIS fake services ready");
    std::fflush(stdout);
    return app.exec();
  }

  if (argc > 1 && QString::fromUtf8(argv[1]) == "--real") {
    runRealClient();
    return EXIT_SUCCESS;
  }

  if (argc > 1 && QString::fromUtf8(argv[1]) == "--self-test") {
    QProcess server;
    server.start(QCoreApplication::applicationFilePath(), {"--server"});
    expect(server.waitForStarted(5000), "fake server did not start");
    expect(server.waitForReadyRead(5000), "fake server did not become ready");
    expect(server.readAllStandardOutput().contains("MPRIS fake services ready"),
           "fake server readiness message missing");
    QProcess client;
    client.start(QCoreApplication::applicationFilePath(), {});
    const bool finished = client.waitForFinished(10000);
    const bool passed =
        finished && client.exitStatus() == QProcess::NormalExit && client.exitCode() == 0;
    std::fputs(client.readAllStandardOutput().constData(), stdout);
    std::fputs(client.readAllStandardError().constData(), stderr);
    server.terminate();
    if (!server.waitForFinished(5000)) {
      server.kill();
      server.waitForFinished(5000);
    }
    expect(passed, "client failed or timed out");
    return EXIT_SUCCESS;
  }

  runClient();
}
