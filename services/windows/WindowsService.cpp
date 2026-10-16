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
#include "services/windows/ServiceWait.h"
#include "platform/windows/Win32Resource.h"

using std::make_shared;
using std::shared_ptr;
using std::vector;
using std::wstring;

namespace
{
winutil::UniqueServiceHandle openManager()
{
	winutil::UniqueServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
	if (!manager)
		throw WindowsServiceError(L"OpenSCManager failed (" + win32::errorMessage(GetLastError()) + L")");
	return manager;
}

void stopAndWait(WindowsService& service, ULONGLONG deadline)
{
	DWORD state = service.getState();
	if (state == SERVICE_START_PENDING)
	{
		service.waitFor(SERVICE_RUNNING, deadline);
		state = SERVICE_RUNNING;
	}
	if (state != SERVICE_STOPPED && state != SERVICE_STOP_PENDING)
		service.stop();
	service.waitFor(SERVICE_STOPPED, deadline);
}

void startAndWait(WindowsService& service, ULONGLONG deadline)
{
	DWORD state = service.getState();
	if (state == SERVICE_STOP_PENDING)
	{
		service.waitFor(SERVICE_STOPPED, deadline);
		state = SERVICE_STOPPED;
	}
	if (state != SERVICE_RUNNING && state != SERVICE_START_PENDING)
		service.start();
	service.waitFor(SERVICE_RUNNING, deadline);
}
}

bool WindowsServiceControl::stop(const wstring& serviceName, unsigned timeoutMs, bool* wasActive)
{
	auto manager = openManager();
	WindowsService service(manager.get(), serviceName, SERVICE_STOP | SERVICE_QUERY_STATUS);
	const bool active = service.getState() != SERVICE_STOPPED;
	if (wasActive != nullptr)
		*wasActive = active;
	stopAndWait(service, GetTickCount64() + timeoutMs);
	return active;
}

void WindowsServiceControl::start(const wstring& serviceName, unsigned timeoutMs)
{
	auto manager = openManager();
	WindowsService service(manager.get(), serviceName, SERVICE_START | SERVICE_QUERY_STATUS);
	startAndWait(service, GetTickCount64() + timeoutMs);
}

void WindowsServiceControl::restart(const wstring& serviceName, unsigned timeoutMs)
{
	auto manager = openManager();
	const ULONGLONG deadline = GetTickCount64() + timeoutMs;
	vector<shared_ptr<WindowsService>> services;
	auto mainService = make_shared<WindowsService>(manager.get(), serviceName,
		SERVICE_START | SERVICE_STOP | SERVICE_QUERY_STATUS | SERVICE_ENUMERATE_DEPENDENTS);
	services.push_back(mainService);
	if (mainService->getState() == SERVICE_RUNNING)
	{
		for (const auto& name : mainService->getActiveDependentServices())
			services.insert(prev(services.end()), make_shared<WindowsService>(manager.get(), name,
				SERVICE_START | SERVICE_STOP | SERVICE_QUERY_STATUS));
	}
	for (const auto& service : services)
		stopAndWait(*service, deadline);
	for (auto it = services.rbegin(); it != services.rend(); ++it)
		startAndWait(**it, deadline);
}

WindowsService::WindowsService(SC_HANDLE scManager, const std::wstring& serviceName, DWORD desiredAccess)
	: serviceName(serviceName)
{
	serviceHandle.reset(OpenServiceW(scManager, serviceName.c_str(), desiredAccess));
	if (!serviceHandle)
		fail(L"OpenService", GetLastError());
}

void WindowsService::waitFor(DWORD state, ULONGLONG deadline)
{
	if (!servicecontrol::waitFor(state, deadline, [this] {return getState();},
		[] {return GetTickCount64();}, [](ULONGLONG delay) {Sleep(static_cast<DWORD>(delay));}))
		throw WindowsServiceError(L"Service transition timed out on service \"" + serviceName + L"\"");
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
