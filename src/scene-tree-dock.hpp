/*
AC Scene Folders
Copyright (C) 2026 alejoxcode

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

#include <obs.h>
#include <obs-frontend-api.h>

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QTreeWidget>
#include <QWidget>

class QLineEdit;
class QTimer;
class QToolButton;

/* Tree that reports when the user finishes a drag & drop reorder. */
class SceneTreeWidget : public QTreeWidget {
	Q_OBJECT

public:
	explicit SceneTreeWidget(QWidget *parent = nullptr) : QTreeWidget(parent) {}

signals:
	void itemsMoved();

protected:
	void dropEvent(QDropEvent *event) override;
};

class SceneTreeDock : public QWidget {
	Q_OBJECT

public:
	explicit SceneTreeDock(QWidget *parent = nullptr);
	~SceneTreeDock() override;

	void handleFrontendEvent(enum obs_frontend_event event);
	void saveTo(obs_data_t *save_data);
	void loadFrom(obs_data_t *save_data);
	void shutdown();

public slots:
	/* Syncs the tree with the real OBS scene list (added, removed, renamed). */
	void reconcile();

private slots:
	void onItemClicked(QTreeWidgetItem *item, int column);
	void onItemDoubleClicked(QTreeWidgetItem *item, int column);
	void onItemActivated(QTreeWidgetItem *item, int column);
	void onItemExpanded(QTreeWidgetItem *item);
	void onItemCollapsed(QTreeWidgetItem *item);
	void onItemsMoved();
	void onContextMenu(const QPoint &pos);
	void onSearchReturn();
	void applyFilter();
	void newFolderAtRoot();

private:
	enum ItemType { Folder = 1, Scene = 2 };
	enum Roles {
		RoleType = Qt::UserRole,
		RoleUuid,
		RoleName,
		RoleColor,
		RoleFavorite,
		RoleExpanded,
	};

	using SceneList = QList<QPair<QString, QString>>; /* uuid, name */

	static SceneList listScenes();
	static bool isFolder(const QTreeWidgetItem *item);
	static bool isScene(const QTreeWidgetItem *item);

	QTreeWidgetItem *makeFolder(const QString &name, const QString &color, bool expanded);
	QTreeWidgetItem *makeScene(const QString &uuid, const QString &name, bool favorite);
	void refreshItem(QTreeWidgetItem *item);

	QTreeWidgetItem *createFolder(QTreeWidgetItem *reference);
	QList<QTreeWidgetItem *> topLevelSelection(QTreeWidgetItem *fallback) const;
	void moveItems(const QList<QTreeWidgetItem *> &items, QTreeWidgetItem *target);
	void collectFolders(QTreeWidgetItem *parent, const QString &prefix,
			    QList<QPair<QString, QTreeWidgetItem *>> &out) const;
	int updateFolderBadge(QTreeWidgetItem *folder, const QString &programUuid, bool &containsLive);
	void renameFolder(QTreeWidgetItem *folder);
	void deleteFolder(QTreeWidgetItem *folder);
	void setFolderColor(QTreeWidgetItem *folder, const QString &color);
	void toggleFavorite(QTreeWidgetItem *scene);
	void activateScene(QTreeWidgetItem *item, bool forceProgram);

	obs_data_array_t *serialize(QTreeWidgetItem *parent) const;
	void build(QTreeWidgetItem *parent, obs_data_array_t *array, const QHash<QString, QString> &byUuid,
		   const QHash<QString, QString> &byName, QSet<QString> &seen);
	bool tryApplyPending();
	void clearPending();

	bool filterItem(QTreeWidgetItem *item, const QString &text, bool favOnly);
	bool filterActive() const;
	void restoreExpanded();
	void updateCurrentHighlight();
	void markDirty();

	SceneTreeWidget *tree = nullptr;
	QLineEdit *search = nullptr;
	QToolButton *favButton = nullptr;
	QToolButton *addButton = nullptr;
	QTimer *saveTimer = nullptr;
	QWidget *hint = nullptr; /* first-run help, shown while there are no folders */

	QTreeWidgetItem *lastToggled = nullptr; /* only compared, never dereferenced */
	QElapsedTimer toggleTimer;

	obs_data_t *pending = nullptr; /* saved layout waiting for scenes to exist */
	bool hasPending = false;
	bool cleaning = false; /* scene collection is being torn down */
	bool updating = false; /* programmatic change, ignore expand/collapse signals */
	bool closed = false;
};
