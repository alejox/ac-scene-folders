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

#include "scene-tree-dock.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QAction>
#include <QColor>
#include <QDropEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMetaObject>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

static const char *SAVE_KEY = "ac-scene-folders";
static const int SAVE_VERSION = 1;

struct ColorPreset {
	const char *localeKey;
	const char *hex;
};

static const ColorPreset COLOR_PRESETS[] = {
	{"Color.Cyan", "#4DE8FF"},  {"Color.Amber", "#FFB454"}, {"Color.Purple", "#9B6BFF"},
	{"Color.Red", "#FF5C7A"},   {"Color.Green", "#5CE08A"}, {"Color.Blue", "#4D8DFF"},
};

static QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

static QIcon folderIcon(const QWidget *widget, const QString &color)
{
	QColor fill = color.isEmpty() ? widget->palette().color(QPalette::Mid) : QColor(color);
	if (!fill.isValid())
		fill = widget->palette().color(QPalette::Mid);

	QPixmap pixmap(32, 32);
	pixmap.fill(Qt::transparent);
	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(Qt::NoPen);
	painter.setBrush(fill);
	painter.drawRoundedRect(3, 5, 12, 6, 2, 2);  /* tab */
	painter.drawRoundedRect(3, 8, 26, 19, 4, 4); /* body */
	painter.end();
	return QIcon(pixmap);
}

static void source_renamed(void *data, calldata_t *)
{
	auto *dock = static_cast<SceneTreeDock *>(data);
	/* The signal can arrive from any thread: hop to the UI thread. */
	QMetaObject::invokeMethod(dock, "reconcile", Qt::QueuedConnection);
}

/* ------------------------------------------------------------------------- */

void SceneTreeWidget::dropEvent(QDropEvent *event)
{
	QTreeWidget::dropEvent(event);
	emit itemsMoved();
}

/* ------------------------------------------------------------------------- */

SceneTreeDock::SceneTreeDock(QWidget *parent) : QWidget(parent)
{
	search = new QLineEdit(this);
	search->setPlaceholderText(T("Search.Placeholder"));
	search->setClearButtonEnabled(true);

	favButton = new QToolButton(this);
	favButton->setText(QStringLiteral("★"));
	favButton->setCheckable(true);
	favButton->setToolTip(T("Favorites.OnlyTooltip"));

	addButton = new QToolButton(this);
	addButton->setText(QStringLiteral("+"));
	addButton->setToolTip(T("Folder.New"));

	auto *bar = new QHBoxLayout();
	bar->setContentsMargins(0, 0, 0, 0);
	bar->setSpacing(4);
	bar->addWidget(search, 1);
	bar->addWidget(favButton);
	bar->addWidget(addButton);

	tree = new SceneTreeWidget(this);
	tree->setColumnCount(1);
	tree->setHeaderHidden(true);
	tree->setIndentation(16);
	tree->setUniformRowHeights(true);
	tree->setExpandsOnDoubleClick(false);
	tree->setSelectionMode(QAbstractItemView::SingleSelection);
	tree->setDragEnabled(true);
	tree->setAcceptDrops(true);
	tree->setDropIndicatorShown(true);
	tree->setDragDropMode(QAbstractItemView::InternalMove);
	tree->setDefaultDropAction(Qt::MoveAction);
	tree->setContextMenuPolicy(Qt::CustomContextMenu);

	auto *mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(4, 4, 4, 4);
	mainLayout->setSpacing(4);
	mainLayout->addLayout(bar);
	mainLayout->addWidget(tree, 1);

	saveTimer = new QTimer(this);
	saveTimer->setSingleShot(true);
	saveTimer->setInterval(800);
	connect(saveTimer, &QTimer::timeout, this, [this]() {
		if (!closed && !cleaning)
			obs_frontend_save();
	});

	connect(search, &QLineEdit::textChanged, this, &SceneTreeDock::applyFilter);
	connect(search, &QLineEdit::returnPressed, this, &SceneTreeDock::onSearchReturn);
	connect(favButton, &QToolButton::toggled, this, &SceneTreeDock::applyFilter);
	connect(addButton, &QToolButton::clicked, this, &SceneTreeDock::newFolderAtRoot);

	connect(tree, &QTreeWidget::itemClicked, this, &SceneTreeDock::onItemClicked);
	connect(tree, &QTreeWidget::itemDoubleClicked, this, &SceneTreeDock::onItemDoubleClicked);
	connect(tree, &QTreeWidget::itemActivated, this, &SceneTreeDock::onItemActivated);
	connect(tree, &QTreeWidget::itemExpanded, this, &SceneTreeDock::onItemExpanded);
	connect(tree, &QTreeWidget::itemCollapsed, this, &SceneTreeDock::onItemCollapsed);
	connect(tree, &QTreeWidget::customContextMenuRequested, this, &SceneTreeDock::onContextMenu);
	connect(tree, &SceneTreeWidget::itemsMoved, this, &SceneTreeDock::onItemsMoved);

	signal_handler_connect(obs_get_signal_handler(), "source_rename", source_renamed, this);
}

