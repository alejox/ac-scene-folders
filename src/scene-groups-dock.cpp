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

#include "scene-groups-dock.hpp"
#include "update-check.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDropEvent>
#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTimer>
#include <QTreeWidgetItemIterator>
#include <QToolButton>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

static const char *SAVE_KEY = "ac-scene-groups";
static const char *LEGACY_KEY = "ac-scene-folders"; /* v0.1/v0.2 folder tree */
static const int SAVE_VERSION = 3;

enum ItemRoles {
	RoleUuid = Qt::UserRole, /* scene UUID, or folder id */
	RoleFolder,              /* true on folder rows */
	RoleFolderColor,
	RoleCount,
	RoleLive,
	RolePreview,
	RoleShared,
};

struct ColorPreset {
	const char *localeKey;
	const char *hex;
};

static const ColorPreset COLOR_PRESETS[] = {
	{"Color.Cyan", "#4DE8FF"},  {"Color.Amber", "#FFB454"}, {"Color.Purple", "#B197FF"},
	{"Color.Green", "#5CE08A"}, {"Color.Red", "#FF5C6C"},   {"Color.Blue", "#4D8DFF"},
};
static const int COLOR_PRESET_COUNT = int(sizeof(COLOR_PRESETS) / sizeof(COLOR_PRESETS[0]));

static const QColor LIVE_COLOR(229, 72, 77);
static const QColor PREVIEW_COLOR(255, 180, 84);
static const QColor PILL_TEXT(27, 29, 34);

static QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

static QIcon dotIcon(const QString &color)
{
	QPixmap pixmap(24, 24);
	pixmap.fill(Qt::transparent);
	QColor fill(color);
	if (fill.isValid()) {
		QPainter painter(&pixmap);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setPen(Qt::NoPen);
		painter.setBrush(fill);
		painter.drawEllipse(6, 6, 12, 12);
		painter.end();
	}
	return QIcon(pixmap);
}

static QIcon folderIcon(const QString &color, const QColor &fallback)
{
	QColor fill(color);
	if (!fill.isValid())
		fill = fallback;
	QPixmap pixmap(32, 32);
	pixmap.fill(Qt::transparent);
	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(Qt::NoPen);
	painter.setBrush(fill);
	painter.drawRoundedRect(3, 6, 12, 6, 2, 2);
	painter.drawRoundedRect(3, 9, 26, 18, 4, 4);
	painter.end();
	return QIcon(pixmap);
}

static void source_renamed(void *data, calldata_t *)
{
	auto *dock = static_cast<SceneGroupsDock *>(data);
	/* The signal can arrive from any thread: hop to the UI thread. */
	QMetaObject::invokeMethod(dock, "reconcile", Qt::QueuedConnection);
}

/* ------------------------------------------------------------------------- */
/* Row painting: name, "N groups" chip, LIVE / PREVIEW pill                  */

class SceneRowDelegate : public QStyledItemDelegate {
public:
	explicit SceneRowDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

	QString liveText;
	QString previewText;
	QString sharedText; /* "%1 groups" */

	QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
	{
		QSize size = QStyledItemDelegate::sizeHint(option, index);
		size.setHeight(qMax(size.height(), 32));
		return size;
	}

	void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
	{
		if (index.data(RoleFolder).toBool()) {
			paintFolder(painter, option, index);
			return;
		}

		QStyleOptionViewItem opt = option;
		initStyleOption(&opt, index);
		const QString name = opt.text;
		const bool live = index.data(RoleLive).toBool();
		const bool preview = index.data(RolePreview).toBool();
		const int shared = index.data(RoleShared).toInt();

		/* Theme-native background (hover, selection), without the text. */
		opt.text.clear();
		QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
		style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

		painter->save();
		painter->setRenderHint(QPainter::Antialiasing);

		const QRect row = option.rect.adjusted(3, 2, -3, -2);
		if (live || preview) {
			QColor tint = live ? LIVE_COLOR : PREVIEW_COLOR;
			tint.setAlpha(live ? 48 : 40);
			painter->setPen(Qt::NoPen);
			painter->setBrush(tint);
			painter->drawRoundedRect(row, 6, 6);
		}

		QFont small = opt.font;
		if (small.pointSizeF() > 0)
			small.setPointSizeF(small.pointSizeF() * 0.82);
		else if (small.pixelSize() > 0)
			small.setPixelSize(qMax(9, int(small.pixelSize() * 0.82)));

		int right = row.right() - 6;
		const bool selected = opt.state.testFlag(QStyle::State_Selected);
		const QColor textColor = opt.palette.color(selected ? QPalette::HighlightedText : QPalette::Text);

		if (live || preview) {
			QFont pillFont = small;
			pillFont.setBold(true);
			const QFontMetrics metrics(pillFont);
			const QString text = live ? liveText : previewText;
			const int width = metrics.horizontalAdvance(text) + 14;
			const int height = metrics.height() + 4;
			const QRect pill(right - width, row.center().y() - height / 2, width, height);
			painter->setPen(Qt::NoPen);
			painter->setBrush(live ? LIVE_COLOR : PREVIEW_COLOR);
			painter->drawRoundedRect(pill, height / 2.0, height / 2.0);
			painter->setFont(pillFont);
			painter->setPen(PILL_TEXT);
			painter->drawText(pill, Qt::AlignCenter, text);
			right = pill.left() - 6;
		}

		if (shared > 1) {
			const QFontMetrics metrics(small);
			const QString text = sharedText.arg(shared);
			const int width = metrics.horizontalAdvance(text) + 12;
			const int height = metrics.height() + 2;
			const QRect chip(right - width, row.center().y() - height / 2, width, height);
			QColor muted = textColor;
			muted.setAlpha(150);
			painter->setBrush(Qt::NoBrush);
			painter->setPen(muted);
			painter->drawRoundedRect(chip, height / 2.0, height / 2.0);
			painter->setFont(small);
			painter->drawText(chip, Qt::AlignCenter, text);
			right = chip.left() - 6;
		}

		QFont nameFont = opt.font;
		nameFont.setBold(live);
		const QRect textRect(row.left() + 8, row.top(), qMax(0, right - row.left() - 8), row.height());
		painter->setFont(nameFont);
		painter->setPen(textColor);
		painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
				  QFontMetrics(nameFont).elidedText(name, Qt::ElideRight, textRect.width()));

		painter->restore();
	}

	/* Folder row: colored folder, name, live dot when closed over the live scene, scene count. */
	void paintFolder(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
	{
		QStyleOptionViewItem opt = option;
		initStyleOption(&opt, index);
		const QString name = opt.text;
		opt.text.clear();
		opt.icon = QIcon();
		QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
		style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

		painter->save();
		painter->setRenderHint(QPainter::Antialiasing);
		const QRect row = option.rect.adjusted(3, 2, -3, -2);
		const bool selected = opt.state.testFlag(QStyle::State_Selected);
		const QColor textColor = opt.palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
		QColor muted = textColor;
		muted.setAlpha(150);

		const int iconSize = 16;
		const QRect iconRect(row.left() + 4, row.center().y() - iconSize / 2, iconSize, iconSize);
		folderIcon(index.data(RoleFolderColor).toString(), muted).paint(painter, iconRect);

		QFont small = opt.font;
		if (small.pointSizeF() > 0)
			small.setPointSizeF(small.pointSizeF() * 0.85);
		int right = row.right() - 6;
		const QString count = QString::number(index.data(RoleCount).toInt());
		const QFontMetrics smallMetrics(small);
		const int countWidth = smallMetrics.horizontalAdvance(count);
		painter->setFont(small);
		painter->setPen(muted);
		painter->drawText(QRect(right - countWidth, row.top(), countWidth, row.height()),
				  Qt::AlignVCenter | Qt::AlignRight, count);
		right -= countWidth + 8;

		const bool open = opt.state.testFlag(QStyle::State_Open);
		if (index.data(RoleLive).toBool() && !open) {
			painter->setPen(Qt::NoPen);
			painter->setBrush(LIVE_COLOR);
			painter->drawEllipse(QPoint(right - 4, row.center().y()), 4, 4);
			right -= 14;
		}

		QFont nameFont = opt.font;
		nameFont.setBold(true);
		const QRect textRect(iconRect.right() + 8, row.top(), qMax(0, right - iconRect.right() - 8),
				     row.height());
		painter->setFont(nameFont);
		painter->setPen(textColor);
		painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
				  QFontMetrics(nameFont).elidedText(name, Qt::ElideRight, textRect.width()));
		painter->restore();
	}
};

