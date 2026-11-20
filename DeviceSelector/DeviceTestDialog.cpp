/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include <QDir>
#include <devices/DeviceAPOInfo.h>
#include <devices/DeviceTestPlan.h>
#include <services/registry/WindowsRegistry.h>
#include "DeviceTestDialog.h"
#include "services/logging/Logging.h"

namespace
{
QString keyOf(const std::wstring& guid)
{
	return QString::fromStdWString(guid).toLower();
}
}

DeviceTestDialog::DeviceTestDialog(const QVector<InstallFailure>& failures, bool runCheck, QWidget* parent)
	: DeviceTestView(parent), checks(runCheck)
{
	resize(620, 640);

	QVector<std::shared_ptr<DeviceAPOInfo>> devices;
	if (checks)
	{
		try
		{
			addDevices(DeviceAPOInfo::loadAllInfos(false), devices);
			addDevices(DeviceAPOInfo::loadAllInfos(true), devices);
		}
		catch (const RegistryError& e)
		{
			// Windows audio still has to restart for what was installed, so
			// the thread runs; there is only nothing it can ask.
			entries.clear();
			entryIndex.clear();
			devices.clear();
			runNote = Note{QString(), describe(Problem::DeviceListUnreadable),
					{QString::fromStdWString(e.getMessage())}, DeviceReport::Dead};
			logError(QString::fromStdWString(e.getMessage()));
		}
	}
	addFailures(failures);

	QVector<Row> rows;
	for (const Entry& entry : entries)
		rows.append(entry.row);
	setRows(rows);

	connect(this, &DeviceTestView::stopRequested, this, &DeviceTestDialog::onStopRequested);

	if (!checks)
	{
		phase = Phase::Done;
		refresh();
		return;
	}

	refresh();

	thread = new DeviceTestThread(this, devices);
	connect(thread, &DeviceTestThread::log, this, &DeviceTestDialog::log);
	connect(thread, &DeviceTestThread::logError, this, &DeviceTestDialog::logError);
	connect(thread, &DeviceTestThread::setItemStatus, this, &DeviceTestDialog::setItemStatus);
	connect(thread, &DeviceTestThread::deviceGaveUp, this, &DeviceTestDialog::onDeviceGaveUp);
	connect(thread, &DeviceTestThread::deviceRefused, this, &DeviceTestDialog::onDeviceRefused);
	connect(thread, &DeviceTestThread::deviceUnreachable, this, &DeviceTestDialog::onDeviceUnreachable);
	connect(thread, &DeviceTestThread::runStopped, this, &DeviceTestDialog::onRunStopped);
	connect(thread, &DeviceTestThread::finished, this, &DeviceTestDialog::onThreadFinished);
	connect(thread, &DeviceTestThread::abort, this, &DeviceTestDialog::onAbort);
	thread->start();
}

void DeviceTestDialog::addDevices(const std::vector<std::shared_ptr<AbstractAPOInfo>>& infos,
	QVector<std::shared_ptr<DeviceAPOInfo>>& devices)
{
	for (const std::shared_ptr<AbstractAPOInfo>& info : infos)
	{
		if (!info->isInstalled())
			continue;
		std::shared_ptr<DeviceAPOInfo> device = std::dynamic_pointer_cast<DeviceAPOInfo>(info);
		if (!device)
			continue;

		Entry entry;
		entry.row.key = keyOf(device->getDeviceGuid());
		entry.row.connection = QString::fromStdWString(device->getConnectionName());
		entry.row.device = QString::fromStdWString(device->getDeviceName());
		entry.row.input = device->isInput();
		entry.row.defaultDevice = device->isDefaultDevice();

		// The thread leaves these two out (DeviceTestThread's constructor):
		// nothing can be played through them, so they have no answer to give.
		if (device->isDisabled() || device->isUnplugged())
		{
			const Problem problem = device->isDisabled() ? Problem::DeviceDisabled : Problem::DeviceUnplugged;
			entry.problem = problem;
			entry.row.state = describe(problem).row;
			entry.row.report = DeviceReport::Untestable;
			entry.settled = true;
		}
		else
		{
			const DeviceTestSelection selection = deviceTestSelectionOf(*device);
			entry.tested = true;
			entry.expectsPreMix = DeviceTestPlan::expectsPreMix(selection);
			entry.expectsPostMix = DeviceTestPlan::expectsPostMix(selection);
			entry.row.state = DeviceTestView::tr("Checking");
			entry.row.report = DeviceReport::Waiting;
			devices.append(device);
		}

		entryIndex.insert(entry.row.key, entries.size());
		entries.append(entry);
	}
}