SceneTreeDock::~SceneTreeDock()
{
	/* OBS state is released in shutdown(), triggered by the EXIT event. */
}

void SceneTreeDock::shutdown()
{
	if (closed)
		return;
	closed = true;
	saveTimer->stop();
	signal_handler_disconnect(obs_get_signal_handler(), "source_rename", source_renamed, this);
	clearPending();
}

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */

SceneTreeDock::SceneList SceneTreeDock::listScenes()
{
	SceneList result;
	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_source_t *source = scenes.sources.array[i];
		const char *uuid = obs_source_get_uuid(source);
		const char *name = obs_source_get_name(source);
		if (uuid && name)
			result.append(qMakePair(QString::fromUtf8(uuid), QString::fromUtf8(name)));
	}
	obs_frontend_source_list_free(&scenes);
	return result;
}

bool SceneTreeDock::isFolder(const QTreeWidgetItem *item)
{
	return item && item->data(0, RoleType).toInt() == Folder;
}

bool SceneTreeDock::isScene(const QTreeWidgetItem *item)
{
	return item && item->data(0, RoleType).toInt() == Scene;
}

QTreeWidgetItem *SceneTreeDock::makeFolder(const QString &name, const QString &color, bool expanded)
{
	auto *item = new QTreeWidgetItem();
	item->setData(0, RoleType, Folder);
	item->setData(0, RoleName, name);
	item->setData(0, RoleColor, color);
	item->setData(0, RoleExpanded, expanded);
	item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
	refreshItem(item);
	return item;
}

QTreeWidgetItem *SceneTreeDock::makeScene(const QString &uuid, const QString &name, bool favorite)
{
	auto *item = new QTreeWidgetItem();
	item->setData(0, RoleType, Scene);
	item->setData(0, RoleUuid, uuid);
	item->setData(0, RoleName, name);
	item->setData(0, RoleFavorite, favorite);
	/* No ItemIsDropEnabled: nothing can be dropped inside a scene. */
	item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemNeverHasChildren);
	refreshItem(item);
	return item;
}

void SceneTreeDock::refreshItem(QTreeWidgetItem *item)
{
	const QString name = item->data(0, RoleName).toString();
	if (isFolder(item)) {
		item->setText(0, name);
		item->setIcon(0, folderIcon(this, item->data(0, RoleColor).toString()));
	} else {
		const bool favorite = item->data(0, RoleFavorite).toBool();
		item->setText(0, favorite ? QStringLiteral("★ ") + name : name);
	}
}

void SceneTreeDock::markDirty()
{
	if (closed || cleaning)
		return;
	saveTimer->start();
}

/* ------------------------------------------------------------------------- */
/* Persistence: the layout lives inside the scene collection file            */

