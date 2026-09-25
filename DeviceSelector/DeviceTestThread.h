/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2024  Jonas Thedering

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

#include <atomic>
#include <devices/DeviceAPOInfo.h>
#include <devices/DeviceTestPlan.h>
#include <QHash>
#include <QThread>
#include "ReceiveThread.h"

enum class ItemStatusType
{
	waiting,
	success,
	warning,
	error
};

Q_DECLARE_METATYPE(ItemStatusType)

class DeviceTestThread : public QThread
{
	Q_OBJECT

public:
	DeviceTestThread(QObject* parent, const QVector<std::shared_ptr<DeviceAPOInfo>>& devices);

	// How a run ended. Incomplete covers every exit before the verdict: a
	// pipe that could not be read, a message that was not JSON, an aborted
	// service restart, an interruption. Audit #348 TD-19: the headless command
	// used to read a count that starts at 0 and is only written at the very
	// end, so a run that stopped early reported "the APO is alive".
	enum class Verdict
	{
		Incomplete,
		Passed,
		Failed
	};

	// Valid once finished() fired. The dialog reads the verdict off the log;
	// the headless command (DeviceSelector --install-endpoint) needs it as a
	// value, and treats Incomplete as a failure.
	Verdict verdict() const {return verdictValue.load();}
	// How many devices no install mode worked for; meaningful for Failed.
	int nonWorkingDeviceCount() const {return nonWorking.load();}

signals:
	void log(const QString& message);
	void logError(const QString& message);
	void showErrorDialog(const QString& message);
	void abort(const QString& message, int code);
	void setItemStatus(const QString& guid, bool postMix, ItemStatusType statusType);
	void finished();

protected:
	__override void run();

private:
	// The ladder, the verdict and the rows' status are decided by
	// devices/DeviceTestPlan (audit #348 F15); this thread restarts the
	// service, reinstalls, listens on the pipe and reports.
	struct DeviceUnderTest
	{
		DeviceUnderTest(std::shared_ptr<DeviceAPOInfo> deviceInfo, const DeviceTestSelection& selection)
			: deviceInfo(std::move(deviceInfo)), plan(selection)
		{
		}

		std::shared_ptr<DeviceAPOInfo> deviceInfo;
		DeviceTestPlan plan;
	};

	void showStatus(const QString& deviceGuid, DeviceTestStage stage, DeviceTestItemStatus status);

	QHash<QString, DeviceUnderTest> infoMap;
	std::atomic<int> nonWorking{0};
	std::atomic<Verdict> verdictValue{Verdict::Incomplete};
};
