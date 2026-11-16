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

#include <QObject>
#include <QString>

#include <atomic>
#include <thread>

/* Looks for a newer release on GitHub at most once a day and reports it.
 * It never downloads or installs anything: the dock shows a notice. */
class UpdateChecker : public QObject {
	Q_OBJECT

public:
	explicit UpdateChecker(QObject *parent = nullptr);
	~UpdateChecker() override;

	void start();
	void stop();

	bool isEnabled() const { return enabled; }
	void setEnabled(bool on);
	void dismiss(const QString &version);

	static int compareVersions(const QString &a, const QString &b);

signals:
	void updateAvailable(const QString &version, const QString &url);

private:
	void load();
	void save() const;
	void handleResult(const QString &version, const QString &url, bool ok);
	void announceIfNewer();

	QString settingsPath;
	bool enabled = true;
	qint64 lastCheck = 0;
	QString latest;
	QString latestUrl;
	QString dismissed;

	std::thread worker;
	std::atomic<bool> stopping{false};
};