obs_data_array_t *SceneTreeDock::serialize(QTreeWidgetItem *parent) const
{
	obs_data_array_t *array = obs_data_array_create();
	for (int i = 0; i < parent->childCount(); i++) {
		QTreeWidgetItem *child = parent->child(i);
		obs_data_t *entry = obs_data_create();
		obs_data_set_string(entry, "name", child->data(0, RoleName).toString().toUtf8().constData());

		if (isFolder(child)) {
			obs_data_set_string(entry, "type", "folder");
			obs_data_set_string(entry, "color", child->data(0, RoleColor).toString().toUtf8().constData());
			obs_data_set_bool(entry, "expanded", child->data(0, RoleExpanded).toBool());
			obs_data_array_t *children = serialize(child);
			obs_data_set_array(entry, "children", children);
			obs_data_array_release(children);
		} else {
			obs_data_set_string(entry, "type", "scene");
			obs_data_set_string(entry, "uuid", child->data(0, RoleUuid).toString().toUtf8().constData());
			obs_data_set_bool(entry, "favorite", child->data(0, RoleFavorite).toBool());
		}

		obs_data_array_push_back(array, entry);
		obs_data_release(entry);
	}
	return array;
}

void SceneTreeDock::saveTo(obs_data_t *save_data)
{
	if (closed || cleaning)
		return;

	/* Layout loaded but not applied yet: write it back untouched. */
	if (hasPending) {
		if (pending)
			obs_data_set_obj(save_data, SAVE_KEY, pending);
		return;
	}

	obs_data_t *root = obs_data_create();
	obs_data_set_int(root, "version", SAVE_VERSION);
	obs_data_array_t *items = serialize(tree->invisibleRootItem());
	obs_data_set_array(root, "items", items);
	obs_data_array_release(items);
	obs_data_set_obj(save_data, SAVE_KEY, root);
	obs_data_release(root);
}

void SceneTreeDock::clearPending()
{
	if (pending) {
		obs_data_release(pending);
		pending = nullptr;
	}
	hasPending = false;
}

void SceneTreeDock::loadFrom(obs_data_t *save_data)
{
	if (closed)
		return;

	clearPending();
	pending = obs_data_get_obj(save_data, SAVE_KEY); /* may be null: first run */
	hasPending = true;
	cleaning = false;

	updating = true;
	tree->clear();
	updating = false;

	reconcile();
}

void SceneTreeDock::build(QTreeWidgetItem *parent, obs_data_array_t *array, const QHash<QString, QString> &byUuid,
			  const QHash<QString, QString> &byName, QSet<QString> &seen)
{
	const size_t count = obs_data_array_count(array);
	for (size_t i = 0; i < count; i++) {
		obs_data_t *entry = obs_data_array_item(array, i);
		const QString type = QString::fromUtf8(obs_data_get_string(entry, "type"));
		const QString name = QString::fromUtf8(obs_data_get_string(entry, "name"));

		if (type == QLatin1String("folder")) {
			obs_data_set_default_bool(entry, "expanded", true);
			QTreeWidgetItem *folder = makeFolder(name, QString::fromUtf8(obs_data_get_string(entry, "color")),
							     obs_data_get_bool(entry, "expanded"));
			parent->addChild(folder);
			obs_data_array_t *children = obs_data_get_array(entry, "children");
			if (children) {
				build(folder, children, byUuid, byName, seen);
				obs_data_array_release(children);
			}
		} else if (type == QLatin1String("scene")) {
			/* Match by UUID; fall back to the name for imported collections. */
			QString uuid = QString::fromUtf8(obs_data_get_string(entry, "uuid"));
			if (!byUuid.contains(uuid))
				uuid = byName.value(name);
			if (!uuid.isEmpty() && byUuid.contains(uuid) && !seen.contains(uuid)) {
				seen.insert(uuid);
				parent->addChild(makeScene(uuid, byUuid.value(uuid), obs_data_get_bool(entry, "favorite")));
			}
		}

		obs_data_release(entry);
	}
}

