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
#include <platform/windows/WindowsVersion.h>
#include <services/windows/WindowsService.h>
#include <devices/DeviceAPOInfoKeys.h>
#include <platform/windows/ComPtr.h>
#include <ObjBase.h>
#include "DeviceTestThread.h"

using std::find;
using std::thread;

DeviceTestThread::DeviceTestThread(QObject* parent, const QVector<std::shared_ptr<DeviceAPOInfo>>& devices)
	: QThread(parent)
{
	bool isNewerWindows = WindowsVersion::isAtLeast(6, 3); // Windows 8.1
	for (const std::shared_ptr<DeviceAPOInfo>& apoInfo : devices)
	{
		if (apoInfo->isDisabled() || apoInfo->isUnplugged())
			continue;

		DeviceTestInfo testInfo(apoInfo);
		if (apoInfo->getSelectedInstallState().autoAdjust)
		{
			if (isNewerWindows)
			{
				testInfo.remainingInstallModes.append(DeviceAPOInfo::INSTALL_SFX_EFX);
				testInfo.remainingInstallModes.append(DeviceAPOInfo::INSTALL_SFX_MFX);
			}
			testInfo.remainingInstallModes.append(DeviceAPOInfo::INSTALL_LFX_GFX);
		}
		else
		{
			testInfo.remainingInstallModes.append(apoInfo->getSelectedInstallState().installMode);
		}
		testInfo.bestInstallMode = apoInfo->getSelectedInstallState().installMode;
		testInfo.wantsOriginalApoPreMix = apoInfo->getSelectedInstallState().useOriginalAPOPreMix || apoInfo->getOriginalAPOPreMix() == L"";
		testInfo.wantsOriginalApoPostMix = apoInfo->getSelectedInstallState().useOriginalAPOPostMix || apoInfo->getOriginalAPOPostMix() == L"";

		infoMap.insert(QString::fromStdWString(apoInfo->getDeviceGuid()).toLower(), testInfo);
	}
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
		std::wstring pipeName = L"EqualizerAPODeviceTest";
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
			auto testInfo = infoMap.find(deviceGuid);
			try
			{
				testInfo->remainingInstallModes.removeOne(testInfo->deviceInfo->getSelectedInstallState().installMode);
				testInfo->currentResult.childAPOPreMixOk = !testInfo->deviceInfo->getSelectedInstallState().useOriginalAPOPreMix || testInfo->deviceInfo->getOriginalAPOPreMix() == L"";
				testInfo->currentResult.childAPOPostMixOk = !testInfo->deviceInfo->getSelectedInstallState().useOriginalAPOPostMix || testInfo->deviceInfo->getOriginalAPOPostMix() == L"";
				if (testInfo->deviceInfo->getSelectedInstallState().installPreMix)
					emit setItemStatus(deviceGuid, false, ItemStatusType::waiting);
				if (testInfo->deviceInfo->getSelectedInstallState().installPostMix && !testInfo->deviceInfo->isInput())
					emit setItemStatus(deviceGuid, true, ItemStatusType::waiting);
				testInfo->deviceInfo->testAPOInstallation();
			}
			catch (const DeviceException& e)
			{
				emit showErrorDialog(QString::fromStdWString(e.getMessage()));
			}
		}

		const auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds{3};
		try
		{
			std::string message;
			while ((message= thread.waitUntil(timeout)) != "")
			{
				QJsonParseError error;
				QJsonDocument jsonDoc = QJsonDocument::fromJson(QByteArray(QString::fromStdString(message).toUtf8()), &error);
				if (jsonDoc.isNull())
				{
					emit logError(error.errorString());
					return;
				}
				QJsonObject jsonObj = jsonDoc.object();
				QString deviceGuid = jsonObj.value("deviceGuid").toString();
				QString stage = jsonObj.value("stage").toString();
				QString phase = jsonObj.value("phase").toString();
				auto testInfo = infoMap.find(deviceGuid.toLower());

				if (testInfo == infoMap.end())
				{
					emit logError(tr("Received unknown device GUID %1.").arg(deviceGuid));
					return;
				}

				TestResult& result = testInfo->currentResult;
				const DeviceAPOInfo::InstallState& installState = testInfo->deviceInfo->getSelectedInstallState();
				if (stage == "PreMix")
				{
					if (phase == "Initialize")
						result.preMixOk = true;
					else if (phase == "ChildAPO")
						result.childAPOPreMixOk = true;
					if (result.preMixOk && result.childAPOPreMixOk)
						emit setItemStatus(deviceGuid, false, ItemStatusType::success);
				}
				else if (stage == "PostMix")
				{
					if (phase == "Initialize")
						result.postMixOk = true;
					else if (phase == "ChildAPO")
						result.childAPOPostMixOk = true;
					if (result.postMixOk && result.childAPOPostMixOk)
						emit setItemStatus(deviceGuid, true, ItemStatusType::success);
				}

				if ((result.preMixOk && result.childAPOPreMixOk || !installState.installPreMix)
					&& (result.postMixOk && result.childAPOPostMixOk || !installState.installPostMix || testInfo->deviceInfo->isInput()))
				{
					remainingDevices.remove(deviceGuid.toLower());
					if (remainingDevices.isEmpty())
						break;
				}
			}

			if (!remainingDevices.isEmpty())
			{
				emit logError(tr("Check failed for %n device(s).", nullptr, remainingDevices.size()));
				QMutableSetIterator<QString> it(remainingDevices);
				while (it.hasNext())
				{
					QString deviceGuid = it.next();
					auto testInfo = infoMap.find(deviceGuid);
					DeviceAPOInfo::InstallState& installState = testInfo->deviceInfo->getSelectedInstallState();
					if (testInfo->currentResult.getScore() > testInfo->bestResult.getScore())
					{
						testInfo->bestInstallMode = installState.installMode;
						testInfo->bestResult = testInfo->currentResult;
					}

					DeviceAPOInfo::InstallMode installMode;
					TestResult result;
					if (!testInfo->remainingInstallModes.isEmpty())
					{
						installMode = testInfo->remainingInstallModes.first();
						result = testInfo->currentResult;
						testInfo->currentResult = TestResult();
					}
					else
					{
						installMode = testInfo->bestInstallMode;
						result = testInfo->bestResult;
						it.remove();
						nonWorkingDevices++;
					}
					if (installState.installPreMix)
						emit setItemStatus(deviceGuid, false, result.preMixOk ? (result.childAPOPreMixOk || !installState.useOriginalAPOPreMix ? ItemStatusType::success : ItemStatusType::warning) : ItemStatusType::error);
					if (installState.installPostMix && !testInfo->deviceInfo->isInput())
						emit setItemStatus(deviceGuid, true, result.postMixOk ? (result.childAPOPostMixOk || !installState.useOriginalAPOPostMix ? ItemStatusType::success : ItemStatusType::warning) : ItemStatusType::error);
					QString installModeName;
					switch (installMode)
					{
					case DeviceAPOInfo::INSTALL_LFX_GFX:
						installModeName = "LFX/GFX";
						break;
					case DeviceAPOInfo::INSTALL_SFX_MFX:
						installModeName = "SFX/MFX";
						break;
					case DeviceAPOInfo::INSTALL_SFX_EFX:
						installModeName = "SFX/EFX";
						break;
					}
					emit log(tr("Setting install mode for %1 %2 to %3.").arg(testInfo->deviceInfo->getDeviceName()).arg(testInfo->deviceInfo->getConnectionName()).arg(installModeName));

					installState.installMode = installMode;
					installState.useOriginalAPOPreMix = testInfo->wantsOriginalApoPreMix && testInfo->deviceInfo->getOriginalAPOPreMix() != L"";
					installState.useOriginalAPOPostMix = testInfo->wantsOriginalApoPostMix && testInfo->deviceInfo->getOriginalAPOPostMix() != L"";
					testInfo->deviceInfo->reinstall();
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

	nonWorking.store(nonWorkingDevices);
	if (nonWorkingDevices == 0)
		emit log("<b>" + tr("Checks done. No problems were detected.") + "</b>");
	else
		emit logError("<b>" + tr("Checks done. Problems were detected for %n device(s).", nullptr, nonWorkingDevices) + "</b>");
}
