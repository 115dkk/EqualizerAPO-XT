/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "DeviceTestView.h"

#include <QCoreApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QScrollArea>
#include <QTextEdit>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "DeviceListDelegate.h"
#include "DisclosureHeader.h"
#include "SkinButton.h"

namespace
{
// The notes may take this much of the dialog before they scroll, so a long
// list of problems cannot push the devices out of sight.
const int notesMaxHeight = 300;
}

// One panel, painted by the active skin. Its height follows its text at the
// width the layout gives it.
class DevicePanelWidget : public QWidget
{
public:
	explicit DevicePanelWidget(QWidget* parent = nullptr)
		: QWidget(parent)
	{
		QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Fixed);
		policy.setHeightForWidth(true);
		setSizePolicy(policy);
	}

	void setState(const DevicePanelState& newState)
	{
		state = newState;
		if (isVisible())
			setFixedHeight(heightForWidth(width()));
		updateGeometry();
		update();
	}

	bool hasHeightForWidth() const override {return true;}
	int heightForWidth(int width) const override
	{
		return DeviceSkinPainter::active()->panelHeight(width, state, font(), DeviceSkinPainter::activeTokens());
	}
	// The layout asks before the widget has its width; answering with the
	// height at a narrow guess would hold the whole dialog taller than its
	// text needs. resizeEvent fixes the height once the width is known.
	QSize sizeHint() const override {return QSize(420, heightForWidth(width() > 240 ? width() : 580));}
	QSize minimumSizeHint() const override {return QSize(240, 24);}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter painter(this);
		painter.setFont(font());
		DeviceSkinPainter::active()->paintPanel(painter, rect(), state, font(), DeviceSkinPainter::activeTokens());
	}

	void resizeEvent(QResizeEvent* event) override
	{
		QWidget::resizeEvent(event);
		const int wanted = heightForWidth(width());
		if (wanted != height())
			setFixedHeight(wanted);
	}

private:
	DevicePanelState state;
};

