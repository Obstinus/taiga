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

#include "library_menu.hpp"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QUrl>

#include "gui/media/media_dialog.hpp"
#include "gui/media/media_menu.hpp"
#include "gui/utils/theme.hpp"
#include "gui/utils/widgets.hpp"
#include "media/anime_db.hpp"

namespace gui {

LibraryMenu::LibraryMenu(QWidget* parent, const QString& path, int anime_id)
    : QMenu(parent), m_path(path), m_anime_id(anime_id) {
  setAttribute(Qt::WA_DeleteOnClose);
}

void LibraryMenu::popup() {
  if (m_path.isEmpty()) return;

  addAction(QIcon(m_path), tr("Open"), tr("Enter"), this, &LibraryMenu::open);
  addSeparator();
  addAction(theme.getIcon("delete"), tr("Delete"), tr("Del"), this, &LibraryMenu::remove);
  addAction(theme.getIcon("edit"), tr("Rename"), tr("F2"), this, &LibraryMenu::rename);

  if (const auto item = anime::db.item(m_anime_id)) {
    addSeparator();
    addAction(theme.getIcon("info"), tr("Details"), this, &LibraryMenu::viewDetails);
  }

  QMenu::popup(QCursor::pos());
}

void LibraryMenu::open() const {
  QDesktopServices::openUrl(QUrl::fromLocalFile(m_path));
}

void LibraryMenu::remove() const {
  const QFileInfo info{m_path};
  if (!info.exists()) return;

  const auto name = info.fileName();
  if (!confirm(parentWidget(), tr("Move this item to the Trash?"), name, tr("Move to Trash"))) {
    return;
  }

  if (!QFile::moveToTrash(m_path)) {
    QMessageBox::critical(parentWidget(), tr("Delete failed"),
                          tr("Taiga could not move %1 to the Trash.").arg(name));
  }
}

void LibraryMenu::rename() const {
  const QFileInfo info{m_path};
  if (!info.exists()) return;

  bool accepted = false;
  const auto name = QInputDialog::getText(parentWidget(), tr("Rename"), tr("New name:"),
                                          QLineEdit::Normal, info.fileName(), &accepted);
  if (!accepted) return;

  const auto newName = name.trimmed();
  if (newName.isEmpty() || newName == info.fileName() || newName.contains('/') ||
      newName.contains('\\')) {
    if (newName.isEmpty() || newName.contains('/') || newName.contains('\\')) {
      QMessageBox::warning(parentWidget(), tr("Rename failed"), tr("Enter a valid file name."));
    }
    return;
  }

  const auto newPath = info.dir().filePath(newName);
  if (QFileInfo::exists(newPath)) {
    QMessageBox::warning(parentWidget(), tr("Rename failed"),
                         tr("An item named %1 already exists.").arg(newName));
    return;
  }

  if (!QFile::rename(m_path, newPath)) {
    QMessageBox::critical(parentWidget(), tr("Rename failed"),
                          tr("Taiga could not rename %1.").arg(info.fileName()));
  }
}

void LibraryMenu::viewDetails() const {
  const auto item = anime::db.item(m_anime_id);
  if (!item) return;

  MediaDialog::show(parentWidget(), MediaDialogPage::Details, *item);
}

}  // namespace gui