bool SceneTreeDock::tryApplyPending()
{
	if (!hasPending)
		return true;

	const SceneList scenes = listScenes();
	if (scenes.isEmpty())
		return false; /* scenes not loaded yet, try again on the next event */

	QHash<QString, QString> byUuid;
	QHash<QString, QString> byName;
	for (const auto &scene : scenes) {
		byUuid.insert(scene.first, scene.second);
		byName.insert(scene.second, scene.first);
	}

	updating = true;
	tree->clear();
	if (pending) {
		obs_data_array_t *items = obs_data_get_array(pending, "items");
		if (items) {
			QSet<QString> seen;
			build(tree->invisibleRootItem(), items, byUuid, byName, seen);
			obs_data_array_release(items);
		}
	}
	updating = false;

	clearPending();
	return true;
}

/* ------------------------------------------------------------------------- */
/* Sync with OBS                                                             */

void SceneTreeDock::reconcile()
{
	if (closed || cleaning)
		return;
	if (!tryApplyPending())
		return;

	const SceneList scenes = listScenes();
	QHash<QString, QString> byUuid;
	for (const auto &scene : scenes)
		byUuid.insert(scene.first, scene.second);

	QSet<QString> present;
	QList<QTreeWidgetItem *> stale;
	for (QTreeWidgetItemIterator it(tree); *it; ++it) {
		QTreeWidgetItem *item = *it;
		if (!isScene(item))
			continue;
		const QString uuid = item->data(0, RoleUuid).toString();
		if (!byUuid.contains(uuid) || present.contains(uuid)) {
			stale.append(item);
			continue;
		}
		present.insert(uuid);
		if (item->data(0, RoleName).toString() != byUuid.value(uuid)) {
			item->setData(0, RoleName, byUuid.value(uuid));
			refreshItem(item);
		}
	}

	updating = true;
	qDeleteAll(stale);
	for (const auto &scene : scenes) {
		if (!present.contains(scene.first))
			tree->invisibleRootItem()->addChild(makeScene(scene.first, scene.second, false));
	}
	updating = false;

	applyFilter();
	updateCurrentHighlight();
}

void SceneTreeDock::handleFrontendEvent(enum obs_frontend_event event)
{
	if (closed)
		return;

	switch (event) {
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		/* The old collection was already saved; drop its tree. */
		cleaning = true;
		saveTimer->stop();
		clearPending();
		updating = true;
		tree->clear();
		updating = false;
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		cleaning = false;
		reconcile();
		break;
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
		reconcile();
		break;
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
		updateCurrentHighlight();
		break;
	default:
		break;
	}
}

void SceneTreeDock::updateCurrentHighlight()
{
	if (closed || cleaning)
		return;

	QString programUuid;
	QString selectedUuid;

	if (obs_source_t *program = obs_frontend_get_current_scene()) {
		programUuid = QString::fromUtf8(obs_source_get_uuid(program));
		obs_source_release(program);
	}
	selectedUuid = programUuid;
	if (obs_frontend_preview_program_mode_active()) {
		if (obs_source_t *preview = obs_frontend_get_current_preview_scene()) {
			selectedUuid = QString::fromUtf8(obs_source_get_uuid(preview));
			obs_source_release(preview);
		}
	}

	QSignalBlocker blocker(tree);
	QTreeWidgetItem *selected = nullptr;
	for (QTreeWidgetItemIterator it(tree); *it; ++it) {
		QTreeWidgetItem *item = *it;
		if (!isScene(item))
			continue;
		const QString uuid = item->data(0, RoleUuid).toString();
		QFont font = item->font(0);
		font.setBold(uuid == programUuid); /* bold = scene that is live */
		item->setFont(0, font);
		if (uuid == selectedUuid)
			selected = item;
	}
	tree->setCurrentItem(selected);
}

void SceneTreeDock::activateScene(QTreeWidgetItem *item, bool forceProgram)
{
	if (!isScene(item) || closed)
		return;

	obs_source_t *source = obs_get_source_by_uuid(item->data(0, RoleUuid).toString().toUtf8().constData());
	if (!source)
		return;

	if (obs_frontend_preview_program_mode_active() && !forceProgram)
		obs_frontend_set_current_preview_scene(source);
	else
		obs_frontend_set_current_scene(source);

	obs_source_release(source);
}

/* ------------------------------------------------------------------------- */
/* Interaction                                                               */

