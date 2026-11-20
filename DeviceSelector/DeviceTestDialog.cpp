/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include <QDir>
#include <devices/DeviceAPOInfo.h>
#include <devices/DeviceTestPlan.h>
#include <platform/windows/ComPtr.h>
#include <services/windows/AudioServiceNames.h>
#include <services/windows/WindowsService.h>
#include <ObjBase.h>
#include <algorithm>
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

DeviceTestDialog::DeviceTestDialog(const QVector<InstallFailure>& failures, const QVector<NewInstall>& installed,
	bool runCheck, QWidget* parent)
	: DeviceTestView(parent), failures(failures), installed(installed), checks(runCheck)
{
	resize(620, 640);
	connect(this, &DeviceTestView::stopRequested, this, &DeviceTestDialog::onStopRequested);
	connect(this, &DeviceTestView::reinstallRequested, this, &DeviceTestDialog::onReinstallRequested);
	begin({});
}

void DeviceTestDialog::begin(const QSet<QString>& reinstall)
{
	entries.clear();
	entryIndex.clear();
	runNote.reset();
	stoppedByUser = false;
	phase = Phase::Running;
	setCloseCode(QDialog::Accepted);

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

	// Reinstall: the devices that had a problem get their registration
	// written again before the check asks them. A refusal joins the changes
	// that could not be made; the device is still asked.
	for (const std::shared_ptr<DeviceAPOInfo>& device : devices)
	{
		if (!reinstall.contains(keyOf(device->getDeviceGuid())))
			continue;
		try
		{
			log(DeviceTestView::tr("Reinstalling %1 %2").arg(QString::fromStdWString(device->getConnectionName()),
				QString::fromStdWString(device->getDeviceName())));
			device->reinstall();
		}
		catch (const WideError& e)
		{
			InstallFailure failure;
			failure.guid = QString::fromStdWString(device->getDeviceGuid());
			failure.connection = QString::fromStdWString(device->getConnectionName());
			failure.device = QString::fromStdWString(device->getDeviceName());
			failure.input = device->isInput();
			failure.defaultDevice = device->isDefaultDevice();
			failure.message = QString::fromStdWString(e.getMessage());
			failure.fromReinstall = true;
			failures.append(failure);
		}
	}
	addFailures();

	QVector<Row> rows;
	for (const Entry& entry : entries)
		rows.append(entry.row);
	setRows(rows);

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

void DeviceTestDialog::addFailures()
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
	const Tally counts = tally();
	setPhase(phase, counts);
	// Offered once there is something a second attempt could change.
	setReinstallOffered(phase == Phase::Stopped || phase == Phase::StoppedByUser
		|| (phase == Phase::Done && counts.dead + counts.unchanged + counts.attention + counts.unchecked > 0));
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

	// A new attempt starts with every stage waiting. What an earlier attempt
	// said about the other stage does not carry over into it.
	if (statusType == ItemStatusType::waiting)
		(postMix ? entry->preMix : entry->postMix).reset();
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

void DeviceTestDialog::onReinstallRequested()
{
	if ((thread != nullptr && thread->isRunning()) || (undoThread != nullptr && undoThread->isRunning()))
		return;

	// Every device the check did not find working is installed again.
	QSet<QString> reinstall;
	for (const Entry& entry : entries)
	{
		if (entry.tested && entry.row.report != DeviceReport::Alive)
			reinstall.insert(entry.row.key);
	}

	// The changes that were refused are tried again as they were asked for.
	// A refusal met while reinstalling is not kept: its device is in the
	// list and is reinstalled above. A device left partly changed stays
	// reported; the user was told to restart the computer first.
	QVector<InstallFailure> remaining;
	for (const InstallFailure& failure : failures)
	{
		if (failure.fromReinstall)
			continue;
		InstallFailure again = failure;
		if (!failure.retry || !failure.retry(again))
			remaining.append(again);
	}
	failures = remaining;

	// What a stop took off is installed again.
	for (const NewInstall& change : removed)
	{
		const QString error = change.redo();
		if (error.isEmpty())
		{
			installed.append(change);
			continue;
		}
		InstallFailure failure;
		failure.guid = change.guid;
		failure.connection = change.connection;
		failure.device = change.device;
		failure.input = change.input;
		failure.defaultDevice = change.defaultDevice;
		failure.message = error;
		failures.append(failure);
	}
	removed.clear();

	if (undoThread != nullptr)
	{
		undoThread->deleteLater();
		undoThread = nullptr;
	}
	if (thread != nullptr)
	{
		thread->disconnect(this);
		thread->deleteLater();
		thread = nullptr;
	}
	// Whatever was written, Windows audio has to pick it up.
	checks = true;
	clearRecord();
	begin(reinstall);
}

void DeviceTestDialog::onThreadFinished()
{
	const bool reachedVerdict = thread != nullptr && thread->verdict() != DeviceTestThread::Verdict::Incomplete;
	if (reachedVerdict && !runNote)
	{
		phase = Phase::Done;
	}
	else if (stoppedByUser && !runNote)
	{
		// The user stopped the install, so what this OK installed comes off
		// again. The rows stay as they are until that is done.
		if (!installed.isEmpty())
		{
			undoInstalls();
			return;
		}
		showStopped({});
		return;
	}
	else
	{
		phase = Phase::Stopped;
		if (!runNote)
			runNote = Note{QString(), describe(Problem::ResultChannelBroken), {}, DeviceReport::Dead};
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

void DeviceTestDialog::undoInstalls()
{
	// Off the GUI thread: Windows audio restarts at the end so the APO that
	// was just removed is unloaded, and that takes seconds.
	const QVector<NewInstall> changes = installed;
	auto results = std::make_shared<QVector<QPair<QString, QString>>>();
	undoThread = QThread::create([changes, results]() {
		winutil::ComApartment apartment(COINIT_MULTITHREADED);
		for (const NewInstall& change : changes)
			results->append({change.guid, change.undo()});
		try
		{
			WindowsServiceControl::restart(audioServiceName);
		}
		catch (const WindowsServiceError& e)
		{
			results->append({QString(), QString::fromStdWString(e.getMessage())});
		}
	});
	undoThread->setParent(this);
	connect(undoThread, &QThread::finished, this, [this, results]() {
		showStopped(*results);
	});
	undoThread->start();
}

void DeviceTestDialog::showStopped(const QVector<QPair<QString, QString>>& undone)
{
	phase = Phase::StoppedByUser;
	const QString logLine = DeviceTestView::tr("Details are in %1.")
		.arg(QDir::toNativeSeparators(QString::fromStdWString(Logging::currentPath())));

	QVector<NewInstall> still;
	for (const QPair<QString, QString>& result : undone)
	{
		if (result.first.isEmpty())
		{
			// Windows audio did not restart after the removal.
			runNote = Note{DeviceTestView::tr("Windows audio"), describe(Problem::AudioRestartFailed),
					{result.second}, DeviceReport::Dead};
			logError(result.second);
			continue;
		}
		const auto change = std::find_if(installed.begin(), installed.end(), [&](const NewInstall& candidate) {
			return candidate.guid.compare(result.first, Qt::CaseInsensitive) == 0;
		});
		if (change == installed.end())
			continue;
		Entry* entry = entryFor(result.first);
		if (result.second.isEmpty())
		{
			removed.append(*change);
			if (entry != nullptr)
			{
				entry->row.state = DeviceTestView::tr("Install undone");
				entry->row.report = DeviceReport::Unchecked;
				entry->settled = true;
				entry->problem.reset();
				showRow(*entry);
			}
		}
		else
		{
			still.append(*change);
			logError(change->connection + QLatin1String(": ") + result.second);
			if (entry != nullptr)
				entry->installNote = Note{change->connection, describe(Problem::InstallRefused), {result.second, logLine},
						DeviceReport::Dead};
		}
	}
	if (!undone.isEmpty())
		installed = still;

	for (Entry& entry : entries)
	{
		if (entry.tested && !entry.settled)
		{
			entry.row.state = DeviceTestView::tr("Not checked");
			entry.row.report = DeviceReport::Unchecked;
			showRow(entry);
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
	// A removal in progress is waited for: cutting it off would leave a
	// device between installed and not.
	if (undoThread != nullptr && undoThread->isRunning())
	{
		undoThread->disconnect(this);
		undoThread->wait();
	}

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
	// Esc and the window's X end here. After an aborted run they answer
	// what Close answers, so DeviceSelector::finish still asks for the
	// reboot and the installer is not told the check passed (PR #503 review).
	if (closeCodeValue() != QDialog::Accepted)
	{
		done(closeCodeValue());
		return;
	}
	stopTestThread();
	DeviceTestView::reject();
}

void DeviceTestDialog::done(int result)
{
	stopTestThread();
	DeviceTestView::done(result);
}
