/**
 * Taiga
 * Copyright (C) 2010-2026, Eren Okka
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "sharing.hpp"

#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QUrl>
#include <QUuid>
#ifdef Q_OS_LINUX
#include <QLocalSocket>
#endif
#include <memory>

#include "base/log.hpp"
#include "media/anime_db.hpp"
#include "media/anime_utils.hpp"
#include "sync/service.hpp"
#include "taiga/network.hpp"
#include "taiga/settings.hpp"
#include "track/episode.hpp"

namespace track::sharing {

namespace {

struct EpisodeDetails {
  QString title;
  QString episode;
  QString total;
  QString url;
};

EpisodeDetails details(const Episode& episode) {
  const auto* item = anime::db.item(episode.animeId());
  const auto title = item ? anime::preferredTitle(*item)
                          : episode.element(anitomy::ElementKind::Title, "Unknown anime");

  EpisodeDetails result{
      .title = QString::fromStdString(title),
      .episode = QString::fromStdString(episode.element(anitomy::ElementKind::Episode, "?")),
      .total = item && item->episode_count > 0 ? QString::number(item->episode_count) : "?",
      .url = {},
  };
  if (item) result.url = sync_service::animePageUrl(item->id);
  return result;
}

bool isPrivate(const Episode& episode) {
  const auto* entry = anime::db.entry(episode.animeId());
  return entry && entry->is_private;
}

QString formatMessage(const std::string& format, const Episode& episode) {
  const auto info = details(episode);
  auto result = QString::fromStdString(format);
  result.replace("%title%", info.title);
  result.replace("%episode%", info.episode);
  result.replace("%total%", info.total);
  result.replace("%url%", info.url);
  return result.trimmed();
}

void sendHttp(const Episode& episode) {
  if (!taiga::settings.httpSharingEnabled()) return;

  const QUrl url{QString::fromStdString(taiga::settings.httpSharingUrl())};
  if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https") ||
      url.host().isEmpty()) {
    qWarning() << "Ignoring invalid HTTP sharing URL:" << url;
    return;
  }

  const auto message = formatMessage(taiga::settings.httpSharingFormat(), episode);
  if (message.isEmpty()) return;

  QNetworkRequest request{url};
  request.setHeader(QNetworkRequest::ContentTypeHeader,
                    QStringLiteral("text/plain; charset=utf-8"));
  request.setRawHeader("User-Agent", "Taiga-Linux");
  auto* reply = taiga::network()->post(request, message.toUtf8());
  QObject::connect(reply, &QNetworkReply::finished, reply, [reply] {
    if (reply->error() != QNetworkReply::NoError) {
      qWarning() << "HTTP sharing failed:" << reply->errorString();
    }
  });
}

QByteArray ircSafe(const QString& value) {
  auto safe = value;
  safe.replace(QRegularExpression(QStringLiteral(R"([\r\n])")), QStringLiteral(" "));
  return safe.toUtf8();
}

void sendIrc(const Episode& episode) {
  if (!taiga::settings.ircSharingEnabled()) return;

  const auto server = QString::fromStdString(taiga::settings.ircServer()).trimmed();
  auto channel = QString::fromStdString(taiga::settings.ircChannel()).trimmed();
  auto nickname = QString::fromStdString(taiga::settings.ircNickname()).trimmed();
  const auto message = formatMessage(taiga::settings.ircFormat(), episode);
  const auto port = taiga::settings.ircPort();

  if (server.isEmpty() || channel.isEmpty() || nickname.isEmpty() || message.isEmpty() ||
      port < 1 || port > 65535)
    return;

  if (!channel.startsWith('#') && !channel.startsWith('&')) channel.prepend('#');
  nickname.replace(QRegularExpression(QStringLiteral(R"([^A-Za-z0-9_\[\]\\`^{}|-])")), "_");
  if (nickname.isEmpty()) nickname = "Taiga";

  QTcpSocket socket;
  socket.connectToHost(server, static_cast<quint16>(port));
  if (!socket.waitForConnected(1500)) {
    qWarning() << "IRC sharing could not connect:" << socket.errorString();
    return;
  }

  const auto nick = ircSafe(nickname);
  socket.write("NICK " + nick + "\r\n");
  socket.write("USER " + nick + " 0 * :Taiga\r\n");
  socket.write("JOIN " + ircSafe(channel) + "\r\n");

  const auto body = ircSafe(message);
  QByteArray actionPrefix(1, char(1));
  actionPrefix += "ACTION ";
  QByteArray actionSuffix(1, char(1));
  actionSuffix += "\r\n";
  const auto command =
      taiga::settings.ircUseAction()
          ? QByteArrayLiteral("PRIVMSG ") + ircSafe(channel) + " :" + actionPrefix + body +
                actionSuffix
          : QByteArrayLiteral("PRIVMSG ") + ircSafe(channel) + " :" + body + "\r\n";
  socket.write(command);
  socket.waitForBytesWritten(1500);
  socket.disconnectFromHost();
}

#ifdef Q_OS_LINUX
QByteArray frame(const quint32 opcode, const QByteArray& payload) {
  QByteArray result;
  QDataStream stream{&result, QIODevice::WriteOnly};
  stream.setByteOrder(QDataStream::LittleEndian);
  stream << opcode << static_cast<quint32>(payload.size());
  result.append(payload);
  return result;
}

bool sendDiscordFrame(QLocalSocket& socket, const quint32 opcode, const QJsonObject& object) {
  const auto payload = QJsonDocument{object}.toJson(QJsonDocument::Compact);
  const auto data = frame(opcode, payload);
  return socket.write(data) == data.size() && socket.waitForBytesWritten(500);
}

std::unique_ptr<QLocalSocket> connectDiscord() {
  QStringList paths;
  const auto runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
  if (!runtime.isEmpty()) {
    for (int i = 0; i < 10; ++i)
      paths.push_back(runtime + QStringLiteral("/discord-ipc-%1").arg(i));
  }
  for (int i = 0; i < 10; ++i) paths.push_back(QStringLiteral("/tmp/discord-ipc-%1").arg(i));

  for (const auto& path : paths) {
    auto socket = std::make_unique<QLocalSocket>();
    socket->connectToServer(path);
    if (socket->waitForConnected(100)) return socket;
  }
  return nullptr;
}

void setDiscordActivity(const EpisodeDetails* info) {
  const auto applicationId =
      QString::fromStdString(taiga::settings.discordApplicationId()).trimmed();
  if (applicationId.isEmpty()) return;

  auto socket = connectDiscord();
  if (!socket) return;

  if (!sendDiscordFrame(*socket, 0, QJsonObject{{"v", 1}, {"client_id", applicationId}})) return;
  socket->waitForReadyRead(100);
  socket->readAll();

  QJsonObject args{{"pid", QCoreApplication::applicationPid()}};
  if (info) {
    QJsonObject timestamps;
    timestamps.insert("start", QDateTime::currentSecsSinceEpoch());
    QJsonObject activity;
    activity.insert("details", info->title);
    activity.insert("state", QStringLiteral("Episode %1/%2").arg(info->episode, info->total));
    activity.insert("timestamps", timestamps);
    if (!info->url.isEmpty()) {
      QJsonObject button;
      button.insert("label", "View Anime");
      button.insert("url", info->url);
      activity.insert("buttons", QJsonArray{button});
    }
    args.insert("activity", activity);
  } else {
    args.insert("activity", QJsonValue::Null);
  }

  sendDiscordFrame(*socket, 1,
                   QJsonObject{{"cmd", "SET_ACTIVITY"},
                               {"args", args},
                               {"nonce", QUuid::createUuid().toString(QUuid::WithoutBraces)}});
}
#else
void setDiscordActivity(const EpisodeDetails*) {}
#endif

}  // namespace

void update(const Episode& episode) {
  if (!taiga::settings.sharingEnabled() || !taiga::settings.discordSharingEnabled() ||
      isPrivate(episode))
    return;
  const auto info = details(episode);
  setDiscordActivity(&info);
}

void announce(const Episode& episode) {
  if (!taiga::settings.sharingEnabled() || isPrivate(episode)) return;
  sendHttp(episode);
  sendIrc(episode);
}

void clear() {
  setDiscordActivity(nullptr);
}

}  // namespace track::sharing