DeviceTestView::DeviceTestView(QWidget* parent)
	: QDialog(parent)
{
	setWindowFlags(windowFlags().setFlag(Qt::WindowContextHelpButtonHint, false));
	setWindowTitle(tr("APO installation check"));

	const SkinTokens& tokens = DeviceSkinPainter::activeTokens();

	QVBoxLayout* column = new QVBoxLayout(this);

	// The answer first: one sentence that says where the check stands, and
	// under it the one thing the user has to know or do about it.
	verdict = new DevicePanelWidget(this);
	column->addWidget(verdict);

	// The same list the selector showed, painted by the same skin. Nothing
	// in it can be ticked here: the rows report.
	tree = new QTreeWidget(this);
	tree->setColumnCount(1);
	tree->setHeaderHidden(true);
	tree->setItemDelegate(new DeviceListDelegate(tree));
	tree->setRootIsDecorated(false);
	tree->setIndentation(0);
	tree->setExpandsOnDoubleClick(false);
	tree->setSelectionMode(QAbstractItemView::NoSelection);
	tree->setFocusPolicy(Qt::NoFocus);
	QPalette listPalette = tree->palette();
	listPalette.setBrush(QPalette::Highlight, Qt::transparent);
	tree->setPalette(listPalette);
	tree->setMinimumHeight(150);
	column->addWidget(tree, 1);

	// What went wrong and what to do about it, one panel per problem. The
	// dialog this replaces showed a red mark and left the rest to a log.
	notesHost = new QWidget;
	notesHost->setAttribute(Qt::WA_NoSystemBackground);
	notesColumn = new QVBoxLayout(notesHost);
	notesColumn->setContentsMargins(0, 0, 0, 0);
	notesColumn->setSpacing(6);
	notesScroll = new QScrollArea(this);
	notesScroll->setObjectName(QStringLiteral("deviceTestNotes"));
	notesScroll->setFrameShape(QFrame::NoFrame);
	notesScroll->setWidgetResizable(true);
	notesScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	notesScroll->setFocusPolicy(Qt::NoFocus);
	notesScroll->viewport()->setAutoFillBackground(false);
	notesScroll->setStyleSheet(QStringLiteral("QScrollArea#deviceTestNotes, QScrollArea#deviceTestNotes > QWidget > QWidget { background: transparent; }"));
	notesScroll->setWidget(notesHost);
	notesScroll->setVisible(false);
	notesHost->installEventFilter(this);
	column->addWidget(notesScroll);

	// What the check did, line by line: what the user is asked to send when
	// a problem needs a report. Closed until they open it.
	recordHeader = new DisclosureHeader(this);
	recordHeader->setTitle(tr("Record"));
	column->addWidget(recordHeader);

	record = new QTextEdit(this);
	record->setObjectName(QStringLiteral("deviceTestRecord"));
	record->setReadOnly(true);
	QFont mono(tokens.monoFontFamily);
	mono.setPointSizeF(qMax(7.5, record->font().pointSizeF() - 0.5));
	record->setFont(mono);
	record->setFixedHeight(132);
	record->setVisible(false);
	column->addWidget(record);
	connect(recordHeader, &DisclosureHeader::toggled, record, &QWidget::setVisible);

	QHBoxLayout* buttons = new QHBoxLayout;
	buttons->addStretch(1);
	stopButton = new SkinButton(tr("Stop"), false, this);
	// Installs again and runs the check once more. Named for what it does
	// to the device; the check is how the user learns whether it worked.
	reinstallButton = new SkinButton(tr("Reinstall"), false, this);
	reinstallButton->setVisible(false);
	closeButton = new SkinButton(tr("Close"), true, this);
	closeButton->setVisible(false);
	buttons->addWidget(stopButton);
	buttons->addWidget(reinstallButton);
	buttons->addWidget(closeButton);
	connect(reinstallButton, &QPushButton::clicked, this, &DeviceTestView::reinstallRequested);
	column->addLayout(buttons);
	connect(stopButton, &QPushButton::clicked, this, &DeviceTestView::stopRequested);
	connect(closeButton, &QPushButton::clicked, this, [this]() {
		done(closeCode);
	});
}

void DeviceTestView::applyRow(QTreeWidgetItem* item, const Row& row)
{
	item->setData(0, DeviceListDelegate::StateTextRole, row.state);
	item->setData(0, DeviceListDelegate::ReportRole, static_cast<int>(row.report));
	// The device's own instrument carries the answer: lit while the APO
	// is alive, armed while the check runs, out when it found nothing.
	const bool lit = row.report != DeviceReport::Dead;
	const bool settled = row.report == DeviceReport::Alive || row.report == DeviceReport::Degraded
		|| row.report == DeviceReport::Untestable;
	item->setData(0, DeviceListDelegate::InstalledRole, lit && settled);
	item->setData(0, DeviceListDelegate::UnavailableRole, row.report == DeviceReport::Untestable);
	item->setData(0, Qt::CheckStateRole, lit ? Qt::Checked : Qt::Unchecked);
}

void DeviceTestView::setRows(const QVector<Row>& rows)
{
	tree->clear();
	items.clear();
	QTreeWidgetItem* sections[2] = {nullptr, nullptr};
	for (const Row& row : rows)
	{
		const int side = row.input ? 1 : 0;
		if (sections[side] == nullptr)
		{
			sections[side] = new QTreeWidgetItem(tree, QStringList(QCoreApplication::translate("DeviceSelector",
				row.input ? "Capture devices" : "Playback devices")));
			sections[side]->setData(0, DeviceListDelegate::InputSideRole, row.input);
			sections[side]->setExpanded(true);
		}

		QTreeWidgetItem* item = new QTreeWidgetItem(sections[side], QStringList(row.connection));
		item->setData(0, DeviceListDelegate::DeviceNameRole, row.device);
		item->setData(0, DeviceListDelegate::DefaultDeviceRole, row.defaultDevice);
		item->setData(0, DeviceListDelegate::InputSideRole, row.input);
		item->setFlags(Qt::ItemIsEnabled);
		applyRow(item, row);
		if (!row.key.isEmpty())
			items.insert(row.key, item);
	}
}

