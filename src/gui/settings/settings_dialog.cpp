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

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "base/string.hpp"
#include "gui/main/main_window.hpp"
#include "gui/utils/theme.hpp"
#include "sync/service.hpp"
#include "taiga/accounts.hpp"
#include "taiga/settings.hpp"
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

  auto accountsLayout = new QVBoxLayout(ui_->accountsPage);
  accountsLayout->setContentsMargins(0, 0, 0, 0);

  auto accountsDescription =
      new QLabel(tr("Choose the service used for synchronization and manage its sign-in."),
                 ui_->accountsPage);
  accountsDescription->setWordWrap(true);
  accountsLayout->addWidget(accountsDescription);

  auto accountForm = new QFormLayout;
  auto serviceBox = new QComboBox(ui_->accountsPage);
  serviceBox->addItem(sync::serviceName(sync::ServiceId::AniList), "anilist");
  serviceBox->addItem(sync::serviceName(sync::ServiceId::Kitsu), "kitsu");
  serviceBox->addItem(sync::serviceName(sync::ServiceId::MyAnimeList), "myanimelist");
  const auto currentService = QString::fromStdString(taiga::settings.service());
  const auto currentIndex = serviceBox->findData(currentService);
  if (currentIndex >= 0) serviceBox->setCurrentIndex(currentIndex);
  accountForm->addRow(tr("Service:"), serviceBox);

  auto username = new QLabel(ui_->accountsPage);
  auto status = new QLabel(ui_->accountsPage);
  accountForm->addRow(tr("Account:"), username);
  accountForm->addRow(tr("Status:"), status);
  accountsLayout->addLayout(accountForm);

  auto accountActions = new QHBoxLayout;
  auto authenticate = new QPushButton(tr("Authenticate"), ui_->accountsPage);
  auto synchronize = new QPushButton(tr("Synchronize"), ui_->accountsPage);
  accountActions->addWidget(authenticate);
  accountActions->addWidget(synchronize);
  accountActions->addStretch();
  accountsLayout->addLayout(accountActions);
  accountsLayout->addStretch();

  const auto refreshAccount = [serviceBox, username, status] {
    const auto service = sync::serviceIdFromSlug(serviceBox->currentData().toString());
    const auto serviceSlug = sync::serviceSlug(service).toStdString();
    const auto name = taiga::accounts.serviceUsername(serviceSlug);
    username->setText(name.empty() ? QObject::tr("Not configured")
                                   : QString::fromStdString(name));
    status->setText(sync::isUserAuthenticated() ? QObject::tr("Authenticated")
                                                : QObject::tr("Not authenticated"));
  };
  refreshAccount();

  connect(serviceBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
          [serviceBox, refreshAccount](const int) {
            taiga::settings.setService(serviceBox->currentData().toString().toStdString());
            refreshAccount();
          });
  connect(this, &QDialog::finished, this, [currentService](const int result) {
    if (result != QDialog::Accepted) {
      taiga::settings.setService(currentService.toStdString());
    }
  });
  connect(authenticate, &QPushButton::clicked, this, [refreshAccount] {
    sync::authenticateUser();
    refreshAccount();
  });
  connect(synchronize, &QPushButton::clicked, this, [refreshAccount] {
    sync::synchronize();
    refreshAccount();
  });

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

  auto unavailablePage = new QWidget(this);
  auto unavailableLayout = new QVBoxLayout(unavailablePage);
  auto unavailableLabel =
      new QLabel(tr("This settings section is not available yet."), unavailablePage);
  unavailableLabel->setWordWrap(true);
  unavailableLayout->addWidget(unavailableLabel);
  unavailableLayout->addStretch();
  ui_->stackedWidget->addWidget(unavailablePage);

  connect(configure, &QPushButton::clicked, this, [this] {
    accept();
    mainWindow()->configureTorrents();
  });

  connect(ui_->treeWidget, &QTreeWidget::currentItemChanged, this,
          [this, torrentsPage, unavailablePage](QTreeWidgetItem* current, QTreeWidgetItem*) {
            if (current) {
              const auto section = current->parent() ? current->parent() : current;
              if (section->text(0) == "Accounts") {
                ui_->stackedWidget->setCurrentWidget(ui_->accountsPage);
              } else if (section->text(0) == "Torrents") {
                ui_->stackedWidget->setCurrentWidget(torrentsPage);
              } else {
                ui_->stackedWidget->setCurrentWidget(unavailablePage);
              }
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