/* ------------------------------------------------------------------------- */

void SceneTreeWidget::dropEvent(QDropEvent *event)
{
	/* Folders only live at the top level. */
	bool movingFolder = false;
	const QList<QTreeWidgetItem *> selection = selectedItems();
	for (const QTreeWidgetItem *item : selection) {
		if (item->data(0, RoleFolder).toBool())
			movingFolder = true;
	}
	if (movingFolder) {
		QTreeWidgetItem *target = itemAt(event->position().toPoint());
		QTreeWidgetItem *parentTarget = nullptr;
		if (target)
			parentTarget = dropIndicatorPosition() == QAbstractItemView::OnItem ? target : target->parent();
		if (parentTarget) {
			event->ignore();
			return;
		}
	}

	QTreeWidget::dropEvent(event);
	emit orderChanged();
}

/* ------------------------------------------------------------------------- */

SceneGroupsDock::SceneGroupsDock(QWidget *parent) : QWidget(parent)
{
	/* Group selector */
	tabs = new QTabBar(this);
	tabs->setExpanding(false);
	tabs->setDrawBase(false);
	tabs->setDocumentMode(true);
	tabs->setUsesScrollButtons(true);
	tabs->setElideMode(Qt::ElideRight);
	tabs->setIconSize(QSize(12, 12));

	addGroupButton = new QToolButton(this);
	addGroupButton->setText(QStringLiteral("+"));
	addGroupButton->setToolTip(T("Group.New"));
	addGroupButton->setAccessibleName(T("Group.New"));

	auto *selector = new QHBoxLayout();
	selector->setContentsMargins(0, 0, 0, 0);
	selector->setSpacing(4);
	selector->addWidget(tabs, 1);
	selector->addWidget(addGroupButton);

	/* Update notice */
	updateBanner = new QFrame(this);
	updateBanner->setFrameShape(QFrame::StyledPanel);
	updateLabel = new QLabel(updateBanner);
	updateLabel->setWordWrap(true);
	auto *updateGet = new QPushButton(T("Update.Download"), updateBanner);
	auto *updateSkip = new QPushButton(T("Update.Skip"), updateBanner);
	updateSkip->setFlat(true);
	auto *updateButtons = new QHBoxLayout();
	updateButtons->setSpacing(6);
	updateButtons->addWidget(updateGet);
	updateButtons->addWidget(updateSkip);
	updateButtons->addStretch(1);
	auto *updateLayout = new QVBoxLayout(updateBanner);
	updateLayout->setContentsMargins(10, 8, 10, 8);
	updateLayout->setSpacing(6);
	updateLayout->addWidget(updateLabel);
	updateLayout->addLayout(updateButtons);
	updateBanner->setVisible(false);

	updates = new UpdateChecker(this);
	connect(updates, &UpdateChecker::updateAvailable, this,
		[this](const QString &version, const QString &url) { showUpdate(version, url); });
	connect(updateGet, &QPushButton::clicked, this, [this]() {
		if (!updateUrl.isEmpty())
			QDesktopServices::openUrl(QUrl(updateUrl));
	});
	connect(updateSkip, &QPushButton::clicked, this, [this]() {
		updates->dismiss(updateVersion);
		updateBanner->setVisible(false);
	});

	/* First run card */
	firstRun = new QFrame(this);
	firstRun->setFrameShape(QFrame::StyledPanel);
	auto *firstTitle = new QLabel(T("FirstRun.Title"), firstRun);
	QFont titleFont = firstTitle->font();
	titleFont.setBold(true);
	firstTitle->setFont(titleFont);
	auto *firstBody = new QLabel(T("FirstRun.Body"), firstRun);
	firstBody->setWordWrap(true);
	auto *suggestStream =
		new QPushButton(dotIcon(QString::fromLatin1(COLOR_PRESETS[0].hex)), T("Group.SuggestStream"), firstRun);
	auto *suggestRecording = new QPushButton(dotIcon(QString::fromLatin1(COLOR_PRESETS[1].hex)),
						 T("Group.SuggestRecording"), firstRun);
	auto *suggestEmpty = new QPushButton(T("FirstRun.Empty"), firstRun);
	suggestEmpty->setFlat(true);
	auto *suggestRow = new QHBoxLayout();
	suggestRow->setSpacing(6);
	suggestRow->addWidget(suggestStream);
	suggestRow->addWidget(suggestRecording);
	auto *firstLayout = new QVBoxLayout(firstRun);
	firstLayout->setContentsMargins(10, 10, 10, 10);
	firstLayout->setSpacing(8);
	firstLayout->addWidget(firstTitle);
	firstLayout->addWidget(firstBody);
	firstLayout->addLayout(suggestRow);
	firstLayout->addWidget(suggestEmpty);
	firstRun->setVisible(false);

	/* Subheader */
	subLabel = new QLabel(this);
	subLabel->setEnabled(false); /* muted, theme-native */
	gearButton = new QToolButton(this);
	gearButton->setText(QStringLiteral("⚙"));
	gearButton->setAutoRaise(true);
	gearButton->setToolTip(T("Group.Settings"));
	gearButton->setAccessibleName(T("Group.Settings"));
	auto *sub = new QHBoxLayout();
	sub->setContentsMargins(4, 0, 0, 0);
	sub->addWidget(subLabel, 1);
	sub->addWidget(gearButton);

	searchEdit = new QLineEdit(this);
	searchEdit->setPlaceholderText(T("Search.Placeholder"));
	searchEdit->setClearButtonEnabled(true);
	searchEdit->setVisible(false);

	/* Scene list */
	list = new SceneTreeWidget(this);
	list->setColumnCount(1);
	list->setHeaderHidden(true);
	list->setIndentation(14);
	list->setRootIsDecorated(true);
	list->setExpandsOnDoubleClick(false);
	auto *delegate = new SceneRowDelegate(list);
	delegate->liveText = T("Badge.Live");
	delegate->previewText = T("Badge.Preview");
	delegate->sharedText = T("Badge.Shared");
	list->setItemDelegate(delegate);
	list->setSelectionMode(QAbstractItemView::ExtendedSelection);
	list->setEditTriggers(QAbstractItemView::EditKeyPressed);
	list->setContextMenuPolicy(Qt::CustomContextMenu);
	list->setDefaultDropAction(Qt::MoveAction);
	list->setDropIndicatorShown(true);
	list->setUniformRowHeights(true);

	newSceneEdit = new QLineEdit(this);
	newSceneEdit->setPlaceholderText(T("Scene.NamePlaceholder"));
	newSceneEdit->setVisible(false);
	newSceneHint = new QLabel(this);
	newSceneHint->setWordWrap(true);
	newSceneHint->setVisible(false);

	studioHint = new QLabel(T("Studio.Hint"), this);
	studioHint->setEnabled(false);
	studioHint->setVisible(false);

	/* Footer */
	newSceneButton = new QPushButton(T("Scene.New"), this);
	newFolderButton = new QToolButton(this);
	newFolderButton->setIcon(folderIcon(QString(), palette().color(QPalette::ButtonText)));
	newFolderButton->setToolTip(T("Folder.New"));
	newFolderButton->setAccessibleName(T("Folder.New"));
	searchButton = new QToolButton(this);
	searchButton->setText(T("Search.Button"));
	searchButton->setCheckable(true);
	searchButton->setToolTip(T("Search.Tooltip"));
	searchButton->setAccessibleName(T("Search.Tooltip"));
	auto *footer = new QHBoxLayout();
	footer->setContentsMargins(0, 0, 0, 0);
	footer->setSpacing(4);
	footer->addWidget(newSceneButton, 1);
	footer->addWidget(newFolderButton);
	footer->addWidget(searchButton);

	auto *mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(4, 4, 4, 4);
	mainLayout->setSpacing(4);
	mainLayout->addLayout(selector);
	mainLayout->addWidget(updateBanner);
	mainLayout->addWidget(firstRun);
	mainLayout->addLayout(sub);
	mainLayout->addWidget(searchEdit);
	mainLayout->addWidget(list, 1);
	mainLayout->addWidget(newSceneEdit);
	mainLayout->addWidget(newSceneHint);
	mainLayout->addWidget(studioHint);
	mainLayout->addLayout(footer);

	saveTimer = new QTimer(this);
	saveTimer->setSingleShot(true);
	saveTimer->setInterval(800);
	connect(saveTimer, &QTimer::timeout, this, [this]() {
		if (!closed && !cleaning)
			obs_frontend_save();
	});

	connect(tabs, &QTabBar::currentChanged, this, [this](int index) { onTabChanged(index); });
	connect(addGroupButton, &QToolButton::clicked, this, [this]() { promptNewGroup(QStringList()); });
	connect(gearButton, &QToolButton::clicked, this, [this]() { editCurrentGroup(); });
	connect(suggestStream, &QPushButton::clicked, this,
		[this]() { addGroup(T("Group.SuggestStream"), QString::fromLatin1(COLOR_PRESETS[0].hex)); });
	connect(suggestRecording, &QPushButton::clicked, this,
		[this]() { addGroup(T("Group.SuggestRecording"), QString::fromLatin1(COLOR_PRESETS[1].hex)); });
	connect(suggestEmpty, &QPushButton::clicked, this, [this]() { promptNewGroup(QStringList()); });

	connect(searchButton, &QToolButton::toggled, this, [this](bool on) {
		searchEdit->setVisible(on);
		if (on)
			searchEdit->setFocus();
		else
			searchEdit->clear();
		rebuildList();
	});
	connect(searchEdit, &QLineEdit::textChanged, this, [this]() { rebuildList(); });

	connect(list, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int) { onItemClicked(item); });
	connect(list, &QTreeWidget::itemDoubleClicked, this,
		[this](QTreeWidgetItem *item, int) { onItemDoubleClicked(item); });
	connect(list, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int) { onItemChanged(item); });
	connect(list, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *item) { onFolderToggled(item, true); });
	connect(list, &QTreeWidget::itemCollapsed, this,
		[this](QTreeWidgetItem *item) { onFolderToggled(item, false); });
	connect(list, &QTreeWidget::customContextMenuRequested, this,
		[this](const QPoint &pos) { onContextMenu(pos); });
	connect(list, &SceneTreeWidget::orderChanged, this, [this]() { onOrderChanged(); });

	connect(newSceneButton, &QPushButton::clicked, this, [this]() { beginNewScene(); });
	connect(newFolderButton, &QToolButton::clicked, this, [this]() { newFolder(); });
	connect(newSceneEdit, &QLineEdit::returnPressed, this, [this]() { commitNewScene(); });

	auto *cancelNew = new QAction(newSceneEdit);
	cancelNew->setShortcut(Qt::Key_Escape);
	cancelNew->setShortcutContext(Qt::WidgetShortcut);
	newSceneEdit->addAction(cancelNew);
	connect(cancelNew, &QAction::triggered, this, [this]() { cancelNewScene(); });

	signal_handler_connect(obs_get_signal_handler(), "source_rename", source_renamed, this);

	rebuildTabs();
}

