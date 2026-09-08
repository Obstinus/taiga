#include "track/recognition.hpp"

#include <QCoreApplication>
#include <QDirIterator>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cstdio>

#include "media/anime_db.hpp"
#include "taiga/settings.hpp"
#include "track/episode.hpp"
#include "track/recognition_cache.hpp"
#include "track/recognition_relations.hpp"

#include "recognition_fixture.hpp"

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  int failures = 0;
  const auto add = [](int id, const std::string& title) {
    Anime item;
    item.id = id;
    item.episode_count = 24;
    item.titles.romaji = title;
    anime::db.updateItem(item);
  };
  add(1, "Vinland Saga");
  add(2, "Vinland Saga Season 2");
  add(3, "Example 3rd Season");
  add(4, "Another II");
  QTemporaryDir temporary;
  if (!temporary.isValid()) return 1;
  const auto libraryPath = temporary.path() + "/Vinland Saga";
  testLibraryFolders = {libraryPath.toStdString()};
  const auto check = [&](QString path, int expected) {
    if (path.startsWith("/library/")) {
      path.replace(0, 8, libraryPath);
      if (!QDir{}.mkpath(QFileInfo(path).absolutePath())) {
        ++failures;
        return;
      }
    }
    auto episode = track::recognition::parseFileInfo(QFileInfo(path));
    const int actual = track::recognition::identify(episode);
    if (actual != expected) {
      std::fprintf(stderr, "%s: expected %d, got %d\n", qPrintable(path), expected, actual);
      ++failures;
    }
  };
  check("[Foxtrot] Vinland Saga S2 - 01 [BD 1080p FLAC] [Dual Audio] [DE35CA5F].mkv", 2);
  check("Vinland Saga S02E04.mkv", 2);
  check("Vinland Saga 2nd Season - 24.mkv", 2);
  check("Vinland Saga - 01.mkv", 1);
  check("Vinland Saga S1 - 01.mkv", 1);
  check("Vinland Saga S3 - 01.mkv", 0);
  check("Vinland Saga S2 - 25.mkv", 0);
  check("Vinland Saga S2 - NCOP 01.mkv", 0);
  check("Vinland Saga S2 - 06.5.mkv", 2);
  check("Example S03 - 01.mkv", 3);
  check("Another S2 - 01.mkv", 4);
  check("/library/01.mkv", 0);
  check("/library/Season 2/01.mkv", 0);
  check("/library/Vinland Saga/Season 2/01.mkv", 2);
  check("/library/Vinland Saga/Season 2/Vinland Saga - 01.mkv", 2);
  check("/library/[Foxtrot] Vinland Saga S2 [BD 1080p]/01.mkv", 2);
  check("/library/Vinland Saga S2/Vinland Saga S1 - 01.mkv", 1);
  check("/library/Example S3/Vinland Saga - 01.mkv", 1);
  check("Vinland Saga S2", 2);

  // A missing sequel must never fall back to the first-season catalog entry.
  track::recognition::cache()->remove(*anime::db.item(2));
  check("Vinland Saga S2 - 01.mkv", 0);
  track::recognition::cache()->add(*anime::db.item(2));

  int localFiles = 0;
  if (argc > 1) {
    QDirIterator files(QString::fromLocal8Bit(argv[1]), {"*.mkv"}, QDir::Files,
                       QDirIterator::Subdirectories);
    while (files.hasNext()) {
      const auto path = files.next();
      if (!path.contains("Vinland Saga S2")) continue;
      check(path, path.contains("NCOP") || path.contains("NCED") ? 0 : 2);
      ++localFiles;
    }
    if (localFiles == 0) ++failures;
  }
  std::printf("Recognition: %d failures; %d local Vinland Saga files checked\n", failures,
              localFiles);
  return failures ? 1 : 0;
}
