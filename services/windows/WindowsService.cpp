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
#include "platform/windows/Win32Error.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "diagnostics/performance/PrecisionTimer.h"
#include "services/windows/WindowsService.h"
#include "platform/windows/Win32Resource.h"

using std::make_shared;
using std::shared_ptr;
using std::vector;
using std::wstring;

void WindowsServiceControl::restart(const wstring& serviceName)
{
	winutil::UniqueServiceHandle scManager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS));
	if (!scManager)
		throw WindowsServiceError(L"OpenSCManager failed (" + win32::errorMessage(GetLastError()) + L")");

	vector<shared_ptr<WindowsService>> services;
	shared_ptr<WindowsService> mainService = make_shared<WindowsService>(scManager.get(), serviceName, true);
	services.push_back(mainService);

	DWORD mainState = mainService->getState();
	if (mainState == SERVICE_RUNNING)
	{
		vector<wstring> dependentServices = mainService->getActiveDependentServices();
		for (const wstring& dependentServiceName : dependentServices)
		{
			shared_ptr<WindowsService> dependentService = make_shared<WindowsService>(scManager.get(), dependentServiceName.c_str(), false);
			services.insert(prev(services.end()), dependentService);
		}
	}

	PrecisionTimer timer;
	timer.start();
	for (shared_ptr<WindowsService> service : services)
	{
		DWORD state = service->getState();
		if (state == SERVICE_RUNNING)
			state = service->stop();

		while (state != SERVICE_STOPPED)
		{
			if (timer.stop() > 30)
				throw WindowsServiceError(L"Service stop timed out on service \"" + service->getServiceName() + L"\"");

			Sleep(100);

			state = service->getState();
		}
	}

	// all services should be stopped now, so start them again

	for (auto it = services.rbegin(); it != services.rend(); it++)
	{
		shared_ptr<WindowsService> service = *it;
		service->start();

		DWORD state = service->getState();
		double retryDelay = 5;
		while (state != SERVICE_RUNNING)
		{
			double time = timer.stop();
			if (time > 30)
				throw WindowsServiceError(L"Service start timed out on service \"" + service->getServiceName() + L"\"");
			if (time > retryDelay)
			{
				// sometimes, the service won't start on the first try
				service->start();
				retryDelay = time + 5;
			}

			Sleep(100);

			state = service->getState();
		}
	}
}

WindowsService::WindowsService(SC_HANDLE scManager, const std::wstring& serviceName, bool allowEnumerate)
	: serviceName(serviceName)
{
	DWORD desiredAccess = SERVICE_START | SERVICE_STOP | SERVICE_QUERY_STATUS;
	if (allowEnumerate)
		desiredAccess |= SERVICE_ENUMERATE_DEPENDENTS;
	serviceHandle.reset(OpenServiceW(scManager, serviceName.c_str(), desiredAccess));
	if (!serviceHandle)
		fail(L"OpenService", GetLastError());
}

const std::wstring& WindowsService::getServiceName()
{
	return serviceName;
}

DWORD WindowsService::getState()
{
	SERVICE_STATUS_PROCESS ssp;
	DWORD dwBytesNeeded;
	if (!QueryServiceStatusEx(serviceHandle.get(), SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(SERVICE_STATUS_PROCESS), &dwBytesNeeded))
		fail(L"QueryServiceStatusEx", GetLastError());

	return ssp.dwCurrentState;
}

void WindowsService::start()
{
	if (!StartServiceW(serviceHandle.get(), 0, nullptr))
		fail(L"StartService", GetLastError());
}

DWORD WindowsService::stop()
{
	SERVICE_STATUS ss;
	if (!ControlService(serviceHandle.get(), SERVICE_CONTROL_STOP, &ss))
		fail(L"ControlService", GetLastError());

	return ss.dwCurrentState;
}

vector<wstring> WindowsService::getActiveDependentServices()
{
	DWORD bytesNeeded, count;
	if (EnumDependentServicesW(serviceHandle.get(), SERVICE_ACTIVE, nullptr, 0, &bytesNeeded, &count))
		// if the call succeeds, there are no dependent services
		return vector<wstring>();

	DWORD error = GetLastError();
	if (error != ERROR_MORE_DATA)
		fail(L"EnumDependentServices", error);

	winutil::UniqueProcessHeapPtr<ENUM_SERVICE_STATUSW> dependencies(
		static_cast<LPENUM_SERVICE_STATUSW>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytesNeeded)));
	if (!dependencies)
		throw WindowsServiceError(L"HeapAlloc for EnumDependentServices failed");

	if (!EnumDependentServicesW(serviceHandle.get(), SERVICE_ACTIVE, dependencies.get(), bytesNeeded, &bytesNeeded, &count))
		fail(L"EnumDependentServices", GetLastError());

	vector<wstring> result;
	for (unsigned i = 0; i < count; i++)
		result.push_back(dependencies.get()[i].lpServiceName);

	return result;
}

void WindowsService::fail(const wstring& functionName, DWORD error)
{
	throw WindowsServiceError(functionName + L" failed for service \"" + serviceName + L"\" (" + win32::errorMessage(error) + L")");
}
