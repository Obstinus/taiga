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

#include "accounts.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

#include "base/string.hpp"
#include "sync/anilist/anilist_ratings.hpp"
#include "sync/kitsu/kitsu_ratings.hpp"
#include "taiga/path.hpp"

namespace {

// No secret is passed in argv, the environment, or log/error messages.
// QProcess keeps the UI responsive while the keyring may ask to unlock.
void secretTool(QObject* owner, const QString& operation, const std::string& token,
                std::function<void(bool, std::string)> done) {
#ifdef Q_OS_LINUX
  const auto executable = QStandardPaths::findExecutable("secret-tool", {"/usr/bin", "/bin"});
  if (executable.isEmpty()) {
    QTimer::singleShot(0, owner, [done] { done(false, {}); });
    return;
  }
  const auto profile =
      QCryptographicHash::hash(
          QDir(QString::fromStdString(taiga::get_data_path())).absolutePath().toUtf8(),
          QCryptographicHash::Sha256)
          .toHex();
  QStringList arguments{operation};
  if (operation == "store") arguments << "--label=Taiga AniList access token";
  arguments << "application" << "moe.taiga" << "account" << "anilist"
            << "profile" << QString::fromLatin1(profile);
  auto process = new QProcess(owner);
  auto timer = new QTimer(process);
  timer->setSingleShot(true);
  QObject::connect(timer, &QTimer::timeout, process, [process] { process->kill(); });
  QObject::connect(process, &QProcess::errorOccurred, process,
                   [process, done](QProcess::ProcessError error) {
                     if (error != QProcess::FailedToStart) return;
                     process->deleteLater();
                     done(false, {});
                   });
  QObject::connect(
      process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process,
      [process, timer, operation, done](int code, QProcess::ExitStatus status) {
        timer->stop();
        auto output = process->readAllStandardOutput();
        const auto errors = process->readAllStandardError();
        const bool ok = status == QProcess::NormalExit &&
                        (code == 0 || ((operation == "lookup" || operation == "clear") &&
                                       code == 1 && errors.isEmpty()));
        // secret-tool prints one trailing newline; tokens themselves are not trimmed.
        if (output.endsWith('\n')) output.chop(1);
        auto value = ok && operation == "lookup" ? output.toStdString() : std::string{};
        output.fill('\0');
        process->deleteLater();
        done(ok, std::move(value));
      });
  process->start(executable, arguments);
  if (operation == "store") process->write(QByteArray::fromStdString(token));
  process->closeWriteChannel();
  timer->start(30000);
#else
  Q_UNUSED(operation);
  Q_UNUSED(token);
  QTimer::singleShot(0, owner, [done] { done(false, {}); });
#endif
}

}  // namespace