void SceneTreeDock::onItemClicked(QTreeWidgetItem *item, int)
{
	if (isFolder(item))
		item->setExpanded(!item->isExpanded()); /* whole row toggles, not only the arrow */
	else
		activateScene(item, false);
}

void SceneTreeDock::onItemDoubleClicked(QTreeWidgetItem *item, int)
{
	/* In Studio Mode a double click sends the scene to program. */
	if (isScene(item) && obs_frontend_preview_program_mode_active())
		activateScene(item, true);
}

void SceneTreeDock::onItemActivated(QTreeWidgetItem *item, int)
{
	if (isScene(item))
		activateScene(item, false);
}

void SceneTreeDock::onItemExpanded(QTreeWidgetItem *item)
{
	if (updating || filterActive() || !isFolder(item))
		return;
	item->setData(0, RoleExpanded, true);
	markDirty();
}

void SceneTreeDock::onItemCollapsed(QTreeWidgetItem *item)
{
	if (updating || filterActive() || !isFolder(item))
		return;
	item->setData(0, RoleExpanded, false);
	markDirty();
}

void SceneTreeDock::onItemsMoved()
{
	/* Qt forgets the expanded state of moved folders. */
	if (!filterActive())
		restoreExpanded();
	applyFilter();
	updateCurrentHighlight();
	markDirty();
}

void SceneTreeDock::restoreExpanded()
{
	updating = true;
	for (QTreeWidgetItemIterator it(tree); *it; ++it) {
		if (isFolder(*it))
			(*it)->setExpanded((*it)->data(0, RoleExpanded).toBool());
	}
	updating = false;
}

bool SceneTreeDock::filterActive() const
{
	return !search->text().trimmed().isEmpty() || favButton->isChecked();
}

bool SceneTreeDock::filterItem(QTreeWidgetItem *item, const QString &text, bool favOnly)
{
	if (isScene(item)) {
		const bool matchesText = text.isEmpty() ||
					 item->data(0, RoleName).toString().contains(text, Qt::CaseInsensitive);
		const bool matchesFav = !favOnly || item->data(0, RoleFavorite).toBool();
		const bool visible = matchesText && matchesFav;
		item->setHidden(!visible);
		return visible;
	}

	bool anyChild = false;
	for (int i = 0; i < item->childCount(); i++) {
		if (filterItem(item->child(i), text, favOnly))
			anyChild = true;
	}

	const bool filtering = !text.isEmpty() || favOnly;
	const bool visible = anyChild || !filtering; /* empty folders stay visible when not filtering */
	item->setHidden(!visible);
	if (filtering && anyChild)
		item->setExpanded(true);
	return visible;
}

void SceneTreeDock::applyFilter()
{
	const QString text = search->text().trimmed();
	const bool favOnly = favButton->isChecked();

	updating = true;
	QTreeWidgetItem *root = tree->invisibleRootItem();
	for (int i = 0; i < root->childCount(); i++)
		filterItem(root->child(i), text, favOnly);
	updating = false;

	if (text.isEmpty() && !favOnly)
		restoreExpanded();
}

void SceneTreeDock::onSearchReturn()
{
	/* Enter jumps to the first scene that matches the search. */
	for (QTreeWidgetItemIterator it(tree, QTreeWidgetItemIterator::NotHidden); *it; ++it) {
		if (isScene(*it)) {
			activateScene(*it, false);
			return;
		}
	}
}

/* ------------------------------------------------------------------------- */
/* Folder actions                                                            */

void SceneTreeDock::newFolderAtRoot()
{
	createFolder(nullptr);
}

void SceneTreeDock::createFolder(QTreeWidgetItem *reference)
{
	bool ok = false;
	const QString name =
		QInputDialog::getText(this, T("Folder.New"), T("Folder.NamePrompt"), QLineEdit::Normal, T("Folder.DefaultName"), &ok)
			.trimmed();
	if (!ok || name.isEmpty())
		return;

	QTreeWidgetItem *folder = makeFolder(name, QString(), true);
	if (isFolder(reference)) {
		reference->addChild(folder);
		reference->setData(0, RoleExpanded, true);
	} else if (reference && reference->parent()) {
		QTreeWidgetItem *parent = reference->parent();
		parent->insertChild(parent->indexOfChild(reference) + 1, folder);
	} else if (reference) {
		tree->insertTopLevelItem(tree->indexOfTopLevelItem(reference) + 1, folder);
	} else {
		tree->insertTopLevelItem(0, folder);
	}

	if (!filterActive())
		restoreExpanded();
	markDirty();
}

