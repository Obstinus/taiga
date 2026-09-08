#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>

#include "taiga/accounts.hpp"

namespace {
QString dataPath;
int failures = 0;
void check(bool ok, const char* message) {
  if (ok) return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  ++failures;
}
void waitFor(const std::function<void(std::function<void()>)>& operation) {
  QEventLoop loop;
  bool completed = false;
  QTimer timer;
  timer.setSingleShot(true);
  QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
  timer.start(35000);
  operation([&] { completed = true; loop.quit(); });
  if (!completed) loop.exec();
  check(completed, "credential operation timed out");
}
bool hasLegacyToken() {
  QFile file(dataPath + "/accounts.json");
  if (!file.open(QIODevice::ReadOnly)) return false;
  return QJsonDocument::fromJson(file.readAll()).object().contains("anilist.token");
}
void seedLegacy() {
  QFile file(dataPath + "/accounts.json");
  check(file.open(QIODevice::WriteOnly), "create legacy fixture");
  file.write(R"({"anilist.token":"test-only-token","anilist.username":"example","anilist.authenticated":true})");
  file.close();
  file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadOther);
}
}
namespace taiga {
std::string get_data_path() { return dataPath.toStdString(); }
}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir temporary;
  if (!temporary.isValid()) return 1;
  dataPath = temporary.path() + "/data";
  check(QDir().mkpath(dataPath), "create data directory");
  check(QFile::setPermissions(dataPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
      QFileDevice::ExeOwner | QFileDevice::ReadGroup | QFileDevice::WriteGroup |
      QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::WriteOther |
      QFileDevice::ExeOther), "seed permissive directory");
  const bool keyring = app.arguments().contains("--keyring");
  if (!keyring) qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent-taiga-test-bus");
  seedLegacy();
  taiga::Accounts accounts;
  int errors = 0;
  QObject::connect(&accounts, &taiga::Accounts::credentialStorageError,
                   [&](const QString& message) {
    ++errors;
    check(!message.contains("test-only-token"), "errors must not expose the token");
  });
  check(!accounts.anilistAuthenticated(), "persisted authentication flag is not trusted");
  waitFor([&](auto done) { accounts.loadAnilistToken(done); });
  check(accounts.anilistToken() == "test-only-token", "migration preserves session credential");
  check(hasLegacyToken() != keyring, "legacy removed only after verified keyring persistence");
  const auto modeMask = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
      QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
      QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
  const auto privateFile = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
  check((QFile::permissions(dataPath + "/accounts.json") & modeMask) == privateFile,
        "account file mode is 0600");
  check((QFile::permissions(dataPath) & modeMask) == (privateFile | QFileDevice::ExeOwner),
        "data directory mode is 0700");
  check(errors == (keyring ? 0 : 1), "storage failure is reported");
  if (keyring) {
    taiga::Accounts reopened;
    waitFor([&](auto done) { reopened.loadAnilistToken(done); });
    check(reopened.anilistToken() == "test-only-token", "token survives restart through keyring");
    waitFor([&](auto done) {
      reopened.storeAnilistToken("replacement-test-token", [done](bool ok) {
        check(ok, "replace stored token"); done();
      });
    });
    taiga::Accounts replaced;
    waitFor([&](auto done) { replaced.loadAnilistToken(done); });
    check(replaced.anilistToken() == "replacement-test-token", "replacement loaded from keyring");
    waitFor([&](auto done) {
      replaced.storeAnilistToken({}, [done](bool ok) { check(ok, "clear stored token"); done(); });
    });
    taiga::Accounts cleared;
    waitFor([&](auto done) { cleared.loadAnilistToken(done); });
    check(cleared.anilistToken().empty(), "cleared token does not reappear");
  } else {
    QFile::remove(dataPath + "/accounts.json");
    waitFor([&](auto done) {
      accounts.storeAnilistToken("new-test-token", [done](bool ok) {
        check(!ok, "unavailable keyring fails closed"); done();
      });
    });
    check(!hasLegacyToken(), "failed storage never writes new token to JSON");
    check(accounts.anilistToken() == "new-test-token", "failed storage allows session-only use");
  }
  std::printf("accounts security: %d failure(s)\n", failures);
  return failures ? 1 : 0;
}
