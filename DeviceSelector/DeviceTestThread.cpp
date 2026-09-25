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

#include "stdafx.h"
#include "services/registry/RegistryPaths.h"
#include <chrono>
#include <services/registry/WindowsRegistry.h>
#include <services/windows/WindowsService.h>
#include <devices/DeviceAPOInfoKeys.h>
#include <devices/DeviceTestWire.h>
#include <platform/windows/ComPtr.h>
#include <ObjBase.h>
#include "DeviceTestThread.h"

using std::find;
using std::thread;

namespace
{
ItemStatusType itemStatusType(DeviceTestItemStatus status)
{
	switch (status)
	{
	case DeviceTestItemStatus::Waiting:
		return ItemStatusType::waiting;
	case DeviceTestItemStatus::Success:
		return ItemStatusType::success;
	case DeviceTestItemStatus::Warning:
		return ItemStatusType::warning;
	case DeviceTestItemStatus::Error:
		break;
	}
	return ItemStatusType::error;
}
}

DeviceTestThread::DeviceTestThread(QObject* parent, const QVector<std::shared_ptr<DeviceAPOInfo>>& devices)
	: QThread(parent)
{
	for (const std::shared_ptr<DeviceAPOInfo>& apoInfo : devices)
	{
		if (apoInfo->isDisabled() || apoInfo->isUnplugged())
			continue;

		infoMap.insert(QString::fromStdWString(apoInfo->getDeviceGuid()).toLower(),
			DeviceUnderTest(apoInfo, deviceTestSelectionOf(*apoInfo)));
	}
}

void DeviceTestThread::showStatus(const QString& deviceGuid, DeviceTestStage stage, DeviceTestItemStatus status)
{
	emit setItemStatus(deviceGuid, stage == DeviceTestStage::PostMix, itemStatusType(status));
}

