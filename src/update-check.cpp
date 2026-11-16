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

#include "update-check.hpp"
#include "net-get.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QRegularExpression>
#include <QStringList>

static const char *RELEASES_URL = "https://api.github.com/repos/alejox/ac-scene-folders/releases/latest";
static const qint64 CHECK_INTERVAL_SECS = 24 * 60 * 60;

UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent)
{
	if (char *path = obs_module_config_path("update.json")) {
		settingsPath = QString::fromUtf8(path);
		bfree(path);
	}
	load();
}

UpdateChecker::~UpdateChecker()
{
	stop();
}

void UpdateChecker::load()
{
	QFile file(settingsPath);
	if (settingsPath.isEmpty() || !file.open(QIODevice::ReadOnly))
		return;
	const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
	enabled = json.value(QStringLiteral("enabled")).toBool(true);
	lastCheck = (qint64)json.value(QStringLiteral("lastCheck")).toDouble(0);
	latest = json.value(QStringLiteral("latest")).toString();
	latestUrl = json.value(QStringLiteral("url")).toString();
	dismissed = json.value(QStringLiteral("dismissed")).toString();
}

void UpdateChecker::save() const
{
	if (settingsPath.isEmpty())
		return;
	QDir().mkpath(QFileInfo(settingsPath).absolutePath());
	QJsonObject json;
	json.insert(QStringLiteral("enabled"), enabled);
	json.insert(QStringLiteral("lastCheck"), (double)lastCheck);
	json.insert(QStringLiteral("latest"), latest);
	json.insert(QStringLiteral("url"), latestUrl);
	json.insert(QStringLiteral("dismissed"), dismissed);
	QFile file(settingsPath);
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		file.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
}

/* Numeric compare of "1.2.3" style versions; a leading "v" and any suffix are ignored. */
int UpdateChecker::compareVersions(const QString &a, const QString &b)
{
	static const QRegularExpression leading(QStringLiteral("^[vV]"));
	const auto parts = [](QString text) {
		text.remove(leading);
		const int cut = text.indexOf(QRegularExpression(QStringLiteral("[^0-9.]")));
		if (cut >= 0)
			text.truncate(cut);
		QList<int> numbers;
		for (const QString &piece : text.split(QLatin1Char('.'), Qt::SkipEmptyParts))
			numbers.append(piece.toInt());
		return numbers;
	};
	const QList<int> left = parts(a);
	const QList<int> right = parts(b);
	for (int i = 0; i < qMax(left.size(), right.size()); i++) {
		const int x = i < left.size() ? left.at(i) : 0;
		const int y = i < right.size() ? right.at(i) : 0;
		if (x != y)
			return x < y ? -1 : 1;
	}
	return 0;
}

void UpdateChecker::announceIfNewer()
{
	if (!enabled || latest.isEmpty() || latestUrl.isEmpty())
		return;
	if (compareVersions(latest, QString::fromUtf8(PLUGIN_VERSION)) <= 0)
		return;
	if (!dismissed.isEmpty() && compareVersions(latest, dismissed) <= 0)
		return;
	emit updateAvailable(latest, latestUrl);
}

void UpdateChecker::start()
{
	if (!enabled || stopping || worker.joinable())
		return;

	/* A version found on an earlier day is still worth showing. */
	announceIfNewer();

	const qint64 now = QDateTime::currentSecsSinceEpoch();
	if (now - lastCheck < CHECK_INTERVAL_SECS)
		return;

	QPointer<UpdateChecker> self(this);
	const std::string userAgent = std::string("ac-scene-groups/") + PLUGIN_VERSION;
	worker = std::thread([self, userAgent, this]() {
		const acsg::HttpResponse response = acsg::https_get(RELEASES_URL, userAgent, 8000);
		if (stopping)
			return;

		QString version;
		QString url;
		bool ok = false;
		if (response.status == 200) {
			const QJsonObject json =
				QJsonDocument::fromJson(QByteArray::fromStdString(response.body)).object();
			version = json.value(QStringLiteral("tag_name")).toString();
			url = json.value(QStringLiteral("html_url")).toString();
			ok = !version.isEmpty() && url.startsWith(QStringLiteral("https://github.com/"));
		} else if (response.status == 404) {
			ok = true; /* no release published yet */
		} else {
			obs_log(LOG_INFO, "update check skipped: %s",
				response.error.empty() ? std::to_string(response.status).c_str()
						       : response.error.c_str());
		}

		/* Back on the UI thread; dropped if the checker is gone. */
		QMetaObject::invokeMethod(
			this,
			[self, version, url, ok]() {
				if (self)
					self->handleResult(version, url, ok);
			},
			Qt::QueuedConnection);
	});
}

void UpdateChecker::handleResult(const QString &version, const QString &url, bool ok)
{
	if (worker.joinable())
		worker.join();
	if (!ok || stopping)
		return;

	lastCheck = QDateTime::currentSecsSinceEpoch();
	if (!version.isEmpty()) {
		latest = version;
		latestUrl = url;
	}
	save();
	announceIfNewer();
}

void UpdateChecker::stop()
{
	stopping = true;
	if (worker.joinable())
		worker.join();
}

void UpdateChecker::setEnabled(bool on)
{
	enabled = on;
	save();
	if (on) {
		lastCheck = 0; /* turning it on checks right away */
		start();
	}
}

void UpdateChecker::dismiss(const QString &version)
{
	dismissed = version;
	save();
}
