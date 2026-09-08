#include <QCoreApplication>
#include <QMetaObject>
#include <QTimer>
#include <cstdio>

#include "media/anime_list_utils.hpp"
#include "recognition_fixture.hpp"
#include "sync/service.hpp"
#include "track/media.hpp"
#include "track/media_player.hpp"
#include "track/mpris.hpp"
#include "track/sharing.hpp"

using namespace std::chrono_literals;

namespace {
bool syncEnabled = true;
int saves = 0;
int synchronizations = 0;
int announcements = 0;
int failures = 0;
std::vector<track::media::mpris::Result> playerResults;

void check(bool condition, const char* message) {
  if (condition) return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  ++failures;
}
}  // namespace

// Only external boundaries are replaced: player discovery, disk persistence,
// service synchronization and sharing. Detection and recognition are real.
namespace anime {
const ListEntry* Database::entry(int id) const {
  auto it = entries_.constFind(id);
  return it == entries_.cend() ? nullptr : &it.value();
}
void Database::updateEntry(const ListEntry& entry) {
  entries_[entry.anime_id] = entry;
}
void Database::deleteEntry(int id) {
  entries_.remove(id);
}
}  // namespace anime
namespace anime::list {
void save(Entry entry) {
  ++saves;
  anime::db.updateEntry(entry);
}
}  // namespace anime::list
namespace taiga {
bool Settings::syncEnabled() const {
  return ::syncEnabled;
}
bool Settings::detectionEnabled() const {
  return true;
}
std::chrono::milliseconds Settings::mediaDetectionInterval() const {
  return 1h;
}
std::vector<std::string> Settings::disabledMediaPlayers() const {
  return {};
}
}  // namespace taiga
namespace sync_service {
bool synchronize() {
  ++synchronizations;
  return true;
}
}  // namespace sync_service
namespace track::sharing {
void update(const Episode&) {}
void announce(const Episode&) {
  ++announcements;
}
void clear() {}
}  // namespace track::sharing
namespace track::media {
bool parsePlayersData(std::vector<Player>&) {
  return true;
}
}  // namespace track::media
namespace track::media::mpris {
std::vector<Result> getResults(const std::vector<std::string>&) {
  return playerResults;
}
}  // namespace track::media::mpris

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  Anime item;
  item.id = 1;
  item.episode_count = 24;
  item.titles.romaji = "Vinland Saga";
  anime::db.updateItem(item);
  track::media::Detection detection(nullptr);
  check(detection.init(), "initialize detection");
  auto* timer = detection.findChild<QTimer*>();
  if (!timer) return 1;
  const auto poll = [&] {
    // Trigger the real timer connection without sleeping or exposing private methods.
    check(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection), "poll detection");
  };
  const auto play = [&](const char* file, std::chrono::milliseconds position,
                        std::chrono::milliseconds duration = 100s) {
    track::media::mpris::Result result;
    result.service = "test.player";
    result.player.name = "Test Player";
    result.media.state = anisthesia::MediaState::Playing;
    result.media.information.push_back({anisthesia::MediaInfoType::File, file});
    result.media.position = position;
    result.media.duration = duration;
    playerResults = {result};
    poll();
  };
  const auto expectSaves = [&](int expected, const char* message) {
    check(saves == expected && synchronizations == expected && announcements == expected, message);
  };

  play("Vinland Saga - 01.mkv", 94999ms);
  expectSaves(0, "do not save below 95 percent");
  check(detection.isMediaIdentified(), "real recognition identifies the catalog entry");
  play("Vinland Saga - 01.mkv", 95s);
  expectSaves(1, "save exactly at 95 percent");
  const auto* entry = anime::db.entry(1);
  check(entry && entry->watched_episodes == 1 && entry->status == anime::list::Status::Watching,
        "save identified episode and start watching");
  poll();
  play("Vinland Saga - 01.mkv", 100s);
  expectSaves(1, "repeated polls and end of media do not duplicate updates");

  syncEnabled = false;
  play("Vinland Saga - 02.mkv", 95s);
  expectSaves(1, "disabled synchronization blocks progress and announcements");
  syncEnabled = true;
  poll();
  expectSaves(2, "reenabling synchronization saves the current completed episode once");
  check(anime::db.entry(1)->watched_episodes == 2, "advance to episode two");
  play("Vinland Saga - 01.mkv", 100s);
  expectSaves(2, "older episodes do not move progress backward");

  play("Vinland Saga - 03.mkv", 100s, 0ms);
  expectSaves(2, "unknown duration does not complete an episode");
  play("Vinland Saga - 03.mkv", 949ms, 1s);
  expectSaves(2, "short video remains incomplete just below threshold");
  play("Vinland Saga - 03.mkv", 950ms, 1s);
  expectSaves(3, "short video completes at threshold");
  play("Unknown Show - 01.mkv", 100s);
  play("Vinland Saga.mkv", 100s);
  play("Vinland Saga - 00.mkv", 100s);
  expectSaves(3, "unidentified or invalid episode numbers cannot save progress");

  auto completed = *anime::db.entry(1);
  completed.status = anime::list::Status::Completed;
  anime::db.updateEntry(completed);
  play("Vinland Saga - 04.mkv", 95s);
  expectSaves(3, "completed list entry is not updated unless rewatching");
  completed.rewatching = true;
  anime::db.updateEntry(completed);
  poll();
  expectSaves(4, "rewatching allows progress to advance");

  play("Vinland Saga - 05.mkv", 90s);
  playerResults.front().media.state = anisthesia::MediaState::Paused;
  poll();
  check(detection.isMediaIdentified(), "pausing preserves now playing");
  expectSaves(4, "pausing before threshold does not save progress");
  detection.setEnabled(false);
  playerResults.front().media.position = 100s;
  poll();
  expectSaves(4, "disabled detection does not save progress");
  check(!detection.getCurrentEpisode(), "disabling detection clears now playing");
  detection.setEnabled(true);
  playerResults.clear();
  poll();
  check(!detection.getCurrentMedia(), "player exit clears detected media");
  expectSaves(4, "player exit does not save stale progress");
  std::printf("Media progress: %d failures\n", failures);
  return failures ? 1 : 0;
}
