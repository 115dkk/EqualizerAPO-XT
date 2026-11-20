/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
	The installation check as the Device Selector's skins present it: the
	devices that carry the APO, each on the row it had in the selector, with
	the check's answer in place of the install state. The view holds no test
	logic; it shows the rows and the record it is handed, so the review
	captures and the live dialog draw the same thing.
*/
#pragma once

#include <QDialog>
#include <QString>
#include <QVector>

#include "skins/DeviceSkinPainter.h"

class DisclosureHeader;
class QLabel;
class QTextEdit;
class QTreeWidget;
class QTreeWidgetItem;
class SkinButton;

class DeviceTestView : public QDialog
{
	Q_OBJECT

public:
	struct Row
	{
		QString connection;
		QString device;
		QString state;
		DeviceReport report = DeviceReport::Waiting;
		bool input = false;
		bool defaultDevice = false;
	};

	struct RecordLine
	{
		QString text;
		bool error = false;
	};

	enum class Phase
	{
		Running,
		Passed,
		Problem,
		Stopped // the check itself could not run to its end
	};

	// Everything the installation and its check can report. The first group
	// is about one device and comes with that device's name; the second is
	// about the run as a whole.
	enum class Problem
	{
		NoAnswerEveryMode,     // automatic adjustment tried all three modes
		NoAnswerSelectedMode,  // automatic adjustment off: only one was tried
		PostMixSilent,         // pre-mix came up, post-mix did not
		PreMixSilent,          // post-mix came up, pre-mix did not
		OriginalApoFailed,     // ours came up, the driver's APO beside it did not
		ModeChangeRefused,     // the reinstall in another mode was refused
		InstallRefused,        // the install was refused; the device is as before
		InstallLeftPartial,    // refused, and undoing it did not finish either
		DeviceDisabled,
		DeviceUnplugged,
		AudioRestartFailed,
		ComUnavailable,
		TestSetupRefused,
		ResultChannelBroken,
		StoppedByUser
	};

	struct ProblemText
	{
		QString row;    // the row's status sentence (device problems)
		QString cause;  // what happened
		QString action; // what the user can do about it
	};
	// detail fills %1 where a sentence has one: the install mode's name.
	static ProblemText describe(Problem problem, const QString& detail = QString());

	// One paragraph of the notes under the list.
	struct Note
	{
		QString subject; // the device, or empty for the run as a whole
		ProblemText text;
		QString system;  // the message Windows gave, when there is one
	};
	void setNotes(const QVector<Note>& notes);

	explicit DeviceTestView(QWidget* parent = nullptr);

	void setRows(const QVector<Row>& rows);
	void setRecord(const QVector<RecordLine>& lines);
	// done and total count devices; they feed the running headline only.
	void setPhase(Phase phase, int done, int total, int failed);

	// The three canned states of the review captures.
	enum class Preview
	{
		Running,
		Passed,
		Problem,
		Stopped
	};
	void loadPreview(Preview preview);

private:
	QLabel* headline = nullptr;
	QLabel* hint = nullptr;
	QTreeWidget* tree = nullptr;
	QLabel* notes = nullptr;
	DisclosureHeader* recordHeader = nullptr;
	QTextEdit* record = nullptr;
	SkinButton* stopButton = nullptr;
	SkinButton* closeButton = nullptr;
};