void DeviceTestDialog::addFailures(const QVector<InstallFailure>& failures)
{
	failureCount = failures.size();
	const QString logLine = DeviceTestView::tr("Details are in %1.")
		.arg(QDir::toNativeSeparators(QString::fromStdWString(Logging::currentPath())));

	for (const InstallFailure& failure : failures)
	{
		const Problem problem = failure.leftPartial ? Problem::InstallLeftPartial : Problem::InstallRefused;
		const Note note{failure.connection, describe(problem), {failure.message, logLine}, DeviceReport::Dead};
		logError(failure.connection + QLatin1String(": ") + failure.message);

		// A device that still carries the APO is in the list already and is
		// checked like the rest; the note says what was not done to it.
		if (Entry* existing = entryFor(failure.guid))
		{
			existing->installNote = note;
			continue;
		}

		Entry entry;
		entry.row.key = failure.guid.toLower();
		entry.row.connection = failure.connection;
		entry.row.device = failure.device;
		entry.row.input = failure.input;
		entry.row.defaultDevice = failure.defaultDevice;
		entry.row.state = describe(problem).row;
		entry.row.report = DeviceReport::Dead;
		entry.settled = true;
		entry.installNote = note;
		entryIndex.insert(entry.row.key, entries.size());
		entries.append(entry);
	}
}

DeviceTestDialog::Entry* DeviceTestDialog::entryFor(const QString& guid)
{
	const auto it = entryIndex.constFind(guid.toLower());
	return it == entryIndex.constEnd() ? nullptr : &entries[*it];
}

void DeviceTestDialog::showRow(const Entry& entry)
{
	updateRow(entry.row.key, entry.row.state, entry.row.report);
}

void DeviceTestDialog::settle(Entry& entry, Problem problem, DeviceReport report, const QString& detail,
	const QStringList& system)
{
	entry.problem = problem;
	entry.problemDetail = detail;
	entry.problemSystem = system;
	entry.row.state = describe(problem, detail).row;
	entry.row.report = report;
	entry.settled = true;
	showRow(entry);
	refresh();
}

DeviceTestView::Tally DeviceTestDialog::tally() const
{
	Tally result;
	result.checked = checks;
	result.unchanged = failureCount;
	for (const Entry& entry : entries)
	{
		// Disabled, unplugged and unreachable devices have no answer to
		// wait for: the headline counts them apart from the devices under
		// check.
		if (entry.row.report == DeviceReport::Untestable)
		{
			result.unchecked++;
			continue;
		}
		// A row the install step failed on and the check never saw is
		// counted by unchanged alone.
		if (!entry.tested)
			continue;
		result.total++;
		if (entry.settled)
			result.done++;
		if (entry.row.report == DeviceReport::Dead)
			result.dead++;
		else if (entry.row.report == DeviceReport::Degraded)
			result.attention++;
	}
	return result;
}

void DeviceTestDialog::refresh()
{
	QVector<Note> notes;
	if (runNote)
		notes.append(*runNote);

	// Disabled and unplugged devices share one sentence each; one panel
	// names them all, so a machine with many idle jacks does not bury the
	// devices that have a real problem.
	QStringList disabled;
	QStringList unplugged;
	const DeviceReport order[] = {DeviceReport::Dead, DeviceReport::Degraded, DeviceReport::Untestable};
	for (const Entry& entry : entries)
	{
		if (entry.installNote)
			notes.append(*entry.installNote);
	}
	for (DeviceReport report : order)
	{
		for (const Entry& entry : entries)
		{
			if (!entry.problem || !entry.settled || entry.row.report != report)
				continue;
			if (*entry.problem == Problem::DeviceDisabled)
				disabled.append(entry.row.connection);
			else if (*entry.problem == Problem::DeviceUnplugged)
				unplugged.append(entry.row.connection);
			else
				notes.append({entry.row.connection, describe(*entry.problem, entry.problemDetail), entry.problemSystem, report});
		}
	}
	if (!unplugged.isEmpty())
		notes.append({unplugged.join(QLatin1String(", ")), describe(Problem::DeviceUnplugged), {}, DeviceReport::Untestable});
	if (!disabled.isEmpty())
		notes.append({disabled.join(QLatin1String(", ")), describe(Problem::DeviceDisabled), {}, DeviceReport::Untestable});

	setNotes(notes);
	setPhase(phase, tally());
}