void DeviceTestView::updateRow(const QString& key, const QString& state, DeviceReport report)
{
	QTreeWidgetItem* item = items.value(key);
	if (item == nullptr)
		return;
	Row row;
	row.state = state;
	row.report = report;
	applyRow(item, row);
}

void DeviceTestView::appendRecord(const QString& text, bool error)
{
	if (error)
		record->append(QStringLiteral("<span style=\"color:%1\">%2</span>")
			.arg(DeviceSkinPainter::activeTokens().danger, text.toHtmlEscaped()));
	else
		record->append(text.toHtmlEscaped());
}

DeviceTestView::ProblemText DeviceTestView::describe(Problem problem, const QString& detail)
{
	switch (problem)
	{
	case Problem::NoAnswerEveryMode:
		return {tr("The APO does not work"),
			tr("All three install modes were tried and Windows did not load the APO on this device in any of them."),
			tr("The cause could not be determined. Update the device's driver, then press Reinstall. Some drivers load no APO at all.")};
	case Problem::NoAnswerSelectedMode:
		return {tr("The APO does not work"),
			tr("Windows did not load the APO on this device in the selected install mode (%1).").arg(detail),
			tr("Turn on Automatic in the troubleshooting options, and the check tries the other modes too.")};
	case Problem::PostMixSilent:
		return {tr("Pre-mix works, post-mix does not"),
			tr("The pre-mix APO works. Windows did not load the post-mix APO on this device."),
			tr("Filters of the post-mix stage are not applied. Turn off post-mix install for this device, or try another install mode.")};
	case Problem::PreMixSilent:
		return {tr("Post-mix works, pre-mix does not"),
			tr("The post-mix APO works. Windows did not load the pre-mix APO on this device."),
			tr("Filters of the pre-mix stage are not applied. Try another install mode.")};
	case Problem::OriginalApoFailed:
		return {tr("The APO works, but the original APO could not start"),
			tr("The equalizer works. The APO of the device's maker, which runs beside it, did not start, so that maker's effects are missing."),
			// The APO is installed and working: nothing is asked of the user.
			QString()};
	case Problem::ModeChangeRefused:
		return {tr("The install mode could not be changed"),
			tr("The check tried to install in another mode and Windows refused the change. The device was left as it was before."),
			tr("Close programs that manage audio devices, then press Reinstall.")};
	case Problem::InstallRefused:
		return {tr("Not changed"),
			tr("Windows refused the change to this device. The device was left as it was before."),
			tr("Close programs that manage audio devices, then press Reinstall.")};
	case Problem::InstallLeftPartial:
		return {tr("Partly changed"),
			tr("Windows refused the change, and undoing it did not finish either. The device may be left partly changed."),
			tr("Restart the computer, then open the Device Selector and press OK.")};
	case Problem::DeviceDisabled:
		return {tr("Not checked: the device is disabled"),
			tr("The device is disabled in Windows, so nothing could be played through it."),
			tr("Enable the device, then press Reinstall.")};
	case Problem::DeviceUnplugged:
		return {tr("Not checked: the device is unplugged"),
			tr("Nothing is plugged into this device, so nothing could be played through it."),
			tr("Plug it in, then press Reinstall.")};
	case Problem::DeviceUnreachable:
		return {tr("Not checked: the device could not be opened"),
			tr("Nothing could be played through this device, so the APO on it could not answer."),
			tr("Press Reinstall to try again.")};
	case Problem::AudioRestartFailed:
		return {QString(),
			tr("Windows audio could not be restarted, so the check did not run. The APO is installed, but it starts working only after Windows audio restarts."),
			tr("Restart the computer.")};
	case Problem::ComUnavailable:
		return {QString(),
			tr("A Windows component the check needs did not start, so the check did not run."),
			tr("Restart the computer, then open the Device Selector and press OK.")};
	case Problem::TestSetupRefused:
		return {QString(),
			tr("Windows blocked the setting the check uses to hear from the APO, so the check did not run."),
			tr("Close security programs that block changes to system settings, then press Reinstall.")};
	case Problem::ResultChannelBroken:
		return {QString(),
			tr("The check lost its connection to the APO before every device had answered."),
			tr("Press Reinstall to try again.")};
	case Problem::DeviceListUnreadable:
		return {QString(),
			tr("The list of audio devices could not be read, so no device was checked."),
			tr("Press Reinstall to try again.")};
	case Problem::StoppedByUser:
		return {QString(),
			tr("The check was stopped. The APO is installed, but whether it works was not checked."),
			tr("Press Reinstall to try again.")};
	}
	return {};
}

