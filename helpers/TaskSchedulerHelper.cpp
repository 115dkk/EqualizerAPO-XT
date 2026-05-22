/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2024  Jonas Dahlinger

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
#define _WIN32_DCOM
#include <windows.h>
#include <iostream>
#include <stdio.h>
#include <comdef.h>
#include <taskschd.h>
#include <wrl/client.h>
#define SECURITY_WIN32
#include <Security.h>
#include "StringHelper.h"
#include "TaskSchedulerHelper.h"

using Microsoft::WRL::ComPtr;

void TaskSchedulerHelper::scheduleAtLogon(const std::wstring& taskName, const std::wstring& programPath, const std::wstring& programArgs, const std::wstring& workingDir)
{
	HRESULT hr = CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, 0, nullptr);
	if (FAILED(hr))
		fail(L"CoInitializeSecurity", hr);

	ComPtr<ITaskService> pService;
	hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pService));
	if (FAILED(hr))
		fail(L"CoCreateInstance", hr);

	hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
	if (FAILED(hr))
		fail(L"ITaskService::Connect", hr);

	ComPtr<ITaskFolder> pRootFolder;
	hr = pService->GetFolder(_bstr_t(L"\\"), &pRootFolder);
	if (FAILED(hr))
		fail(L"ITaskService::GetFolder", hr);

	// remove any existing task first
	pRootFolder->DeleteTask(_bstr_t(taskName.c_str()), 0);

	ComPtr<ITaskDefinition> pTask;
	hr = pService->NewTask(0, &pTask);

	if (FAILED(hr))
		fail(L"ITaskService::NewTask", hr);

	wchar_t userName[255];
	ULONG userNameSize = sizeof(userName)/sizeof(wchar_t);
	if (!GetUserNameExW(NameSamCompatible, userName, &userNameSize))
		fail(L"GetUserNameExW", GetLastError());

	ComPtr<IRegistrationInfo> pRegInfo;
	hr = pTask->get_RegistrationInfo(&pRegInfo);
	if (FAILED(hr))
		fail(L"ITaskDefinition::get_RegistrationInfo", hr);

	hr = pRegInfo->put_Author(userName);
	if (FAILED(hr))
		fail(L"IRegistrationInfo::put_Author", hr);

	{
		ComPtr<ITaskSettings> pSettings;
		hr = pTask->get_Settings(&pSettings);
		if (FAILED(hr))
			fail(L"ITaskDefinition::get_Settings", hr);

		hr = pSettings->put_StartWhenAvailable(VARIANT_TRUE);
		if (FAILED(hr))
			fail(L"ITaskSettings::put_StartWhenAvailable", hr);

		hr = pSettings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
		if (FAILED(hr))
			fail(L"ITaskSettings::put_DisallowStartIfOnBatteries", hr);

		hr = pSettings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
		if (FAILED(hr))
			fail(L"ITaskSettings::put_StopIfGoingOnBatteries", hr);

		hr = pSettings->put_RunOnlyIfNetworkAvailable(VARIANT_TRUE);
		if (FAILED(hr))
			fail(L"ITaskSettings::put_RunOnlyIfNetworkAvailable", hr);
	}

	ComPtr<ITriggerCollection> pTriggerCollection;
	hr = pTask->get_Triggers(&pTriggerCollection);
	if (FAILED(hr))
		fail(L"ITaskDefinition::get_Triggers", hr);

	ComPtr<ITrigger> pTrigger;
	hr = pTriggerCollection->Create(TASK_TRIGGER_LOGON, &pTrigger);
	if (FAILED(hr))
		fail(L"ITriggerCollection::Create", hr);

	{
		ComPtr<ILogonTrigger> pLogonTrigger;
		hr = pTrigger.As(&pLogonTrigger);
		if (FAILED(hr))
			fail(L"QueryInterface(ILogonTrigger)", hr);

		hr = pLogonTrigger->put_Id(_bstr_t(L"Trigger1"));
		if (FAILED(hr))
			fail(L"ITrigger::put_Id", hr);

		hr = pLogonTrigger->put_UserId(_bstr_t(userName));
		if (FAILED(hr))
			fail(L"ILogonTrigger::put_UserId", hr);

		ComPtr<IRepetitionPattern> repetitionPattern;
		hr = pLogonTrigger->get_Repetition(&repetitionPattern);
		if (FAILED(hr))
			fail(L"ILogonTrigger::get_Repetition", hr);

		hr = repetitionPattern->put_Interval(_bstr_t(L"P1D"));
		if (FAILED(hr))
			fail(L"IRepetitionPattern::put_Interval", hr);
	}

	ComPtr<IActionCollection> pActionCollection;
	hr = pTask->get_Actions(&pActionCollection);
	if (FAILED(hr))
		fail(L"ITaskDefinition::get_Actions", hr);

	ComPtr<IAction> pAction;
	hr = pActionCollection->Create(TASK_ACTION_EXEC, &pAction);
	if (FAILED(hr))
		fail(L"IActionCollection::Create", hr);

	{
		ComPtr<IExecAction> pExecAction;
		hr = pAction.As(&pExecAction);
		if (FAILED(hr))
			fail(L"QueryInterface(IExecAction)", hr);

		hr = pExecAction->put_Path(_bstr_t(programPath.c_str()));
		if (FAILED(hr))
			fail(L"IExecAction::put_Path", hr);

		hr = pExecAction->put_Arguments(_bstr_t(programArgs.c_str()));
		if (FAILED(hr))
			fail(L"IExecAction::put_Arguments", hr);

		hr = pExecAction->put_WorkingDirectory(_bstr_t(workingDir.c_str()));
		if (FAILED(hr))
			fail(L"IExecAction::put_WorkingDirectory", hr);
	}

	ComPtr<IRegisteredTask> pRegisteredTask;
	hr = pRootFolder->RegisterTaskDefinition(
		_bstr_t(taskName.c_str()),
		pTask.Get(),
		TASK_CREATE_OR_UPDATE,
		_variant_t(),
		_variant_t(),
		TASK_LOGON_INTERACTIVE_TOKEN,
		_variant_t(L""),
		&pRegisteredTask);
	if (FAILED(hr))
		fail(L"ITaskFolder::RegisterTaskDefinition", hr);
}

void TaskSchedulerHelper::unschedule(const std::wstring& taskName)
{
	HRESULT hr = CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, 0, nullptr);
	if (FAILED(hr))
		fail(L"CoInitializeSecurity", hr);

	ComPtr<ITaskService> pService;
	hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pService));
	if (FAILED(hr))
		fail(L"CoCreateInstance", hr);

	hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
	if (FAILED(hr))
		fail(L"ITaskService::Connect", hr);

	ComPtr<ITaskFolder> pRootFolder;
	hr = pService->GetFolder(_bstr_t(L"\\"), &pRootFolder);
	if (FAILED(hr))
		fail(L"ITaskService::GetFolder", hr);

	// remove any existing task first
	pRootFolder->DeleteTask(_bstr_t(taskName.c_str()), 0);
}

void TaskSchedulerHelper::fail(const std::wstring& functionName, unsigned long error)
{
	throw TaskSchedulerException(functionName + L" failed in task scheduling (" + StringHelper::getSystemErrorString(error) + L")");
}