void DeviceTestDialog::log(const QString& message)
{
	// The thread wraps its last line in <b> for the dialog this replaced.
	QString text = message;
	text.remove(QLatin1String("<b>")).remove(QLatin1String("</b>"));
	appendRecord(text, false);
}

void DeviceTestDialog::logError(const QString& message)
{
	QString text = message;
	text.remove(QLatin1String("<b>")).remove(QLatin1String("</b>"));
	appendRecord(text, true);
}

void DeviceTestDialog::setItemStatus(const QString& guid, bool postMix, ItemStatusType statusType)
{
	Entry* entry = entryFor(guid);
	if (entry == nullptr || entry->settled)
		return;

	(postMix ? entry->postMix : entry->preMix) = statusType;

	auto is = [](const std::optional<ItemStatusType>& status, ItemStatusType wanted) {
		return status && *status == wanted;
	};
	auto failed = [&](const std::optional<ItemStatusType>& status) {
		return is(status, ItemStatusType::error) || is(status, ItemStatusType::warning);
	};
	const bool preMixWorks = !entry->expectsPreMix || is(entry->preMix, ItemStatusType::success);
	const bool postMixWorks = !entry->expectsPostMix || is(entry->postMix, ItemStatusType::success);

	if (preMixWorks && postMixWorks)
	{
		entry->row.state = DeviceTestView::tr("The APO works");
		entry->row.report = DeviceReport::Alive;
		entry->settled = true;
	}
	else if (failed(entry->preMix) || failed(entry->postMix))
	{
		// Not the answer yet: either the next mode of the ladder is tried,
		// or deviceGaveUp follows with what the device is reported as.
		entry->row.state = DeviceTestView::tr("Trying another install mode");
		entry->row.report = DeviceReport::Waiting;
	}
	else if (entry->expectsPreMix && entry->expectsPostMix && is(entry->preMix, ItemStatusType::success))
	{
		entry->row.state = DeviceTestView::tr("Pre-mix works. Checking post-mix");
		entry->row.report = DeviceReport::Waiting;
	}
	else if (entry->expectsPreMix && entry->expectsPostMix && is(entry->postMix, ItemStatusType::success))
	{
		entry->row.state = DeviceTestView::tr("Post-mix works. Checking pre-mix");
		entry->row.report = DeviceReport::Waiting;
	}
	else
	{
		entry->row.state = DeviceTestView::tr("Checking");
		entry->row.report = DeviceReport::Waiting;
	}
	showRow(*entry);
	if (entry->settled)
		refresh();
}

void DeviceTestDialog::onDeviceGaveUp(const QString& guid, DeviceTestOutcome outcome, const QString& modeName)
{
	Entry* entry = entryFor(guid);
	// A device that could not be reached runs out of modes as well; what the
	// user is told stays that it could not be reached.
	if (entry == nullptr || entry->settled)
		return;

	switch (outcome)
	{
	case DeviceTestOutcome::Works:
		entry->row.state = DeviceTestView::tr("The APO works");
		entry->row.report = DeviceReport::Alive;
		entry->settled = true;
		showRow(*entry);
		refresh();
		break;
	case DeviceTestOutcome::OriginalApoFailed:
		settle(*entry, Problem::OriginalApoFailed, DeviceReport::Degraded);
		break;
	case DeviceTestOutcome::PreMixSilent:
		settle(*entry, Problem::PreMixSilent, DeviceReport::Degraded);
		break;
	case DeviceTestOutcome::PostMixSilent:
		settle(*entry, Problem::PostMixSilent, DeviceReport::Degraded);
		break;
	case DeviceTestOutcome::SilentEveryMode:
		settle(*entry, Problem::NoAnswerEveryMode, DeviceReport::Dead);
		break;
	case DeviceTestOutcome::SilentSelectedMode:
		settle(*entry, Problem::NoAnswerSelectedMode, DeviceReport::Dead, modeName);
		break;
	}
}