void DeviceTestView::setNotes(const QVector<Note>& list)
{
	while (QLayoutItem* old = notesColumn->takeAt(0))
	{
		delete old->widget();
		delete old;
	}
	for (const Note& note : list)
	{
		DevicePanelState state;
		state.tone = note.tone;
		state.title = note.subject;
		state.lines.append({DevicePanelState::LineKind::Body, note.text.cause});
		if (!note.text.action.isEmpty())
			state.lines.append({DevicePanelState::LineKind::Action, note.text.action});
		for (const QString& line : note.system)
		{
			if (!line.isEmpty())
				state.lines.append({DevicePanelState::LineKind::System, line});
		}
		DevicePanelWidget* panel = new DevicePanelWidget(notesHost);
		panel->setState(state);
		notesColumn->addWidget(panel);
	}
	notesScroll->setVisible(!list.isEmpty());
	fitNotes();
}

void DeviceTestView::fitNotes()
{
	// The panels learn their height from their width, so the scroll area is
	// sized from the width they are about to get: as tall as its notes, up
	// to the cap. Before the first layout that width is the dialog's own.
	int available = notesScroll->viewport()->width();
	if (!notesScroll->isVisible() || available < 100)
	{
		const QMargins margins = layout()->contentsMargins();
		available = width() - margins.left() - margins.right();
	}
	int wanted = 0;
	for (int i = 0; i < notesColumn->count(); i++)
	{
		const QWidget* panel = notesColumn->itemAt(i)->widget();
		if (panel != nullptr)
			wanted += panel->heightForWidth(available) + (i > 0 ? notesColumn->spacing() : 0);
	}
	const int height = qBound(24, wanted, notesMaxHeight);
	if (notesScroll->maximumHeight() != height || notesScroll->minimumHeight() != height)
		notesScroll->setFixedHeight(height);
}

void DeviceTestView::resizeEvent(QResizeEvent* event)
{
	QDialog::resizeEvent(event);
	fitNotes();
}

bool DeviceTestView::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == notesHost && event->type() == QEvent::Resize)
		fitNotes();
	return QDialog::eventFilter(watched, event);
}

void DeviceTestView::setStopping()
{
	stopButton->setEnabled(false);
}

void DeviceTestView::setReinstallOffered(bool offered)
{
	reinstallButton->setVisible(offered);
}

void DeviceTestView::clearRecord()
{
	record->clear();
}