namespace taiga {

Accounts::Accounts() : QObject{} {}

QString Accounts::fileName() const {
  return u"%1/accounts.json"_s.arg(QString::fromStdString(get_data_path()));
}

bool Accounts::protectCredentials() const {
  const auto path = fileName();
  const auto directory = QFileInfo(path).absolutePath();
  if (QFileInfo(directory).isSymLink() || QFileInfo(path).isSymLink()) return false;
  if (!QDir().mkpath(directory)) return false;
  const auto owner = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
  if (!QFile::setPermissions(directory, owner | QFileDevice::ExeOwner)) return false;
  return !QFile::exists(path) || QFile::setPermissions(path, owner);
}

bool Accounts::removeLegacyAnilistToken() const {
  if (!protectCredentials()) return false;
  auto config = settings();
  config.remove("anilist.token");
  config.remove("anilist.authenticated");
  config.sync();
  return config.status() == QSettings::NoError && protectCredentials();
}

void Accounts::loadAnilistToken(std::function<void()> ready) {
  if (ready) tokenWaiters_.push_back(std::move(ready));
  if (loadingToken_) return;
  const auto finish = [this] {
    loadingToken_ = false;
    auto waiters = std::move(tokenWaiters_);
    tokenWaiters_.clear();
    for (const auto& callback : waiters) callback();
  };
  if (anilistToken_) {
    finish();
    return;
  }
  loadingToken_ = true;
  if (!protectCredentials()) {
    emit credentialStorageError(
        tr("Could not restrict access to the account file. "
           "The saved AniList token was not read."));
    finish();
    return;
  }
  const auto legacy = value("anilist.token").toString().toStdString();
  if (!legacy.empty()) {
    storeAnilistToken(legacy, [finish](bool) { finish(); });
    return;
  }
  secretTool(this, "lookup", {}, [this, finish](bool ok, std::string token) {
    if (ok)
      anilistToken_ = std::move(token);
    else
      emit credentialStorageError(
          tr("Could not read the system keyring. Unlock the keyring "
             "and ensure secret-tool is installed. No token was written to disk."));
    finish();
  });
}

void Accounts::storeAnilistToken(const std::string& token, std::function<void(bool)> done) {
  if (savingToken_) {
    pendingStores_.push_back([this, token, done] { storeAnilistToken(token, done); });
    return;
  }
  savingToken_ = true;
  anilistToken_ = token;
  anilistAuthenticated_ = false;
  const auto complete = [this, done](bool ok) {
    if (!ok)
      emit credentialStorageError(
          tr("The AniList token could not be saved securely. "
             "It is available for this session only. If an old token exists in accounts.json, "
             "it remains there with restricted access until migration succeeds."));
    if (done) done(ok);
    savingToken_ = false;
    if (!pendingStores_.empty()) {
      auto next = std::move(pendingStores_.front());
      pendingStores_.erase(pendingStores_.begin());
      next();
    }
  };
  if (!protectCredentials()) {
    complete(false);
    return;
  }
  secretTool(this, token.empty() ? "clear" : "store", token,
             [this, token, complete](bool ok, std::string) {
               if (!ok) {
                 complete(false);
                 return;
               }
               // Verify the persisted value before removing the only legacy copy.
               secretTool(this, "lookup", {}, [this, token, complete](bool ok, std::string stored) {
                 complete(ok && stored == token && removeLegacyAnilistToken());
               });
             });
}

////////////////////////////////////////////////////////////////////////////////

bool Accounts::anilistAuthenticated() const {
  return anilistAuthenticated_;
}

sync::anilist::RatingSystem Accounts::anilistRatingSystem() const {
  const auto ratingSystem = value("anilist.ratingSystem").toString();
  return sync::anilist::parseRatingSystem(ratingSystem);
}

std::string Accounts::anilistUsername() const {
  return value("anilist.username").toString().toStdString();
}

std::string Accounts::anilistToken() const {
  return anilistToken_.value_or(std::string{});
}

bool Accounts::kitsuAuthenticated() const {
  return value("kitsu.authenticated").toBool();
}

std::string Accounts::kitsuAccessToken() const {
  return value("kitsu.accessToken").toString().toStdString();
}

std::string Accounts::kitsuDisplayName() const {
  return value("kitsu.displayName").toString().toStdString();
}

std::string Accounts::kitsuEmail() const {
  return value("kitsu.email").toString().toStdString();
}

sync::kitsu::RatingSystem Accounts::kitsuRatingSystem() const {
  const auto ratingSystem = value("kitsu.ratingSystem").toString();
  return sync::kitsu::parseRatingSystem(ratingSystem);
}

std::string Accounts::kitsuRefreshToken() const {
  return value("kitsu.refreshToken").toString().toStdString();
}

std::string Accounts::kitsuUserId() const {
  return value("kitsu.userId").toString().toStdString();
}

std::string Accounts::kitsuUsername() const {
  return value("kitsu.username").toString().toStdString();
}

std::string Accounts::kitsuPassword() const {
  return value("kitsu.password").toString().toStdString();
}

bool Accounts::myanimelistAuthenticated() const {
  return value("myanimelist.authenticated").toBool();
}

std::string Accounts::myanimelistUsername() const {
  return value("myanimelist.username").toString().toStdString();
}

std::string Accounts::myanimelistAccessToken() const {
  return value("myanimelist.accessToken").toString().toStdString();
}

std::string Accounts::myanimelistRefreshToken() const {
  return value("myanimelist.refreshToken").toString().toStdString();
}

////////////////////////////////////////////////////////////////////////////////

void Accounts::setAnilistAuthenticated(bool authenticated) {
  anilistAuthenticated_ = authenticated;
  emit authenticationChanged(authenticated);
}

void Accounts::setAnilistRatingSystem(const std::string& ratingSystem) const {
  setValue("anilist.ratingSystem", ratingSystem);
}

void Accounts::setAnilistUsername(const std::string& username) const {
  setValue("anilist.username", username);
}

void Accounts::setAnilistToken(const std::string& token) const {
  const_cast<Accounts*>(this)->storeAnilistToken(token);
}

void Accounts::setKitsuAuthenticated(bool authenticated) {
  setValue("kitsu.authenticated", authenticated);
  emit authenticationChanged(authenticated);
}

void Accounts::setKitsuAccessToken(const std::string& accessToken) const {
  setValue("kitsu.accessToken", accessToken);
}

void Accounts::setKitsuDisplayName(const std::string& displayName) const {
  setValue("kitsu.displayName", displayName);
}

void Accounts::setKitsuEmail(const std::string& email) const {
  setValue("kitsu.email", email);
}

void Accounts::setKitsuRatingSystem(const std::string& ratingSystem) const {
  setValue("kitsu.ratingSystem", ratingSystem);
}

void Accounts::setKitsuRefreshToken(const std::string& refreshToken) const {
  setValue("kitsu.refreshToken", refreshToken);
}

void Accounts::setKitsuUserId(const std::string& userId) const {
  setValue("kitsu.userId", userId);
}

void Accounts::setKitsuUsername(const std::string& username) const {
  setValue("kitsu.username", username);
}

void Accounts::setKitsuPassword(const std::string& password) const {
  setValue("kitsu.password", password);
}

void Accounts::setMyanimelistAuthenticated(bool authenticated) {
  setValue("myanimelist.authenticated", authenticated);
  emit authenticationChanged(authenticated);
}

void Accounts::setMyanimelistUsername(const std::string& username) const {
  setValue("myanimelist.username", username);
}

void Accounts::setMyanimelistAccessToken(const std::string& accessToken) const {
  setValue("myanimelist.accessToken", accessToken);
}

void Accounts::setMyanimelistRefreshToken(const std::string& refreshToken) const {
  setValue("myanimelist.refreshToken", refreshToken);
}

////////////////////////////////////////////////////////////////////////////////

std::string Accounts::serviceUsername(const std::string& service) const {
  if (service == "anilist") return anilistUsername();
  if (service == "kitsu") return kitsuUsername();
  if (service == "myanimelist") return myanimelistUsername();
  return {};
}

}  // namespace taiga