SceneGroupsDock::~SceneGroupsDock()
{
	/* OBS state is released in shutdown(), triggered by the EXIT event. */
}

void SceneGroupsDock::shutdown()
{
	if (closed)
		return;
	closed = true;
	saveTimer->stop();
	updates->stop();
	signal_handler_disconnect(obs_get_signal_handler(), "source_rename", source_renamed, this);
	clearPending();
}

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */

SceneGroupsDock::SceneList SceneGroupsDock::listScenes()
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

bool SceneGroupsDock::sourceAlive(const QString &uuid)
{
	obs_source_t *source = obs_get_source_by_uuid(uuid.toUtf8().constData());
	if (!source)
		return false;
	const bool alive = !obs_source_removed(source);
	obs_source_release(source);
	return alive;
}

bool SceneGroupsDock::nameExists(const QString &name)
{
	obs_source_t *source = obs_get_source_by_name(name.toUtf8().constData());
	if (!source)
		return false;
	obs_source_release(source);
	return true;
}

SceneGroupsDock::Group *SceneGroupsDock::findGroup(const QString &id)
{
	for (Group &group : groups) {
		if (group.id == id)
			return &group;
	}
	return nullptr;
}

SceneGroupsDock::Group *SceneGroupsDock::currentGroup()
{
	return activeGroupId.isEmpty() ? nullptr : findGroup(activeGroupId);
}

QStringList SceneGroupsDock::members(const Group &group)
{
	QStringList result;
	for (const Node &node : group.items) {
		if (node.folder)
			result.append(node.children);
		else
			result.append(node.id);
	}
	return result;
}

bool SceneGroupsDock::hasScene(const Group &group, const QString &uuid)
{
	for (const Node &node : group.items) {
		if (node.folder ? node.children.contains(uuid) : node.id == uuid)
			return true;
	}
	return false;
}

void SceneGroupsDock::removeScene(Group &group, const QString &uuid)
{
	for (qsizetype i = group.items.size() - 1; i >= 0; i--) {
		Node &node = group.items[i];
		if (node.folder)
			node.children.removeAll(uuid);
		else if (node.id == uuid)
			group.items.removeAt(i);
	}
}

void SceneGroupsDock::addScene(Group &group, const QString &uuid, const QString &folderId)
{
	if (hasScene(group, uuid))
		return;
	if (Node *folder = findFolder(group, folderId)) {
		folder->children.append(uuid);
		return;
	}
	Node node;
	node.id = uuid;
	group.items.append(node);
}

SceneGroupsDock::Node *SceneGroupsDock::findFolder(Group &group, const QString &folderId)
{
	if (folderId.isEmpty())
		return nullptr;
	for (Node &node : group.items) {
		if (node.folder && node.id == folderId)
			return &node;
	}
	return nullptr;
}

int SceneGroupsDock::sharedCount(const QString &uuid) const
{
	int count = 0;
	for (const Group &group : groups) {
		if (hasScene(group, uuid))
			count++;
	}
	return count;
}

QStringList SceneGroupsDock::selectedUuids() const
{
	QStringList result;
	const QList<QTreeWidgetItem *> selection = list->selectedItems();
	for (const QTreeWidgetItem *item : selection) {
		if (!item->data(0, RoleFolder).toBool())
			result.append(item->data(0, RoleUuid).toString());
	}
	return result;
}

void SceneGroupsDock::markDirty()
{
	if (closed || cleaning)
		return;
	saveTimer->start();
}

/* ------------------------------------------------------------------------- */
/* Persistence: groups live inside the scene collection file                 */

