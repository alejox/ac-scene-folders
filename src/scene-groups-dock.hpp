/*
AC Scene Groups
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

#include <QHash>
#include <QList>
#include <QListWidget>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QWidget>

class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabBar;
class QTimer;
class QToolButton;

/* List that reports when the user finishes a drag & drop reorder. */
class SceneListWidget : public QListWidget {
	Q_OBJECT

public:
	explicit SceneListWidget(QWidget *parent = nullptr) : QListWidget(parent) {}

signals:
	void orderChanged();

protected:
	void dropEvent(QDropEvent *event) override;
};

class SceneGroupsDock : public QWidget {
	Q_OBJECT

public:
	explicit SceneGroupsDock(QWidget *parent = nullptr);
	~SceneGroupsDock() override;

	void handleFrontendEvent(enum obs_frontend_event event);
	void saveTo(obs_data_t *save_data);
	void loadFrom(obs_data_t *save_data);
	void shutdown();

public slots:
	/* Syncs groups and the visible list with the real OBS scene list. */
	void reconcile();

private:
	struct Group {
		QString id;
		QString name;
		QString color;
		QString profile;    /* linked OBS profile, empty = none */
		QStringList scenes; /* scene UUIDs, in the group's own order */
	};

	using SceneList = QList<QPair<QString, QString>>; /* uuid, name */

	static SceneList listScenes();
	static bool sourceAlive(const QString &uuid);
	static bool nameExists(const QString &name);

	Group *currentGroup();
	Group *findGroup(const QString &id);
	int sharedCount(const QString &uuid) const;
	QStringList selectedUuids() const;

	void rebuildTabs();
	void rebuildList();
	void updateIndicators();
	void updateSubheader(int count);
	void markDirty();

	void onTabChanged(int index);
	void applyLinkedProfile(const Group &group);
	void onItemClicked(QListWidgetItem *item);
	void onItemDoubleClicked(QListWidgetItem *item);
	void onItemChanged(QListWidgetItem *item);
	void onOrderChanged();
	void onContextMenu(const QPoint &pos);
	void activateScene(const QString &uuid, bool forceProgram);

	void promptNewGroup(const QStringList &scenesToAdd);
	Group *addGroup(const QString &name, const QString &color);
	void editCurrentGroup();
	void setMembership(const QStringList &uuids, const QString &groupId, bool member);

	void beginNewScene();
	void commitNewScene();
	void cancelNewScene();
	void duplicateScene(const QString &uuid);
	void deleteScenes(const QStringList &uuids);

	bool tryApplyPending();
	void clearPending();
	void showDockFirstTime();

	QTabBar *tabs = nullptr;
	QToolButton *addGroupButton = nullptr;
	QFrame *firstRun = nullptr;
	QLabel *subLabel = nullptr;
	QToolButton *gearButton = nullptr;
	QLineEdit *searchEdit = nullptr;
	SceneListWidget *list = nullptr;
	QLineEdit *newSceneEdit = nullptr;
	QLabel *newSceneHint = nullptr;
	QLabel *studioHint = nullptr;
	QPushButton *newSceneButton = nullptr;
	QToolButton *searchButton = nullptr;
	QTimer *saveTimer = nullptr;

	QList<Group> groups;
	QString activeGroupId; /* empty = "All" */
	QString profileNote;   /* shown when a linked profile could not be applied */

	obs_data_t *pending = nullptr; /* saved layout waiting for scenes to exist */
	bool hasPending = false;
	bool cleaning = false; /* scene collection is being torn down */
	bool updating = false; /* programmatic list change, ignore item signals */
	bool closed = false;
};