void DeviceTestThread::run()
{
	SCOPE_EXIT{emit finished(); };

	winutil::ComApartment apartment(COINIT_MULTITHREADED);
	if (!apartment.isUsable())
	{
		emit logError(tr("Could not initialize COM for device testing."));
		emit abort(tr("COM initialization failed (0x%1).")
			.arg(static_cast<qulonglong>(apartment.status()), 8, 16, QLatin1Char('0')), -1);
		return;
	}

	if (isInterruptionRequested())
		return;

	try
	{
		emit log(tr("Restarting audio service..."));
		WindowsServiceControl::restart(audioServiceName);
	}
	catch (const WindowsServiceError& e)
	{
		emit logError(tr("Restart failed."));
		emit abort(QString::fromStdWString(e.getMessage()), -1);
		return;
	}

	QList<QString> keys = infoMap.keys();
	QSet<QString> remainingDevices = QSet<QString>(keys.begin(), keys.end());
	int nonWorkingDevices = 0;

	while (!remainingDevices.isEmpty())
	{
		if (isInterruptionRequested())
			break;

		emit log(tr("Checking APO installation..."));
		std::wstring pipeName = devicetest::wire::kPipeName;
		ReceiveThread thread(pipeName);

		try
		{
			systemRegistry().writeValue(APP_REGPATH, deviceTestPipeValueName, pipeName);
		}
		catch (const RegistryError& e)
		{
			emit logError(tr("Could not prepare the device test."));
			emit abort(QString::fromStdWString(e.getMessage()), -1);
			return;
		}
		SCOPE_EXIT{
			try
			{
				systemRegistry().deleteValue(APP_REGPATH, deviceTestPipeValueName);
			}
			catch (const RegistryError& e)
			{
				emit logError(tr("Could not remove the device test registration: %1")
					.arg(QString::fromStdWString(e.getMessage())));
			}
		};

		for (QString deviceGuid : remainingDevices)
		{
			if (isInterruptionRequested())
				return;

			auto testInfo = infoMap.find(deviceGuid);
			try
			{
				const DeviceTestSelection selection = deviceTestSelectionOf(*testInfo->deviceInfo);
				testInfo->plan.beginAttempt(selection);
				if (DeviceTestPlan::expectsPreMix(selection))
					showStatus(deviceGuid, DeviceTestStage::PreMix, DeviceTestItemStatus::Waiting);
				if (DeviceTestPlan::expectsPostMix(selection))
					showStatus(deviceGuid, DeviceTestStage::PostMix, DeviceTestItemStatus::Waiting);
				testInfo->deviceInfo->testAPOInstallation();
			}
			catch (const WideError& e)
			{
				emit showErrorDialog(QString::fromStdWString(e.getMessage()));
			}
		}

		const auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds{3};
		try
		{
			std::string message;
			while (!isInterruptionRequested() && (message = thread.waitUntil(timeout)) != "")
			{
				QJsonParseError error;
				QJsonDocument jsonDoc = QJsonDocument::fromJson(QByteArray(QString::fromStdString(message).toUtf8()), &error);
				if (jsonDoc.isNull())
				{
					emit logError(error.errorString());
					return;
				}
				QJsonObject jsonObj = jsonDoc.object();
				QString deviceGuid = jsonObj.value(QLatin1String(devicetest::wire::kKeyDeviceGuid)).toString();
				const std::optional<DeviceTestStage> stage = deviceTestStageFromWire(
					jsonObj.value(QLatin1String(devicetest::wire::kKeyStage)).toString().toStdString());
				const std::optional<DeviceTestPhase> phase = deviceTestPhaseFromWire(
					jsonObj.value(QLatin1String(devicetest::wire::kKeyPhase)).toString().toStdString());
				auto testInfo = infoMap.find(deviceGuid.toLower());

				if (testInfo == infoMap.end())
				{
					// Another endpoint's APO can answer on the same pipe (an
					// app opening a stream elsewhere during the test). That is
					// not a verdict about the devices under test: note it and
					// keep waiting for theirs.
					emit logError(tr("Received unknown device GUID %1.").arg(deviceGuid));
					continue;
				}

				if (stage)
				{
					if (const std::optional<DeviceTestStage> completed = testInfo->plan.record(*stage, phase))
						showStatus(deviceGuid, *completed, DeviceTestItemStatus::Success);
				}

				if (testInfo->plan.satisfied(deviceTestSelectionOf(*testInfo->deviceInfo)))
				{
					remainingDevices.remove(deviceGuid.toLower());
					if (remainingDevices.isEmpty())
						break;
				}
			}

			if (isInterruptionRequested())
				return;

			if (!remainingDevices.isEmpty())
			{
				emit logError(tr("Check failed for %n device(s).", nullptr, remainingDevices.size()));
				QMutableSetIterator<QString> it(remainingDevices);
				while (it.hasNext())
				{
					if (isInterruptionRequested())
						return;

					QString deviceGuid = it.next();
					auto testInfo = infoMap.find(deviceGuid);
					DeviceAPOInfo::InstallState& installState = testInfo->deviceInfo->getSelectedInstallState();
					// The rows show this round against the state it was tried in,
					// so the selection is read before the mode changes.
					const DeviceTestSelection selection = deviceTestSelectionOf(*testInfo->deviceInfo);
					const DeviceTestPlan::Fallback fallback = testInfo->plan.fallBack(selection);
					if (fallback.givesUp)
					{
						it.remove();
						nonWorkingDevices++;
					}
					if (DeviceTestPlan::expectsPreMix(selection))
						showStatus(deviceGuid, DeviceTestStage::PreMix,
							DeviceTestPlan::statusFor(fallback.shown, DeviceTestStage::PreMix, selection));
					if (DeviceTestPlan::expectsPostMix(selection))
						showStatus(deviceGuid, DeviceTestStage::PostMix,
							DeviceTestPlan::statusFor(fallback.shown, DeviceTestStage::PostMix, selection));
					const QString installModeName = QString::fromLatin1(deviceTestModeName(fallback.mode));
					emit log(tr("Setting install mode for %1 %2 to %3.").arg(testInfo->deviceInfo->getDeviceName()).arg(testInfo->deviceInfo->getConnectionName()).arg(installModeName));

					installState.installMode = static_cast<DeviceAPOInfo::InstallMode>(fallback.mode);
					// Which driver APO the new mode chains to depends on the
					// mode, so these are asked after it is set.
					installState.useOriginalAPOPreMix = testInfo->plan.chainsOriginalApoPreMix(testInfo->deviceInfo->getOriginalAPOPreMix() != L"");
					installState.useOriginalAPOPostMix = testInfo->plan.chainsOriginalApoPostMix(testInfo->deviceInfo->getOriginalAPOPostMix() != L"");
					// A refused write rolls the endpoint back and throws; out of
					// QThread::run that was std::terminate for the diagnostic
					// tool itself (audit #348 TD-06). The device stops being
					// tested and counts as not working.
					auto giveUp = [&](const std::wstring& error) {
						emit logError(QString::fromStdWString(error));
						if (remainingDevices.contains(deviceGuid))
						{
							it.remove();
							nonWorkingDevices++;
						}
					};
					try
					{
						testInfo->deviceInfo->reinstall();
					}
					catch (const WideError& e)
					{
						giveUp(e.getMessage());
					}
				}

				if (!remainingDevices.isEmpty())
					emit log(tr("Trying other configurations..."));

				try
				{
					emit log(tr("Restarting audio service..."));
					WindowsServiceControl::restart(audioServiceName);
				}
				catch (const WindowsServiceError& e)
				{
					emit logError(tr("Restart failed."));
					emit abort(QString::fromStdWString(e.getMessage()), -1);
					return;
				}
			}
		}
		catch (const ReceiveException& e)
		{
			emit showErrorDialog(QString::fromStdWString(e.getMessage()));
			return;
		}
	}

	if (isInterruptionRequested())
		return;

	nonWorking.store(nonWorkingDevices);
	verdictValue.store(nonWorkingDevices == 0 ? Verdict::Passed : Verdict::Failed);
	if (nonWorkingDevices == 0)
		emit log("<b>" + tr("Checks done. No problems were detected.") + "</b>");
	else
		emit logError("<b>" + tr("Checks done. Problems were detected for %n device(s).", nullptr, nonWorkingDevices) + "</b>");
}