void SceneGroupsDock::saveTo(obs_data_t *save_data)
{
	if (closed || cleaning)
		return;

	/* Layout loaded but not applied yet: leave what is stored untouched. */
	if (hasPending) {
		if (pending)
			obs_data_set_obj(save_data, SAVE_KEY, pending);
		return;
	}

	obs_data_t *root = obs_data_create();
	obs_data_set_int(root, "version", SAVE_VERSION);
	obs_data_set_string(root, "active", activeGroupId.toUtf8().constData());

	obs_data_array_t *groupArray = obs_data_array_create();
	for (const Group &group : groups) {
		obs_data_t *entry = obs_data_create();
		obs_data_set_string(entry, "id", group.id.toUtf8().constData());
		obs_data_set_string(entry, "name", group.name.toUtf8().constData());
		obs_data_set_string(entry, "color", group.color.toUtf8().constData());
		obs_data_set_string(entry, "profile", group.profile.toUtf8().constData());

		/* The name lets an imported collection (new UUIDs) find its scenes again. */
		const auto sceneEntry = [](const QString &uuid) {
			obs_data_t *scene = obs_data_create();
			obs_data_set_string(scene, "type", "scene");
			obs_data_set_string(scene, "uuid", uuid.toUtf8().constData());
			if (obs_source_t *source = obs_get_source_by_uuid(uuid.toUtf8().constData())) {
				obs_data_set_string(scene, "name", obs_source_get_name(source));
				obs_source_release(source);
			}
			return scene;
		};
		obs_data_array_t *itemArray = obs_data_array_create();
		for (const Node &node : group.items) {
			obs_data_t *item = nullptr;
			if (node.folder) {
				item = obs_data_create();
				obs_data_set_string(item, "type", "folder");
				obs_data_set_string(item, "id", node.id.toUtf8().constData());
				obs_data_set_string(item, "name", node.name.toUtf8().constData());
				obs_data_set_string(item, "color", node.color.toUtf8().constData());
				obs_data_set_bool(item, "expanded", node.expanded);
				obs_data_array_t *children = obs_data_array_create();
				for (const QString &uuid : node.children) {
					obs_data_t *child = sceneEntry(uuid);
					obs_data_array_push_back(children, child);
					obs_data_release(child);
				}
				obs_data_set_array(item, "children", children);
				obs_data_array_release(children);
			} else {
				item = sceneEntry(node.id);
			}
			obs_data_array_push_back(itemArray, item);
			obs_data_release(item);
		}
		obs_data_set_array(entry, "items", itemArray);
		obs_data_array_release(itemArray);

		obs_data_array_push_back(groupArray, entry);
		obs_data_release(entry);
	}
	obs_data_set_array(root, "groups", groupArray);
	obs_data_array_release(groupArray);

	obs_data_set_obj(save_data, SAVE_KEY, root);
	obs_data_release(root);
}

void SceneGroupsDock::clearPending()
{
	if (pending) {
		obs_data_release(pending);
		pending = nullptr;
	}
	hasPending = false;
}

/* Turns the old folder tree into groups: each top-level folder becomes one group. */
static void collectLegacyScenes(obs_data_array_t *items, obs_data_array_t *out)
{
	const size_t count = obs_data_array_count(items);
	for (size_t i = 0; i < count; i++) {
		obs_data_t *entry = obs_data_array_item(items, i);
		const QString type = QString::fromUtf8(obs_data_get_string(entry, "type"));
		if (type == QLatin1String("scene")) {
			obs_data_array_push_back(out, entry);
		} else if (obs_data_array_t *children = obs_data_get_array(entry, "children")) {
			collectLegacyScenes(children, out);
			obs_data_array_release(children);
		}
		obs_data_release(entry);
	}
}

static obs_data_t *migrateLegacy(obs_data_t *legacy)
{
	obs_data_t *root = obs_data_create();
	obs_data_set_int(root, "version", SAVE_VERSION);
	obs_data_array_t *groupArray = obs_data_array_create();

	if (obs_data_array_t *items = obs_data_get_array(legacy, "items")) {
		const size_t count = obs_data_array_count(items);
		for (size_t i = 0; i < count; i++) {
			obs_data_t *entry = obs_data_array_item(items, i);
			if (QString::fromUtf8(obs_data_get_string(entry, "type")) == QLatin1String("folder")) {
				obs_data_t *group = obs_data_create();
				obs_data_set_string(
					group, "id",
					QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8().constData());
				obs_data_set_string(group, "name", obs_data_get_string(entry, "name"));
				obs_data_set_string(group, "color", obs_data_get_string(entry, "color"));
				obs_data_array_t *sceneArray = obs_data_array_create();
				if (obs_data_array_t *children = obs_data_get_array(entry, "children")) {
					collectLegacyScenes(children, sceneArray);
					obs_data_array_release(children);
				}
				obs_data_set_array(group, "scenes", sceneArray);
				obs_data_array_release(sceneArray);
				obs_data_array_push_back(groupArray, group);
				obs_data_release(group);
			}
			obs_data_release(entry);
		}
		obs_data_array_release(items);
	}

	obs_data_set_array(root, "groups", groupArray);
	obs_data_array_release(groupArray);
	return root;
}

void SceneGroupsDock::loadFrom(obs_data_t *save_data)
{
	if (closed)
		return;

	clearPending();
	pending = obs_data_get_obj(save_data, SAVE_KEY); /* may be null: first run */
	if (!pending) {
		if (obs_data_t *legacy = obs_data_get_obj(save_data, LEGACY_KEY)) {
			pending = migrateLegacy(legacy);
			obs_data_release(legacy);
		}
	}
	hasPending = true;
	cleaning = false;

	groups.clear();
	activeGroupId.clear();
	reconcile();
}

bool SceneGroupsDock::tryApplyPending()
{
	if (!hasPending)
		return true;

	const SceneList scenes = listScenes();
	if (scenes.isEmpty())
		return false; /* scenes not loaded yet, try again on the next event */

	QHash<QString, QString> byName;
	QHash<QString, bool> known;
	for (const auto &scene : scenes) {
		known.insert(scene.first, true);
		byName.insert(scene.second, scene.first);
	}

	groups.clear();
	activeGroupId.clear();

	if (pending) {
		if (obs_data_array_t *groupArray = obs_data_get_array(pending, "groups")) {
			const size_t count = obs_data_array_count(groupArray);
			for (size_t i = 0; i < count; i++) {
				obs_data_t *entry = obs_data_array_item(groupArray, i);
				Group group;
				group.id = QString::fromUtf8(obs_data_get_string(entry, "id"));
				if (group.id.isEmpty())
					group.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
				group.name = QString::fromUtf8(obs_data_get_string(entry, "name"));
				group.color = QString::fromUtf8(obs_data_get_string(entry, "color"));
				group.profile = QString::fromUtf8(obs_data_get_string(entry, "profile"));

				const auto resolve = [&](obs_data_t *scene) {
					QString uuid = QString::fromUtf8(obs_data_get_string(scene, "uuid"));
					if (!known.contains(uuid))
						uuid = byName.value(
							QString::fromUtf8(obs_data_get_string(scene, "name")));
					return uuid;
				};
				if (obs_data_array_t *itemArray = obs_data_get_array(entry, "items")) {
					const size_t itemCount = obs_data_array_count(itemArray);
					for (size_t j = 0; j < itemCount; j++) {
						obs_data_t *item = obs_data_array_item(itemArray, j);
						if (QString::fromUtf8(obs_data_get_string(item, "type")) ==
						    QLatin1String("folder")) {
							obs_data_set_default_bool(item, "expanded", true);
							Node folder;
							folder.folder = true;
							folder.id = QString::fromUtf8(obs_data_get_string(item, "id"));
							if (folder.id.isEmpty())
								folder.id = QUuid::createUuid().toString(
									QUuid::WithoutBraces);
							folder.name =
								QString::fromUtf8(obs_data_get_string(item, "name"));
							folder.color =
								QString::fromUtf8(obs_data_get_string(item, "color"));
							folder.expanded = obs_data_get_bool(item, "expanded");
							group.items.append(folder);
							if (obs_data_array_t *children =
								    obs_data_get_array(item, "children")) {
								const size_t childCount =
									obs_data_array_count(children);
								for (size_t k = 0; k < childCount; k++) {
									obs_data_t *child =
										obs_data_array_item(children, k);
									const QString uuid = resolve(child);
									if (!uuid.isEmpty())
										addScene(group, uuid, folder.id);
									obs_data_release(child);
								}
								obs_data_array_release(children);
							}
						} else {
							const QString uuid = resolve(item);
							if (!uuid.isEmpty())
								addScene(group, uuid);
						}
						obs_data_release(item);
					}
					obs_data_array_release(itemArray);
				} else if (obs_data_array_t *sceneArray = obs_data_get_array(entry, "scenes")) {
					/* v0.3.0 layout: a flat list of scenes. */
					const size_t sceneCount = obs_data_array_count(sceneArray);
					for (size_t j = 0; j < sceneCount; j++) {
						obs_data_t *scene = obs_data_array_item(sceneArray, j);
						const QString uuid = resolve(scene);
						if (!uuid.isEmpty())
							addScene(group, uuid);
						obs_data_release(scene);
					}
					obs_data_array_release(sceneArray);
				}

				groups.append(group);
				obs_data_release(entry);
			}
			obs_data_array_release(groupArray);
		}

		const QString active = QString::fromUtf8(obs_data_get_string(pending, "active"));
		if (findGroup(active))
			activeGroupId = active;
	}

	clearPending();
	rebuildTabs();
	return true;
}

