#include "recognition_fixture.hpp"

std::vector<std::string> testLibraryFolders;

// In-memory catalog and settings keep these tests independent of user data.
// Parsing, cache construction, matching and episode validation are production code.
namespace anime {
Database::Database() = default;
const Anime* Database::item(int id) const {
  auto it = items_.constFind(id);
  return it == items_.cend() ? nullptr : &it.value();
}
const QMap<int, Anime>& Database::items() const {
  return items_;
}
const Settings* Database::settings(int) const {
  return nullptr;
}
void Database::updateItem(const Anime& item) {
  items_[item.id] = item;
}
}  // namespace anime
namespace taiga {
QString Settings::fileName() const {
  return {};
}
std::vector<std::string> Settings::libraryFolders() const {
  return testLibraryFolders;
}
}  // namespace taiga
namespace track::recognition {
std::optional<EpisodeRedirection> findEpisodeRedirection(int, const Episode&) {
  return std::nullopt;
}
}  // namespace track::recognition