void DeviceTestView::setPhase(Phase phase, const Tally& tally)
{
	DevicePanelState state;
	QString hint;
	switch (phase)
	{
	case Phase::Running:
		state.tone = DeviceReport::Waiting;
		if (tally.total > 0)
			state.title = tr("Checking that the APO works on each device (%1 of %2)").arg(tally.done).arg(tally.total);
		else
			state.title = tr("Restarting Windows audio");
		hint = tr("Windows audio restarts during the check, so sound stops for a few seconds.");
		break;
	case Phase::Done:
		if (tally.dead > 0)
		{
			state.tone = DeviceReport::Dead;
			state.title = tr("The APO does not work on %n device(s).", nullptr, tally.dead);
			if (tally.total > tally.dead)
				hint = tr("The other devices are not affected.");
		}
		else if (tally.unchanged > 0)
		{
			state.tone = DeviceReport::Dead;
			state.title = tr("%n device(s) could not be changed.", nullptr, tally.unchanged);
			if (tally.checked && tally.total > 0)
				hint = tr("The APO works on the devices that were checked.");
		}
		else if (tally.attention > 0)
		{
			state.tone = DeviceReport::Degraded;
			state.title = tr("The APO works. %n device(s) have something to look at.", nullptr, tally.attention);
		}
		else if (tally.total == 0 && tally.unchecked == 0)
		{
			state.tone = DeviceReport::Unchecked;
			state.title = tr("The APO is not installed on any device.");
		}
		else if (tally.unchecked > 0)
		{
			state.tone = tally.total > 0 ? DeviceReport::Alive : DeviceReport::Unchecked;
			state.title = tally.total > 0 ? tr("The APO works on every device that could be checked.")
				: tr("No device could be checked.");
			hint = tr("%n device(s) could not be checked.", nullptr, tally.unchecked);
		}
		else
		{
			state.tone = DeviceReport::Alive;
			state.title = tr("The APO works on every device.");
			hint = tr("The equalizer is applied from now on.");
		}
		break;
	case Phase::Stopped:
		state.tone = DeviceReport::Unchecked;
		state.title = tr("The check did not finish.");
		hint = tr("Whether the APO works is not known.");
		break;
	}
	if (!hint.isEmpty())
		state.lines.append({DevicePanelState::LineKind::Body, hint});
	verdict->setState(state);

	const bool running = phase == Phase::Running;
	stopButton->setVisible(running);
	stopButton->setEnabled(true);
	if (running)
		reinstallButton->setVisible(false);
	closeButton->setVisible(!running);
	if (!running)
		closeButton->setFocus();
}