/* ------------------------------------------------------------------------- */
/* Sync with OBS                                                             */

void SceneGroupsDock::reconcile()
{
	if (closed || cleaning)
		return;
	if (!tryApplyPending())
		return;

	/* Drop scenes that no longer exist. Checked against libobs, not the UI list,
	 * so a scene we just created isn't lost before the frontend lists it. */
	for (Group &group : groups) {
		for (const QString &uuid : members(group)) {
			if (!sourceAlive(uuid))
				removeScene(group, uuid);
		}
	}

	rebuildList();
}

void SceneGroupsDock::handleFrontendEvent(enum obs_frontend_event event)
{
	if (closed)
		return;

	switch (event) {
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		/* The old collection was already saved; drop its groups. */
		cleaning = true;
		saveTimer->stop();
		clearPending();
		groups.clear();
		activeGroupId.clear();
		updating = true;
		list->clear();
		updating = false;
		break;
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		cleaning = false;
		reconcile();
		showDockFirstTime();
		QTimer::singleShot(8000, this, [this]() {
			if (!closed)
				updates->start();
		});
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		cleaning = false;
		rebuildTabs();
		reconcile();
		break;
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
		reconcile();
		break;
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
		updateIndicators();
		break;
	case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
		profileNote.clear();
		updateSubheader(list->count());
		break;
	default:
		break;
	}
}

/* A new plugin's dock starts hidden; open it once so the user doesn't have to hunt for it. */
void SceneGroupsDock::showDockFirstTime()
{
	char *marker = obs_module_config_path("dock-shown");
	if (!marker)
		return;
	if (!os_file_exists(marker)) {
		if (auto *dockWidget = qobject_cast<QDockWidget *>(parentWidget()))
			dockWidget->setVisible(true);
		char *dir = obs_module_config_path("");
		if (dir) {
			os_mkdirs(dir);
			bfree(dir);
		}
		QFile file(QString::fromUtf8(marker));
		if (file.open(QIODevice::WriteOnly))
			file.close();
	}
	bfree(marker);
}

void SceneGroupsDock::showUpdate(const QString &version, const QString &url)
{
	updateVersion = version;
	updateUrl = url;
	updateLabel->setText(T("Update.Available").arg(version, QString::fromUtf8(PLUGIN_VERSION)));
	updateBanner->setVisible(true);
}

/* ------------------------------------------------------------------------- */
/* View                                                                      */

void SceneGroupsDock::rebuildTabs()
{
	{
		QSignalBlocker blocker(tabs);
		while (tabs->count() > 0)
			tabs->removeTab(0);

		int activeIndex = -1;
		for (const Group &group : groups) {
			const int index = tabs->addTab(dotIcon(group.color), group.name);
			tabs->setTabData(index, group.id);
			if (group.id == activeGroupId)
				activeIndex = index;
		}
		const int allIndex = tabs->addTab(T("Group.All"));
		tabs->setTabData(allIndex, QString());
		if (activeIndex < 0) {
			activeGroupId.clear();
			activeIndex = allIndex;
		}
		tabs->setCurrentIndex(activeIndex);
	}
	firstRun->setVisible(groups.isEmpty());
}

void SceneGroupsDock::updateSubheader(int count)
{
	QString text = count == 1 ? T("Sub.CountOne") : T("Sub.CountMany").arg(count);
	const Group *group = currentGroup();
	if (group && !group->profile.isEmpty())
		text += QStringLiteral(" · ") + T("Sub.Profile").arg(group->profile);
	if (!profileNote.isEmpty())
		text += QStringLiteral(" · ") + profileNote;
	subLabel->setText(text);
	gearButton->setVisible(group != nullptr);
}

void SceneGroupsDock::rebuildList()
{
	if (closed || cleaning)
		return;

	const SceneList scenes = listScenes();
	QHash<QString, QString> names;
	for (const auto &scene : scenes)
		names.insert(scene.first, scene.second);

	const QString filter = searchEdit->text().trimmed();
	const QStringList keepSelected = selectedUuids();
	const Group *group = currentGroup();
	const bool reorderable = group && filter.isEmpty();
	const auto matches = [&](const QString &uuid) {
		return names.contains(uuid) &&
		       (filter.isEmpty() || names.value(uuid).contains(filter, Qt::CaseInsensitive));
	};
	const auto makeScene = [&](const QString &uuid) {
		auto *item = new QTreeWidgetItem();
		item->setText(0, names.value(uuid));
		item->setData(0, RoleUuid, uuid);
		item->setData(0, RoleShared, sharedCount(uuid));
		Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable |
				      Qt::ItemNeverHasChildren;
		if (reorderable)
			flags |= Qt::ItemIsDragEnabled;
		item->setFlags(flags);
		return item;
	};

	updating = true;
	list->clear();
	QList<QPair<QTreeWidgetItem *, bool>> folderStates;
	int sceneCount = 0;
	if (group) {
		for (const Node &node : group->items) {
			if (!node.folder) {
				if (matches(node.id)) {
					list->addTopLevelItem(makeScene(node.id));
					sceneCount++;
				}
				continue;
			}
			int visible = 0;
			auto *folder = new QTreeWidgetItem();
			folder->setText(0, node.name);
			folder->setData(0, RoleUuid, node.id);
			folder->setData(0, RoleFolder, true);
			folder->setData(0, RoleFolderColor, node.color);
			Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
			if (reorderable)
				flags |= Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
			folder->setFlags(flags);
			for (const QString &uuid : node.children) {
				if (matches(uuid)) {
					folder->addChild(makeScene(uuid));
					visible++;
				}
			}
			folder->setData(0, RoleCount, filter.isEmpty() ? (int)node.children.size() : visible);
			sceneCount += visible;
			/* While searching, folders without matches are hidden and the rest open. */
			if (filter.isEmpty() || visible > 0) {
				list->addTopLevelItem(folder);
				folderStates.append(qMakePair(folder, filter.isEmpty() ? node.expanded : true));
			} else {
				delete folder;
			}
		}
	} else {
		for (const auto &scene : scenes) {
			if (matches(scene.first)) {
				list->addTopLevelItem(makeScene(scene.first));
				sceneCount++;
			}
		}
	}
	for (const auto &state : folderStates)
		state.first->setExpanded(state.second);
	for (QTreeWidgetItemIterator it(list); *it; ++it) {
		if (keepSelected.size() > 1 && keepSelected.contains((*it)->data(0, RoleUuid).toString()))
			(*it)->setSelected(true);
	}
	/* Reordering only makes sense inside a group; "All" follows OBS's own order. */
	list->setDragDropMode(reorderable ? QAbstractItemView::InternalMove : QAbstractItemView::NoDragDrop);
	list->setRootIsDecorated(group != nullptr);
	newFolderButton->setVisible(group != nullptr);
	updating = false;

	updateSubheader(sceneCount);
	updateIndicators();
}

