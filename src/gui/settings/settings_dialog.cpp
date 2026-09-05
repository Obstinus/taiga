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

#include "settings_dialog.hpp"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "base/string.hpp"
#include "gui/main/main_window.hpp"
#include "gui/utils/theme.hpp"
#include "ui_settings_dialog.h"

#ifdef Q_OS_WINDOWS
#include "gui/platforms/windows.hpp"
#endif

namespace gui {

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent), ui_(new Ui::SettingsDialog) {
  ui_->setupUi(this);

#ifdef Q_OS_WINDOWS
  enableMicaBackground(this);
#endif

  ui_->treeWidget->setIndentation(22);

  const auto add_item = [this](QString icon, QString text) {
    auto item = new QTreeWidgetItem(ui_->treeWidget, QStringList(text));
    item->setIcon(0, theme.getIcon(icon));
    item->setSizeHint(0, QSize{0, 24});
    return item;
  };

  const auto add_child = [this](QTreeWidgetItem* parent, QString text) {
    new QTreeWidgetItem(parent, QStringList(text));
  };

  add_item("account_circle", "Accounts");
  add_item("web_asset", "Application");
  add_item("list_alt", "Anime List");
  add_item("folder", "Library");
  {
    auto item = add_item("check_circle", "Recognition");
    add_child(item, "Media players");
    add_child(item, "Streaming");
  }
  {
    auto item = add_item("share", "Sharing");
    add_child(item, "Discord");
    add_child(item, "HTTP");
    add_child(item, "mIRC");
  }
  {
    auto item = add_item("rss_feed", "Torrents");
    add_child(item, "Downloads");
    add_child(item, "Filters");
  }
  {
    auto item = add_item("warning", "Advanced");
    add_child(item, "Cache");
  }

  ui_->treeWidget->expandAll();

  auto torrentsPage = new QWidget(this);
  auto torrentsLayout = new QVBoxLayout(torrentsPage);
  auto description =
      new QLabel(tr("Configure RSS sources, torrent file downloads, filters and refresh interval."),
                 torrentsPage);
  description->setWordWrap(true);
  torrentsLayout->addWidget(description);
  auto configure = new QPushButton(tr("Configure torrents..."), torrentsPage);
  torrentsLayout->addWidget(configure, 0, Qt::AlignLeft);
  torrentsLayout->addStretch();
  ui_->stackedWidget->addWidget(torrentsPage);
  connect(configure, &QPushButton::clicked, this, [this] {
    accept();
    mainWindow()->configureTorrents();
  });

  connect(ui_->treeWidget, &QTreeWidget::currentItemChanged, this,
          [this, torrentsPage](QTreeWidgetItem* current, QTreeWidgetItem*) {
            if (current) {
              const auto section = current->parent() ? current->parent() : current;
              ui_->stackedWidget->setCurrentWidget(
                  section->text(0) == "Torrents" ? torrentsPage : ui_->accountsPage);
              auto text = current->text(0);
              if (current->parent()) {
                text = u"%1 / %2"_s.arg(current->parent()->text(0), text);
              }
              ui_->titleLabel->setText(text);
            }
          });
}

void SettingsDialog::show(QWidget* parent) {
  auto dlg = new SettingsDialog(parent);
  dlg->setAttribute(Qt::WA_DeleteOnClose);
  dlg->setModal(true);
  dlg->QDialog::show();
}

}  // namespace gui