void SceneTreeDock::renameFolder(QTreeWidgetItem *folder)
{
	bool ok = false;
	const QString name = QInputDialog::getText(this, T("Folder.Rename"), T("Folder.NamePrompt"), QLineEdit::Normal,
						   folder->data(0, RoleName).toString(), &ok)
				     .trimmed();
	if (!ok || name.isEmpty())
		return;
	folder->setData(0, RoleName, name);
	refreshItem(folder);
	markDirty();
}

void SceneTreeDock::deleteFolder(QTreeWidgetItem *folder)
{
	/* Only the folder goes away: its content moves up one level. */
	QTreeWidgetItem *parent = folder->parent() ? folder->parent() : tree->invisibleRootItem();
	const int index = parent->indexOfChild(folder);

	updating = true;
	const QList<QTreeWidgetItem *> children = folder->takeChildren();
	parent->insertChildren(index, children);
	delete folder;
	updating = false;

	if (!filterActive())
		restoreExpanded();
	applyFilter();
	updateCurrentHighlight();
	markDirty();
}

void SceneTreeDock::setFolderColor(QTreeWidgetItem *folder, const QString &color)
{
	folder->setData(0, RoleColor, color);
	refreshItem(folder);
	markDirty();
}

void SceneTreeDock::toggleFavorite(QTreeWidgetItem *scene)
{
	scene->setData(0, RoleFavorite, !scene->data(0, RoleFavorite).toBool());
	refreshItem(scene);
	applyFilter();
	markDirty();
}

void SceneTreeDock::onContextMenu(const QPoint &pos)
{
	if (closed || cleaning)
		return;

	QTreeWidgetItem *item = tree->itemAt(pos);
	QMenu menu(this);

	if (isScene(item)) {
		const bool favorite = item->data(0, RoleFavorite).toBool();
		QAction *fav = menu.addAction(favorite ? T("Favorites.Remove") : T("Favorites.Add"));
		connect(fav, &QAction::triggered, this, [this, item]() { toggleFavorite(item); });
		menu.addSeparator();
	}

	QAction *create = menu.addAction(isFolder(item) ? T("Folder.NewInside") : T("Folder.New"));
	connect(create, &QAction::triggered, this, [this, item]() { createFolder(item); });

	if (isFolder(item)) {
		QAction *rename = menu.addAction(T("Folder.Rename"));
		connect(rename, &QAction::triggered, this, [this, item]() { renameFolder(item); });

		QMenu *colors = menu.addMenu(T("Folder.Color"));
		QAction *none = colors->addAction(T("Color.None"));
		connect(none, &QAction::triggered, this, [this, item]() { setFolderColor(item, QString()); });
		for (const ColorPreset &preset : COLOR_PRESETS) {
			const QString hex = QString::fromLatin1(preset.hex);
			QAction *action = colors->addAction(folderIcon(this, hex), T(preset.localeKey));
			connect(action, &QAction::triggered, this, [this, item, hex]() { setFolderColor(item, hex); });
		}

		menu.addSeparator();
		QAction *removeAction = menu.addAction(T("Folder.Delete"));
		connect(removeAction, &QAction::triggered, this, [this, item]() { deleteFolder(item); });
	}

	menu.addSeparator();
	QAction *expand = menu.addAction(T("Tree.ExpandAll"));
	connect(expand, &QAction::triggered, this, [this]() { tree->expandAll(); });
	QAction *collapse = menu.addAction(T("Tree.CollapseAll"));
	connect(collapse, &QAction::triggered, this, [this]() { tree->collapseAll(); });

	menu.exec(tree->viewport()->mapToGlobal(pos));
}