void SceneGroupsDock::updateIndicators()
{
	if (closed || cleaning)
		return;

	QString programUuid;
	if (obs_source_t *program = obs_frontend_get_current_scene()) {
		programUuid = QString::fromUtf8(obs_source_get_uuid(program));
		obs_source_release(program);
	}
	const bool studio = obs_frontend_preview_program_mode_active();
	QString previewUuid;
	if (studio) {
		if (obs_source_t *preview = obs_frontend_get_current_preview_scene()) {
			previewUuid = QString::fromUtf8(obs_source_get_uuid(preview));
			obs_source_release(preview);
		}
	}
	const QString focusUuid = studio && !previewUuid.isEmpty() ? previewUuid : programUuid;

	updating = true;
	QTreeWidgetItem *focus = nullptr;
	for (QTreeWidgetItemIterator it(list); *it; ++it) {
		QTreeWidgetItem *item = *it;
		if (item->data(0, RoleFolder).toBool()) {
			bool containsLive = false;
			for (int i = 0; i < item->childCount(); i++) {
				if (item->child(i)->data(0, RoleUuid).toString() == programUuid)
					containsLive = true;
			}
			item->setData(0, RoleLive, containsLive);
			continue;
		}
		const QString uuid = item->data(0, RoleUuid).toString();
		const bool live = uuid == programUuid;
		item->setData(0, RoleLive, live);
		item->setData(0, RolePreview, studio && !live && uuid == previewUuid);
		if (uuid == focusUuid)
			focus = item;
	}
	/* Keep a multi-selection the user is building. */
	if (list->selectedItems().size() <= 1) {
		QSignalBlocker blocker(list);
		list->setCurrentItem(focus);
	}
	updating = false;

	studioHint->setVisible(studio);
	list->viewport()->update();
}

/* ------------------------------------------------------------------------- */
/* Interaction                                                               */

void SceneGroupsDock::onTabChanged(int index)
{
	if (index < 0)
		return;
	activeGroupId = tabs->tabData(index).toString();
	profileNote.clear();
	cancelNewScene();
	if (const Group *group = currentGroup())
		applyLinkedProfile(*group);
	rebuildList();
	markDirty();
}

void SceneGroupsDock::applyLinkedProfile(const Group &group)
{
	if (group.profile.isEmpty())
		return;

	bool exists = false;
	if (char **profiles = obs_frontend_get_profiles()) {
		for (char **profile = profiles; *profile; profile++) {
			if (group.profile == QString::fromUtf8(*profile))
				exists = true;
		}
		bfree(profiles);
	}
	if (!exists) {
		profileNote = T("Sub.ProfileMissing");
		return;
	}

	char *current = obs_frontend_get_current_profile();
	const bool same = current && group.profile == QString::fromUtf8(current);
	bfree(current);
	if (same)
		return;

	/* OBS can't swap output settings while they are in use. */
	if (obs_frontend_streaming_active() || obs_frontend_recording_active()) {
		profileNote = T("Sub.ProfileBlocked");
		return;
	}
	obs_frontend_set_current_profile(group.profile.toUtf8().constData());
}

void SceneGroupsDock::activateScene(const QString &uuid, bool forceProgram)
{
	if (closed)
		return;
	obs_source_t *source = obs_get_source_by_uuid(uuid.toUtf8().constData());
	if (!source)
		return;
	if (obs_frontend_preview_program_mode_active() && !forceProgram)
		obs_frontend_set_current_preview_scene(source);
	else
		obs_frontend_set_current_scene(source);
	obs_source_release(source);
}

void SceneGroupsDock::onItemClicked(QTreeWidgetItem *item)
{
	/* Ctrl/Cmd/Shift + click builds a selection; it must not switch scenes. */
	if (QApplication::keyboardModifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::MetaModifier))
		return;
	if (item->data(0, RoleFolder).toBool()) {
		item->setExpanded(!item->isExpanded()); /* the whole row opens and closes the folder */
		return;
	}
	activateScene(item->data(0, RoleUuid).toString(), false);
}

void SceneGroupsDock::onItemDoubleClicked(QTreeWidgetItem *item)
{
	/* In Studio Mode a double click sends the scene to program. */
	if (!item->data(0, RoleFolder).toBool() && obs_frontend_preview_program_mode_active())
		activateScene(item->data(0, RoleUuid).toString(), true);
}

void SceneGroupsDock::onFolderToggled(QTreeWidgetItem *item, bool expanded)
{
	if (updating || !item->data(0, RoleFolder).toBool() || !searchEdit->text().trimmed().isEmpty())
		return;
	Group *group = currentGroup();
	if (!group)
		return;
	if (Node *folder = findFolder(*group, item->data(0, RoleUuid).toString())) {
		folder->expanded = expanded;
		markDirty();
	}
}

/* Inline rename (F2 or the context menu). */
void SceneGroupsDock::onItemChanged(QTreeWidgetItem *item)
{
	if (updating || closed)
		return;

	if (item->data(0, RoleFolder).toBool()) {
		Group *group = currentGroup();
		Node *folder = group ? findFolder(*group, item->data(0, RoleUuid).toString()) : nullptr;
		const QString name = item->text(0).trimmed();
		if (folder && !name.isEmpty()) {
			folder->name = name;
			markDirty();
		} else if (folder) {
			updating = true;
			item->setText(0, folder->name);
			updating = false;
		}
		return;
	}

	const QString uuid = item->data(0, RoleUuid).toString();
	obs_source_t *source = obs_get_source_by_uuid(uuid.toUtf8().constData());
	if (!source)
		return;

	const QString oldName = QString::fromUtf8(obs_source_get_name(source));
	const QString newName = item->text(0).trimmed();
	if (newName.isEmpty() || newName == oldName || nameExists(newName)) {
		updating = true;
		item->setText(0, oldName);
		updating = false;
		if (!newName.isEmpty() && newName != oldName)
			QMessageBox::information(this, T("Scene.Rename"), T("Scene.NameTaken"));
	} else {
		obs_source_set_name(source, newName.toUtf8().constData());
	}
	obs_source_release(source);
}

void SceneGroupsDock::onOrderChanged()
{
	Group *group = currentGroup();
	if (!group || !searchEdit->text().trimmed().isEmpty())
		return;

	/* Rebuild the group's layout from what the tree shows now. */
	QHash<QString, Node> folders;
	for (const Node &node : group->items) {
		if (node.folder)
			folders.insert(node.id, node);
	}
	QList<Node> items;
	for (int i = 0; i < list->topLevelItemCount(); i++) {
		QTreeWidgetItem *top = list->topLevelItem(i);
		const QString id = top->data(0, RoleUuid).toString();
		if (top->data(0, RoleFolder).toBool()) {
			Node folder = folders.value(id);
			folder.folder = true;
			folder.id = id;
			folder.children.clear();
			for (int j = 0; j < top->childCount(); j++)
				folder.children.append(top->child(j)->data(0, RoleUuid).toString());
			items.append(folder);
		} else {
			Node scene;
			scene.id = id;
			items.append(scene);
		}
	}
	group->items = items;
	/* Qt forgets the open state of moved folders: rebuild from the model. */
	QTimer::singleShot(0, this, [this]() { rebuildList(); });
	markDirty();
}