void DeviceTestView::loadPreview(Preview preview)
{
	auto threadText = [](const char* source) {
		return QCoreApplication::translate("DeviceTestThread", source);
	};
	const QString alive = tr("The APO works");
	const QString waiting = tr("Checking");

	QVector<Row> rows;
	appendRecord(threadText("Restarting audio service..."), false);
	Tally tally;

	switch (preview)
	{
	case Preview::Running:
		rows.append({QStringLiteral("a"), QStringLiteral("Speakers"), QStringLiteral("TOPPING USB DAC"), alive, DeviceReport::Alive, false, true});
		rows.append({QStringLiteral("b"), QStringLiteral("CABLE Input"), QStringLiteral("VB-Audio Virtual Cable"),
			tr("Pre-mix works. Checking post-mix"), DeviceReport::Waiting, false, false});
		rows.append({QStringLiteral("c"), QStringLiteral("Headphones"), QStringLiteral("Realtek(R) Audio"), waiting, DeviceReport::Waiting, false, false});
		rows.append({QStringLiteral("d"), QStringLiteral("Microphone"), QStringLiteral("USB Audio Device"), waiting, DeviceReport::Waiting, true, true});
		setRows(rows);
		appendRecord(threadText("Checking APO installation..."), false);
		tally.done = 1;
		tally.total = 4;
		setPhase(Phase::Running, tally);
		break;
	case Preview::Passed:
		rows.append({QStringLiteral("a"), QStringLiteral("Speakers"), QStringLiteral("TOPPING USB DAC"), alive, DeviceReport::Alive, false, true});
		rows.append({QStringLiteral("b"), QStringLiteral("CABLE Input"), QStringLiteral("VB-Audio Virtual Cable"), alive, DeviceReport::Alive, false, false});
		rows.append({QStringLiteral("c"), QStringLiteral("Headphones"), QStringLiteral("Realtek(R) Audio"), alive, DeviceReport::Alive, false, false});
		rows.append({QStringLiteral("d"), QStringLiteral("Microphone"), QStringLiteral("USB Audio Device"), alive, DeviceReport::Alive, true, true});
		setRows(rows);
		appendRecord(threadText("Checking APO installation..."), false);
		appendRecord(threadText("Checks done. No problems were detected."), false);
		tally.done = 4;
		tally.total = 4;
		setPhase(Phase::Done, tally);
		break;
	case Preview::Problem:
		rows.append({QStringLiteral("a"), QStringLiteral("Speakers"), QStringLiteral("TOPPING USB DAC"), alive, DeviceReport::Alive, false, true});
		rows.append({QStringLiteral("b"), QStringLiteral("CABLE Input"), QStringLiteral("VB-Audio Virtual Cable"),
			describe(Problem::OriginalApoFailed).row, DeviceReport::Degraded, false, false});
		rows.append({QStringLiteral("c"), QStringLiteral("Headphones"), QStringLiteral("Realtek(R) Audio"),
			describe(Problem::NoAnswerEveryMode).row, DeviceReport::Dead, false, false});
		rows.append({QStringLiteral("e"), QStringLiteral("Digital Output"), QStringLiteral("NVIDIA High Definition Audio"),
			describe(Problem::DeviceUnplugged).row, DeviceReport::Untestable, false, false});
		rows.append({QStringLiteral("d"), QStringLiteral("Microphone"), QStringLiteral("USB Audio Device"), alive, DeviceReport::Alive, true, true});
		setRows(rows);
		appendRecord(threadText("Checking APO installation..."), false);
		appendRecord(threadText("Trying other configurations..."), false);
		appendRecord(threadText("Restarting audio service..."), false);
		appendRecord(threadText("Checking APO installation..."), false);
		appendRecord(QCoreApplication::translate("DeviceTestThread", "Check failed for %n device(s).", nullptr, 1), true);
		appendRecord(QCoreApplication::translate("DeviceTestThread", "Checks done. Problems were detected for %n device(s).", nullptr, 1), true);
		tally.done = 4;
		tally.total = 4;
		tally.dead = 1;
		tally.attention = 1;
		tally.unchecked = 1;
		setPhase(Phase::Done, tally);
		setNotes({{QStringLiteral("Headphones"), describe(Problem::NoAnswerEveryMode), {}, DeviceReport::Dead},
			{QStringLiteral("CABLE Input"), describe(Problem::OriginalApoFailed), {}, DeviceReport::Degraded},
			{QStringLiteral("Digital Output"), describe(Problem::DeviceUnplugged), {}, DeviceReport::Untestable}});
		setReinstallOffered(true);
		break;
	case Preview::Stopped:
	{
		const QString unchecked = tr("Not checked");
		rows.append({QStringLiteral("a"), QStringLiteral("Speakers"), QStringLiteral("TOPPING USB DAC"), unchecked, DeviceReport::Unchecked, false, true});
		rows.append({QStringLiteral("b"), QStringLiteral("CABLE Input"), QStringLiteral("VB-Audio Virtual Cable"), unchecked, DeviceReport::Unchecked, false, false});
		rows.append({QStringLiteral("c"), QStringLiteral("Headphones"), QStringLiteral("Realtek(R) Audio"), unchecked, DeviceReport::Unchecked, false, false});
		setRows(rows);
		appendRecord(threadText("Restart failed."), true);
		tally.total = 3;
		setPhase(Phase::Stopped, tally);
		setNotes({{tr("Windows audio"), describe(Problem::AudioRestartFailed),
			{QStringLiteral("Error while stopping service \"audiosrv\": Access is denied. (5)")}, DeviceReport::Dead}});
		setReinstallOffered(true);
		break;
	}
	}
}
