/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "devices/ReportedOperation.h"

#include "runtime/errors/WideError.h"
#include "services/logging/Logging.h"
#include "services/registry/RegistryTransaction.h"

namespace
{
void finish(RegistryTransaction& plan, DeviceInstallReport& report)
{
	report.outcome = DeviceInstallReport::Outcome::Succeeded;
	report.appliedOperations = plan.appliedOperations();
	report.permissionsWidened = !plan.isFullyReversible();

	// The summary is worth a log line every time: it is how a support request
	// about a device that stopped working can be tied to the moment it was
	// installed. The registry detail goes behind trace, because it is long and
	// only interesting once something is wrong.
	LogFStatic(L"%s", report.toSummaryLine().c_str());
	for (const std::wstring& line : report.toLines())
		TraceFStatic(L"%s", line.c_str());
}

void fail(RegistryTransaction& plan, DeviceInstallReport& report, const std::wstring& failure)
{
	// Roll back here rather than letting the destructor do it, because the
	// report has to carry what the rollback could not put back, and the
	// destructor runs after this function is done.
	plan.rollback();

	report.outcome = DeviceInstallReport::Outcome::Failed;
	report.failure = failure;
	report.appliedOperations = plan.appliedOperations();
	report.rollbackFailures = plan.rollbackFailures();
	report.permissionsWidened = !plan.isFullyReversible();

	// A failure is logged in full: this is the block a user is asked for when
	// they report that installing did nothing.
	for (const std::wstring& line : report.toLines())
		LogFStatic(L"%s", line.c_str());
}
}

namespace ReportedOperation
{
	void run(IRegistry& registry, DeviceInstallReport& report,
		const std::function<void(RegistryTransaction&)>& steps)
	{
		RegistryTransaction plan(registry);
		try
		{
			steps(plan);
		}
		catch (const WideError& e)
		{
			fail(plan, report, e.getMessage());
			throw;
		}
		catch (...)
		{
			// Whatever it was, the device still has to be put back and the
			// report still has to say what happened before the caller sees
			// the exception.
			fail(plan, report, L"an exception of an unexpected type");
			throw;
		}

		plan.commit();
		finish(plan, report);
	}
}