void SceneGroupsDock::onContextMenu(const QPoint &pos)
{
	if (closed || cleaning)
		return;

	QTreeWidgetItem *item = list->itemAt(pos);
	QMenu menu(this);
	Group *group = currentGroup();

	if (item && item->data(0, RoleFolder).toBool()) {
		const QString folderId = item->data(0, RoleUuid).toString();
		QAction *rename = menu.addAction(T("Folder.Rename"));
		connect(rename, &QAction::triggered, this, [this, item]() { list->editItem(item, 0); });
		QAction *sceneHere = menu.addAction(T("Folder.NewSceneHere"));
		connect(sceneHere, &QAction::triggered, this, [this, folderId]() { beginNewScene(folderId); });
		QMenu *colors = menu.addMenu(T("Folder.Color"));
		QAction *none = colors->addAction(T("Color.None"));
		connect(none, &QAction::triggered, this, [this, folderId]() { setFolderColor(folderId, QString()); });
		for (int i = 0; i < COLOR_PRESET_COUNT; i++) {
			const QString hex = QString::fromLatin1(COLOR_PRESETS[i].hex);
			QAction *action = colors->addAction(dotIcon(hex), T(COLOR_PRESETS[i].localeKey));
			connect(action, &QAction::triggered, this,
				[this, folderId, hex]() { setFolderColor(folderId, hex); });
		}
		menu.addSeparator();
		QAction *removeFolder = menu.addAction(T("Folder.Delete"));
		connect(removeFolder, &QAction::triggered, this, [this, folderId]() { deleteFolder(folderId); });
	} else if (item) {
		QStringList uuids = selectedUuids();
		const QString clicked = item->data(0, RoleUuid).toString();
		if (!uuids.contains(clicked))
			uuids = QStringList(clicked);
		const bool single = uuids.size() == 1;

		if (single) {
			QAction *rename = menu.addAction(T("Scene.Rename"));
			connect(rename, &QAction::triggered, this, [this, item]() { list->editItem(item, 0); });
			QAction *duplicate = menu.addAction(T("Scene.Duplicate"));
			connect(duplicate, &QAction::triggered, this, [this, clicked]() { duplicateScene(clicked); });
		}

		if (group) {
			QMenu *move = menu.addMenu(T("Folder.MoveTo"));
			for (const Node &node : group->items) {
				if (!node.folder)
					continue;
				QAction *action = move->addAction(
					folderIcon(node.color, palette().color(QPalette::Mid)), node.name);
				const QString folderId = node.id;
				connect(action, &QAction::triggered, this,
					[this, uuids, folderId]() { moveToFolder(uuids, folderId); });
			}
			move->addSeparator();
			QAction *root = move->addAction(T("Folder.None"));
			connect(root, &QAction::triggered, this, [this, uuids]() { moveToFolder(uuids, QString()); });
			QAction *fresh = move->addAction(T("Folder.NewEllipsis"));
			connect(fresh, &QAction::triggered, this, [this, uuids]() { newFolder(uuids); });
		}

		menu.addSection(T("Menu.ShowIn"));
		for (const Group &other : groups) {
			bool inAll = true;
			for (const QString &uuid : uuids) {
				if (!hasScene(other, uuid))
					inAll = false;
			}
			QAction *action = menu.addAction(dotIcon(other.color), other.name);
			action->setCheckable(true);
			action->setChecked(inAll);
			const QString groupId = other.id;
			connect(action, &QAction::triggered, this,
				[this, uuids, groupId](bool checked) { setMembership(uuids, groupId, checked); });
		}
		QAction *toNew = menu.addAction(T("Group.NewEllipsis"));
		connect(toNew, &QAction::triggered, this, [this, uuids]() { promptNewGroup(uuids); });

		menu.addSeparator();
		QAction *removeAction =
			menu.addAction(single ? T("Scene.Delete") : T("Scene.DeleteMany").arg(uuids.size()));
		connect(removeAction, &QAction::triggered, this, [this, uuids]() { deleteScenes(uuids); });
	} else {
		QAction *create = menu.addAction(T("Scene.New"));
		connect(create, &QAction::triggered, this, [this]() { beginNewScene(); });
		if (group) {
			QAction *folder = menu.addAction(T("Folder.NewEllipsis"));
			connect(folder, &QAction::triggered, this, [this]() { newFolder(); });
		}
		QAction *group = menu.addAction(T("Group.NewEllipsis"));
		connect(group, &QAction::triggered, this, [this]() { promptNewGroup(QStringList()); });
		menu.addSeparator();
		QAction *check = menu.addAction(T("Update.Toggle"));
		check->setCheckable(true);
		check->setChecked(updates->isEnabled());
		connect(check, &QAction::toggled, this, [this](bool on) {
			updates->setEnabled(on);
			if (!on)
				updateBanner->setVisible(false);
		});
	}

	menu.exec(list->viewport()->mapToGlobal(pos));
}

/* ------------------------------------------------------------------------- */
/* Groups                                                                    */

SceneGroupsDock::Group *SceneGroupsDock::addGroup(const QString &name, const QString &color)
{
	Group group;
	group.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	group.name = name;
	group.color = color;
	groups.append(group);
	activeGroupId = group.id;
	profileNote.clear();
	rebuildTabs();
	rebuildList();
	markDirty();
	return &groups.last();
}

void SceneGroupsDock::promptNewGroup(const QStringList &scenesToAdd)
{
	bool ok = false;
	const QString name =
		QInputDialog::getText(this, T("Group.New"), T("Group.NamePrompt"), QLineEdit::Normal, QString(), &ok)
			.trimmed();
	if (!ok || name.isEmpty())
		return;

	const QString color = QString::fromLatin1(COLOR_PRESETS[groups.size() % COLOR_PRESET_COUNT].hex);
	Group *group = addGroup(name, color);
	if (!scenesToAdd.isEmpty()) {
		for (const QString &uuid : scenesToAdd)
			addScene(*group, uuid);
		rebuildList();
	}
}

void SceneGroupsDock::setMembership(const QStringList &uuids, const QString &groupId, bool member)
{
	Group *group = findGroup(groupId);
	if (!group)
		return;
	for (const QString &uuid : uuids) {
		if (member)
			addScene(*group, uuid);
		else
			removeScene(*group, uuid);
	}
	rebuildList();
	markDirty();
}

void SceneGroupsDock::editCurrentGroup()
{
	Group *group = currentGroup();
	if (!group)
		return;

	QDialog dialog(this);
	dialog.setWindowTitle(T("Group.Settings"));

	auto *nameEdit = new QLineEdit(group->name, &dialog);

	auto *colorRow = new QHBoxLayout();
	colorRow->setSpacing(6);
	auto *colorButtons = new QButtonGroup(&dialog);
	for (int i = 0; i < COLOR_PRESET_COUNT; i++) {
		const QString hex = QString::fromLatin1(COLOR_PRESETS[i].hex);
		auto *swatch = new QToolButton(&dialog);
		swatch->setCheckable(true);
		swatch->setFixedSize(26, 26);
		swatch->setToolTip(T(COLOR_PRESETS[i].localeKey));
		swatch->setAccessibleName(T(COLOR_PRESETS[i].localeKey));
		swatch->setStyleSheet(
			QStringLiteral(
				"QToolButton { background: %1; border: 2px solid transparent; border-radius: 13px; } QToolButton:checked { border: 2px solid palette(text); }")
				.arg(hex));
		swatch->setChecked(hex.compare(group->color, Qt::CaseInsensitive) == 0);
		colorButtons->addButton(swatch, i);
		colorRow->addWidget(swatch);
	}
	colorRow->addStretch(1);

	auto *profileCombo = new QComboBox(&dialog);
	profileCombo->addItem(T("Group.NoProfile"), QString());
	if (char **profiles = obs_frontend_get_profiles()) {
		for (char **profile = profiles; *profile; profile++) {
			const QString profileName = QString::fromUtf8(*profile);
			profileCombo->addItem(profileName, profileName);
		}
		bfree(profiles);
	}
	const int profileIndex = profileCombo->findData(group->profile);
	profileCombo->setCurrentIndex(profileIndex < 0 ? 0 : profileIndex);

	auto *profileHelp = new QLabel(T("Group.ProfileHelp"), &dialog);
	profileHelp->setWordWrap(true);
	profileHelp->setEnabled(false);

	auto *form = new QFormLayout();
	form->addRow(T("Group.Name"), nameEdit);
	form->addRow(T("Group.Color"), colorRow);
	form->addRow(T("Group.Profile"), profileCombo);
	form->addRow(QString(), profileHelp);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
	QPushButton *deleteButton = buttons->addButton(T("Group.Delete"), QDialogButtonBox::DestructiveRole);
	deleteButton->setToolTip(T("Group.DeleteHelp"));
	bool deleteRequested = false;
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	connect(deleteButton, &QPushButton::clicked, &dialog, [&dialog, &deleteRequested]() {
		deleteRequested = true;
		dialog.accept();
	});

	auto *dialogLayout = new QVBoxLayout(&dialog);
	dialogLayout->addLayout(form);
	dialogLayout->addWidget(buttons);

	if (dialog.exec() != QDialog::Accepted)
		return;

	/* The dialog ran its own event loop: look the group up again. */
	group = currentGroup();
	if (!group)
		return;

	if (deleteRequested) {
		const QString question = T("Group.DeleteConfirm").arg(group->name);
		if (QMessageBox::question(this, T("Group.Delete"), question) != QMessageBox::Yes)
			return;
		const QString id = group->id;
		for (qsizetype i = 0; i < groups.size(); i++) {
			if (groups.at(i).id == id) {
				groups.removeAt(i);
				break;
			}
		}
		activeGroupId.clear();
	} else {
		const QString name = nameEdit->text().trimmed();
		if (!name.isEmpty())
			group->name = name;
		const int checked = colorButtons->checkedId();
		if (checked >= 0)
			group->color = QString::fromLatin1(COLOR_PRESETS[checked].hex);
		group->profile = profileCombo->currentData().toString();
	}

	profileNote.clear();
	rebuildTabs();
	rebuildList();
	markDirty();
}

