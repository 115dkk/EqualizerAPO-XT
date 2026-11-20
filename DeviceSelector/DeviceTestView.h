/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
	The installation check as the Device Selector's skins present it: the
	devices that carry the APO, each on the row it had in the selector, with
	the check's answer in place of the install state. The view holds no test
	logic; it shows the rows, the notes and the record it is handed, so the
	review captures and the live dialog (DeviceTestDialog) draw the same
	thing.
*/
#pragma once

#include <QDialog>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include "skins/DeviceSkinPainter.h"

class DevicePanelWidget;
class DisclosureHeader;
class QScrollArea;
class QVBoxLayout;
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
		QString key; // the device's GUID; updateRow finds the row by it
		QString connection;
		QString device;
		QString state;
		DeviceReport report = DeviceReport::Waiting;
		bool input = false;
		bool defaultDevice = false;
	};

	enum class Phase
	{
		Running,
		Done,
		Stopped // the check itself could not run to its end
	};

	// What the headline counts. Devices, not stages.
	struct Tally
	{
		int done = 0;      // devices the check has an answer for
		int total = 0;     // devices under check
		int dead = 0;      // the APO does not work
		int unchanged = 0; // the install step was refused
		int attention = 0; // the APO works, with something missing
		int unchecked = 0; // disabled, unplugged or unreachable
		bool checked = true; // false when nothing changed that needed a check
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
		DeviceUnreachable,     // nothing could be played through it
		AudioRestartFailed,
		ComUnavailable,
		TestSetupRefused,
		ResultChannelBroken,
		DeviceListUnreadable,
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
		QString subject;    // the device, or empty for the run as a whole
		ProblemText text;
		QStringList system; // what Windows said, and where the log is
		DeviceReport tone = DeviceReport::Dead;
	};
	void setNotes(const QVector<Note>& notes);

	explicit DeviceTestView(QWidget* parent = nullptr);

	void setRows(const QVector<Row>& rows);
	void updateRow(const QString& key, const QString& state, DeviceReport report);
	void appendRecord(const QString& text, bool error);
	void setPhase(Phase phase, const Tally& tally);
	// Stop was pressed and the check has not ended yet.
	void setStopping();

	// The canned states of the review captures.
	enum class Preview
	{
		Running,
		Passed,
		Problem,
		Stopped
	};
	void loadPreview(Preview preview);

signals:
	void stopRequested();

protected:
	// What exec() answers when Close is pressed.
	void setCloseCode(int code) {closeCode = code;}
	bool eventFilter(QObject* watched, QEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

private:
	void applyRow(QTreeWidgetItem* item, const Row& row);
	void fitNotes();

	DevicePanelWidget* verdict = nullptr;
	QTreeWidget* tree = nullptr;
	QHash<QString, QTreeWidgetItem*> items;
	QScrollArea* notesScroll = nullptr;
	QWidget* notesHost = nullptr;
	QVBoxLayout* notesColumn = nullptr;
	DisclosureHeader* recordHeader = nullptr;
	QTextEdit* record = nullptr;
	SkinButton* stopButton = nullptr;
	SkinButton* closeButton = nullptr;
	int closeCode = QDialog::Accepted;
};
