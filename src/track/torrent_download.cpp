#include "torrent_download.hpp"

#include <QDir>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QTimer>
#include <limits>
#include <memory>

namespace {

constexpr qsizetype kMaxTorrentBytes = 16 * 1024 * 1024;

// Bencoding contains binary strings, so never convert metainfo through text.
class BencodeReader {
public:
  explicit BencodeReader(const QByteArray& data) : m_data(data) {}

  bool torrent() {
    if (!consume('d')) return false;
    bool info = false;
    while (peek() != 'e') {
      QByteArray key;
      if (!string(key)) return false;
      if (key == "info") {
        if (info || peek() != 'd') return false;
        info = infoDictionary();
        if (!info) return false;
      } else if (!value(1)) {
        return false;
      }
    }
    return consume('e') && m_pos == m_data.size() && info;
  }

private:
  char peek() const {
    return m_pos < m_data.size() ? m_data[m_pos] : '\0';
  }
  bool consume(char c) {
    if (peek() != c) return false;
    ++m_pos;
    return true;
  }
  bool string(QByteArray& out) {
    const auto start = m_pos;
    qsizetype length = 0;
    while (peek() >= '0' && peek() <= '9') {
      if (length > m_data.size() / 10) return false;
      length = length * 10 + (m_data[m_pos++] - '0');
      if (length > m_data.size()) return false;
    }
    if (m_pos == start || (m_pos - start > 1 && m_data[start] == '0') || !consume(':') ||
        length > m_data.size() - m_pos)
      return false;
    out = m_data.mid(m_pos, length);
    m_pos += length;
    return true;
  }
  bool integer(qint64* out = nullptr) {
    if (!consume('i')) return false;
    const auto start = m_pos;
    if (peek() == '-') ++m_pos;
    const auto digits = m_pos;
    while (peek() >= '0' && peek() <= '9') ++m_pos;
    if (digits == m_pos || (m_pos - digits > 1 && m_data[digits] == '0') ||
        (digits != start && m_data[digits] == '0'))
      return false;
    bool ok = false;
    const auto number = m_data.mid(start, m_pos - start).toLongLong(&ok);
    if (!ok || !consume('e')) return false;
    if (out) *out = number;
    return true;
  }
  bool value(int depth) {
    if (depth > 64) return false;
    if (peek() == 'i') return integer();
    if (peek() == 'l' || peek() == 'd') {
      const bool dictionary = m_data[m_pos++] == 'd';
      while (peek() != 'e') {
        QByteArray key;
        if (dictionary && !string(key)) return false;
        if (!value(depth + 1)) return false;
      }
      return consume('e');
    }
    QByteArray ignored;
    return string(ignored);
  }
  bool infoDictionary() {
    if (!consume('d')) return false;
    bool name = false, pieces = false, length = false, files = false, tree = false;
    qint64 pieceLength = 0, version = 0, totalLength = 0, pieceCount = 0;
    QSet<QByteArray> keys;
    while (peek() != 'e') {
      QByteArray key;
      if (!string(key) || keys.contains(key)) return false;
      keys.insert(key);
      if (key == "name" || key == "pieces") {
        QByteArray bytes;
        if (!string(bytes)) return false;
        if (key == "name")
          name = !bytes.isEmpty();
        else {
          pieces = bytes.size() % 20 == 0;
          pieceCount = bytes.size() / 20;
        }
      } else if (key == "piece length" || key == "meta version" || key == "length") {
        qint64 number;
        if (!integer(&number)) return false;
        if (key == "piece length")
          pieceLength = number;
        else if (key == "meta version")
          version = number;
        else {
          length = number >= 0;
          totalLength = number;
        }
      } else if (key == "files") {
        if (!fileList(totalLength)) return false;
        files = true;
      } else if (key == "file tree") {
        if (peek() != 'd' || !value(1)) return false;
        tree = true;
      } else if (!value(1)) {
        return false;
      }
    }
    if (!consume('e') || !name || pieceLength <= 0) return false;
    const bool v1 = pieces && (length != files) &&
                    pieceCount == totalLength / pieceLength + (totalLength % pieceLength != 0);
    return v1 || (version == 2 && tree);
  }