void DeviceTestDialog::onDeviceRefused(const QString& guid, const QString& message)
{
	if (Entry* entry = entryFor(guid))
		settle(*entry, Problem::ModeChangeRefused, DeviceReport::Dead, QString(), {message});
}

void DeviceTestDialog::onDeviceUnreachable(const QString& guid, const QString& message)
{
	if (Entry* entry = entryFor(guid))
		settle(*entry, Problem::DeviceUnreachable, DeviceReport::Untestable, QString(), {message});
}

void DeviceTestDialog::onRunStopped(DeviceTestStop reason, const QString& message)
{
	Problem problem = Problem::ResultChannelBroken;
	QString subject;
	switch (reason)
	{
	case DeviceTestStop::ComUnavailable:
		problem = Problem::ComUnavailable;
		break;
	case DeviceTestStop::AudioRestartFailed:
		problem = Problem::AudioRestartFailed;
		subject = DeviceTestView::tr("Windows audio");
		break;
	case DeviceTestStop::SetupRefused:
		problem = Problem::TestSetupRefused;
		break;
	case DeviceTestStop::ChannelBroken:
		problem = Problem::ResultChannelBroken;
		break;
	}
	// The first reason is the one that ended the run.
	if (!runNote)
		runNote = Note{subject, describe(problem), {message}, DeviceReport::Dead};
}

void DeviceTestDialog::onStopRequested()
{
	if (thread == nullptr || !thread->isRunning())
		return;
	stoppedByUser = true;
	setStopping();
	// The thread looks for this between its steps; the longest of them is
	// the audio service restart. onThreadFinished shows the result.
	thread->requestInterruption();
}

void DeviceTestDialog::onThreadFinished()
{
	const bool reachedVerdict = thread != nullptr && thread->verdict() != DeviceTestThread::Verdict::Incomplete;
	if (reachedVerdict && !runNote)
	{
		phase = Phase::Done;
	}
	else
	{
		phase = Phase::Stopped;
		if (!runNote)
			runNote = Note{QString(), describe(stoppedByUser ? Problem::StoppedByUser : Problem::ResultChannelBroken),
					{}, stoppedByUser ? DeviceReport::Unchecked : DeviceReport::Dead};
		for (Entry& entry : entries)
		{
			if (entry.tested && !entry.settled)
			{
				entry.row.state = DeviceTestView::tr("Not checked");
				entry.row.report = DeviceReport::Unchecked;
				showRow(entry);
			}
		}
	}
	refresh();
}

void DeviceTestDialog::onAbort(const QString&, int code)
{
	// What ended the run is on the screen (onRunStopped). The code reaches
	// DeviceSelector::finish when the user closes the dialog, as it did when
	// this was a message box.
	setCloseCode(code);
}

void DeviceTestDialog::stopTestThread()
{
	// Esc and done() bypass closeEvent. All exits must release a running child
	// before the dialog destroys it (audit #467 TD-14). Worker signals are queued;
	// it never waits for the GUI thread that performs this bounded wait.
	if (thread != nullptr && thread->isRunning())
	{
		thread->requestInterruption();
		if (!thread->wait(5000))
		{
			LogFStatic(L"Device test thread did not stop within 5000 ms; detaching it from the closing dialog");
			thread->disconnect(this);
			thread->setParent(nullptr);
			connect(thread, &QThread::finished, thread, &QObject::deleteLater);
			if (!thread->isRunning())
				thread->deleteLater();
			thread = nullptr;
		}
	}
}

void DeviceTestDialog::closeEvent(QCloseEvent* event)
{
	stopTestThread();
	DeviceTestView::closeEvent(event);
}

void DeviceTestDialog::reject()
{
	stopTestThread();
	DeviceTestView::reject();
}

void DeviceTestDialog::done(int result)
{
	stopTestThread();
	DeviceTestView::done(result);
}
