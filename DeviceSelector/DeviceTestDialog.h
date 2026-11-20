/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
	The installation check, live: what the Device Selector shows after OK
	(issue #502). It lists the devices that carry the APO, runs
	DeviceTestThread over them and turns what the thread reports into the
	rows, notes and headline DeviceTestView draws. Devices the install step
	could not change are reported here too, beside the check's own findings.
*/
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <QSet>
#include <devices/DeviceAPOInfo.h>
#include "DeviceTestThread.h"
#include "DeviceTestView.h"

class DeviceTestDialog : public DeviceTestView
{
	Q_OBJECT

public:
	// A device the install step could not change.
	struct InstallFailure
	{
		QString guid;
		QString connection;
		QString device;
		bool input = false;
		bool defaultDevice = false;
		QString message;     // what the operation reported
		bool leftPartial = false; // undoing the change did not finish either
		// Makes the change again. Answers true when it went through;
		// otherwise it fills in what the new attempt reported. Empty when
		// the change cannot be made again from here.
		std::function<bool(InstallFailure& again)> retry;
		// Met while reinstalling from this dialog, not in the install step.
		bool fromReinstall = false;
	};

	// runCheck is false when nothing changed that Windows audio has to pick
	// up: the dialog then only reports the failures.
	DeviceTestDialog(const QVector<InstallFailure>& failures, bool runCheck, QWidget* parent = nullptr);
	void reject() override;
	void done(int result) override;

protected:
	__override void closeEvent(QCloseEvent* event);

private:
	struct Entry
	{
		Row row;
		bool tested = false;  // the thread checks it
		bool settled = false; // its row shows the final answer
		bool expectsPreMix = false;
		bool expectsPostMix = false;
		std::optional<ItemStatusType> preMix;
		std::optional<ItemStatusType> postMix;
		std::optional<Problem> problem;
		QString problemDetail;
		QStringList problemSystem;
		std::optional<Note> installNote;
	};

	void addDevices(const std::vector<std::shared_ptr<AbstractAPOInfo>>& infos, QVector<std::shared_ptr<DeviceAPOInfo>>& devices);
	void begin(const QSet<QString>& reinstall);
	void addFailures();
	Entry* entryFor(const QString& guid);
	void settle(Entry& entry, Problem problem, DeviceReport report, const QString& detail = QString(),
		const QStringList& system = QStringList());
	void showRow(const Entry& entry);
	void refresh();
	Tally tally() const;
	void stopTestThread();

	void log(const QString& message);
	void logError(const QString& message);
	void setItemStatus(const QString& guid, bool postMix, ItemStatusType statusType);
	void onDeviceGaveUp(const QString& guid, DeviceTestOutcome outcome, const QString& modeName);
	void onDeviceRefused(const QString& guid, const QString& message);
	void onDeviceUnreachable(const QString& guid, const QString& message);
	void onRunStopped(DeviceTestStop reason, const QString& message);
	void onStopRequested();
	void onReinstallRequested();
	void onThreadFinished();
	void onAbort(const QString& message, int code);

	QVector<InstallFailure> failures;
	QVector<Entry> entries;
	QHash<QString, int> entryIndex;
	std::optional<Note> runNote;
	Phase phase = Phase::Running;
	bool checks = true;
	bool stoppedByUser = false;
	int failureCount = 0;
	DeviceTestThread* thread = nullptr;
};