  bool fileList(qint64& totalLength) {
    if (!consume('l') || peek() == 'e') return false;
    totalLength = 0;
    while (peek() != 'e') {
      if (!consume('d')) return false;
      bool length = false, path = false;
      QSet<QByteArray> keys;
      while (peek() != 'e') {
        QByteArray key;
        if (!string(key) || keys.contains(key)) return false;
        keys.insert(key);
        if (key == "length") {
          qint64 number;
          if (!integer(&number) || number < 0 ||
              number > std::numeric_limits<qint64>::max() - totalLength)
            return false;
          totalLength += number;
          length = true;
        } else if (key == "path") {
          if (!consume('l') || peek() == 'e') return false;
          while (peek() != 'e') {
            QByteArray component;
            if (!string(component) || component.isEmpty()) return false;
          }
          if (!consume('e')) return false;
          path = true;
        } else if (!value(2)) {
          return false;
        }
      }
      if (!consume('e') || !length || !path) return false;
    }
    return consume('e');
  }

  const QByteArray& m_data;
  qsizetype m_pos = 0;
};

}  // namespace

namespace track {

bool isTorrentMetainfo(const QByteArray& data) {
  return !data.isEmpty() && data.size() <= kMaxTorrentBytes && BencodeReader(data).torrent();
}

TorrentDownloader::TorrentDownloader(QObject* parent) : QObject(parent), m_network(this) {}

void TorrentDownloader::download(const TorrentItem& item, const QString& directory) {
  if (m_pending.contains(item.id)) return;
  // RSS enclosures can use download endpoints without a .torrent suffix.
  // HTTP responses are validated as metainfo before being handed to a client.
  const bool http = item.downloadUrl.isValid() && !item.downloadUrl.host().isEmpty() &&
                    (item.downloadUrl.scheme() == "http" || item.downloadUrl.scheme() == "https");
  if (!http && !isTorrentDownloadUrl(item.downloadUrl)) {
    emit failed(item.id, tr("This item has no valid torrent or magnet link."));
    return;
  }
  if (item.downloadUrl.scheme() == "magnet") {
    emit succeeded(item.id, item.downloadUrl);
    return;
  }
  if (!QDir::isAbsolutePath(directory) || !QDir().mkpath(directory)) {
    emit failed(item.id, tr("Cannot create the torrent download directory."));
    return;
  }
  m_pending.insert(item.id);
  QNetworkRequest request(item.downloadUrl);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setMaximumRedirectsAllowed(5);
  request.setTransferTimeout(30000);
  request.setRawHeader("User-Agent", "Taiga/2.0");
  auto* reply = m_network.get(request);
  reply->setReadBufferSize(kMaxTorrentBytes + 1);
  auto data = std::make_shared<QByteArray>();
  auto* timer = new QTimer(reply);
  timer->setSingleShot(true);
  connect(timer, &QTimer::timeout, reply, [reply] {
    reply->setProperty("torrentError", tr("Torrent download timed out."));
    reply->abort();
  });
  timer->start(60000);
  const auto read = [reply, data] {
    data->append(reply->read(kMaxTorrentBytes + 1 - data->size()));
    if (data->size() > kMaxTorrentBytes) {
      reply->setProperty("torrentError", tr("Torrent file exceeds the 16 MiB limit."));
      reply->abort();
    }
  };
  connect(reply, &QNetworkReply::readyRead, this, read);
  connect(reply, &QNetworkReply::finished, this, [this, reply, timer, data, item, directory, read] {
    timer->stop();
    read();
    m_pending.remove(item.id);
    reply->deleteLater();
    QString error = reply->property("torrentError").toString();
    if (error.isEmpty() && reply->error() != QNetworkReply::NoError) error = reply->errorString();
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (error.isEmpty() && (status < 200 || status >= 300))
      error = tr("HTTP error %1.").arg(status);
    if (error.isEmpty() && !isTorrentMetainfo(*data))
      error = tr("The server did not return a valid torrent file.");
    if (!error.isEmpty()) {
      emit failed(item.id, error);
      return;
    }
    QString name = item.title;
    name.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N} ._()-]")), "_");
    name = name.left(80).trimmed();
    if (name.isEmpty() || name.startsWith('.')) name.prepend("torrent-");
    QTemporaryFile file(QDir(directory).filePath(name + "-XXXXXX.torrent"));
    if (!file.open() || file.write(*data) != data->size() || !file.flush()) {
      emit failed(item.id, tr("Cannot save torrent file: %1").arg(file.errorString()));
      return;
    }
    file.setAutoRemove(false);
    const auto url = QUrl::fromLocalFile(file.fileName());
    file.close();
    emit succeeded(item.id, url);
  });
}

}  // namespace track