/* ------------------------------------------------------------------------- */
/* Folders (one level, inside the active group)                              */

void SceneGroupsDock::newFolder(const QStringList &scenesToMove)
{
	Group *group = currentGroup();
	if (!group)
		return;
	bool ok = false;
	const QString name =
		QInputDialog::getText(this, T("Folder.New"), T("Folder.NamePrompt"), QLineEdit::Normal, QString(), &ok)
			.trimmed();
	group = currentGroup(); /* the dialog ran its own event loop */
	if (!ok || name.isEmpty() || !group)
		return;

	Node folder;
	folder.folder = true;
	folder.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	folder.name = name;
	/* New folders go after the existing ones, before loose scenes. */
	qsizetype index = 0;
	for (qsizetype i = 0; i < group->items.size(); i++) {
		if (group->items.at(i).folder)
			index = i + 1;
	}
	group->items.insert(index, folder);
	if (!scenesToMove.isEmpty()) {
		moveToFolder(scenesToMove, folder.id);
		return;
	}
	rebuildList();
	markDirty();
}

void SceneGroupsDock::setFolderColor(const QString &folderId, const QString &color)
{
	Group *group = currentGroup();
	Node *folder = group ? findFolder(*group, folderId) : nullptr;
	if (!folder)
		return;
	folder->color = color;
	rebuildList();
	markDirty();
}

void SceneGroupsDock::deleteFolder(const QString &folderId)
{
	Group *group = currentGroup();
	if (!group)
		return;
	for (qsizetype i = 0; i < group->items.size(); i++) {
		if (group->items.at(i).folder && group->items.at(i).id == folderId) {
			/* Only the folder goes: its scenes stay in the group, where it was. */
			const QStringList children = group->items.at(i).children;
			group->items.removeAt(i);
			for (qsizetype j = 0; j < children.size(); j++) {
				Node scene;
				scene.id = children.at(j);
				group->items.insert(i + j, scene);
			}
			break;
		}
	}
	rebuildList();
	markDirty();
}

void SceneGroupsDock::moveToFolder(const QStringList &uuids, const QString &folderId)
{
	Group *group = currentGroup();
	if (!group)
		return;
	for (const QString &uuid : uuids) {
		removeScene(*group, uuid);
		addScene(*group, uuid, folderId);
	}
	if (Node *folder = findFolder(*group, folderId))
		folder->expanded = true;
	rebuildList();
	markDirty();
}

/* ------------------------------------------------------------------------- */
/* Scenes                                                                    */

void SceneGroupsDock::beginNewScene(const QString &folderId)
{
	Group *group = currentGroup();
	newSceneFolderId = folderId;
	const Node *folder = group ? findFolder(*group, folderId) : nullptr;
	if (folder)
		newSceneHint->setText(T("Scene.NewHintGroup").arg(group->name + QStringLiteral(" / ") + folder->name));
	else
		newSceneHint->setText(group ? T("Scene.NewHintGroup").arg(group->name) : T("Scene.NewHint"));
	newSceneEdit->clear();
	newSceneEdit->setVisible(true);
	newSceneHint->setVisible(true);
	newSceneEdit->setFocus();
}

void SceneGroupsDock::cancelNewScene()
{
	newSceneFolderId.clear();
	newSceneEdit->setVisible(false);
	newSceneHint->setVisible(false);
}

void SceneGroupsDock::commitNewScene()
{
	const QString name = newSceneEdit->text().trimmed();
	if (name.isEmpty()) {
		cancelNewScene();
		return;
	}
	if (nameExists(name)) {
		newSceneHint->setText(T("Scene.NameTaken"));
		newSceneEdit->selectAll();
		return;
	}

	obs_scene_t *scene = obs_scene_create(name.toUtf8().constData());
	if (!scene)
		return;
	const QString uuid = QString::fromUtf8(obs_source_get_uuid(obs_scene_get_source(scene)));
	obs_scene_release(scene); /* the frontend keeps its own reference */

	if (Group *group = currentGroup())
		addScene(*group, uuid, newSceneFolderId);
	cancelNewScene();
	markDirty();

	/* Open the new scene, but never switch what is on air. */
	const bool onAir = obs_frontend_streaming_active() || obs_frontend_recording_active();
	QTimer::singleShot(150, this, [this, uuid, onAir]() {
		reconcile();
		if (obs_frontend_preview_program_mode_active() || !onAir)
			activateScene(uuid, false);
	});
}

void SceneGroupsDock::duplicateScene(const QString &uuid)
{
	obs_source_t *source = obs_get_source_by_uuid(uuid.toUtf8().constData());
	if (!source)
		return;
	obs_scene_t *scene = obs_scene_from_source(source);
	if (!scene) {
		obs_source_release(source);
		return;
	}

	const QString base = QString::fromUtf8(obs_source_get_name(source));
	QString name;
	for (int n = 2; n < 1000; n++) {
		name = QStringLiteral("%1 %2").arg(base).arg(n);
		if (!nameExists(name))
			break;
	}

	obs_scene_t *copy = obs_scene_duplicate(scene, name.toUtf8().constData(), OBS_SCENE_DUP_REFS);
	obs_source_release(source);
	if (!copy)
		return;
	const QString copyUuid = QString::fromUtf8(obs_source_get_uuid(obs_scene_get_source(copy)));
	obs_scene_release(copy);

	/* The copy joins the same groups, right after the original. */
	for (Group &group : groups) {
		for (qsizetype i = 0; i < group.items.size(); i++) {
			Node &node = group.items[i];
			if (node.folder) {
				const qsizetype index = node.children.indexOf(uuid);
				if (index >= 0)
					node.children.insert(index + 1, copyUuid);
			} else if (node.id == uuid) {
				Node copy;
				copy.id = copyUuid;
				group.items.insert(i + 1, copy);
				break;
			}
		}
	}
	markDirty();
	QTimer::singleShot(150, this, [this]() { reconcile(); });
}

void SceneGroupsDock::deleteScenes(const QStringList &uuids)
{
	if (uuids.isEmpty())
		return;

	if (listScenes().size() - uuids.size() < 1) {
		QMessageBox::information(this, T("Scene.Delete"), T("Scene.DeleteLast"));
		return;
	}

	QString question;
	if (uuids.size() == 1) {
		QString name;
		if (obs_source_t *source = obs_get_source_by_uuid(uuids.first().toUtf8().constData())) {
			name = QString::fromUtf8(obs_source_get_name(source));
			obs_source_release(source);
		}
		question = T("Scene.DeleteConfirm").arg(name);
	} else {
		question = T("Scene.DeleteManyConfirm").arg(uuids.size());
	}
	if (QMessageBox::question(this, T("Scene.Delete"), question) != QMessageBox::Yes)
		return;

	for (const QString &uuid : uuids) {
		if (obs_source_t *source = obs_get_source_by_uuid(uuid.toUtf8().constData())) {
			obs_source_remove(source);
			obs_source_release(source);
		}
	}
	markDirty();
	QTimer::singleShot(150, this, [this]() { reconcile(); });
}
