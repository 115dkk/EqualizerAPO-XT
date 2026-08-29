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

	// How many devices no install mode worked for, once finished() fired.
	// The dialog reads the verdict off the log; the headless command
	// (DeviceSelector --install-endpoint) needs it as a number.
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
	class TestResult
	{
	public:
		bool preMixOk = false;
		bool postMixOk = false;
		bool childAPOPreMixOk = true;
		bool childAPOPostMixOk = true;

		int getScore()
		{
			int score = 0;
			if (preMixOk)
				score += 20;
			if (postMixOk)
				score += 10;
			if (!childAPOPreMixOk)
				score -= 2;
			if (!childAPOPostMixOk)
				score -= 1;
			return score;
		}
	};

	class DeviceTestInfo
	{
	public:
		DeviceTestInfo(std::shared_ptr<DeviceAPOInfo> deviceInfo)
			:deviceInfo(deviceInfo)
		{
		}

		std::shared_ptr<DeviceAPOInfo> deviceInfo;
		TestResult currentResult;
		QVector<DeviceAPOInfo::InstallMode> remainingInstallModes;
		DeviceAPOInfo::InstallMode bestInstallMode;
		TestResult bestResult;
		bool wantsOriginalApoPreMix = false;
		bool wantsOriginalApoPostMix = false;
	};

	QHash<QString, DeviceTestInfo> infoMap;
	std::atomic<int> nonWorking{0};
};
