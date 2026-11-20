/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "DeviceTestView.h"

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "DeviceListDelegate.h"
#include "DisclosureHeader.h"
#include "SkinButton.h"

DeviceTestView::DeviceTestView(QWidget* parent)
	: QDialog(parent)
{
	setWindowFlags(windowFlags().setFlag(Qt::WindowContextHelpButtonHint, false));
	setWindowTitle(tr("APO installation check"));

	const SkinTokens& tokens = DeviceSkinPainter::activeTokens();

	QVBoxLayout* column = new QVBoxLayout(this);

	// The answer first: one sentence that says where the check stands, and
	// under it the one thing the user has to know or do about it.
	headline = new QLabel(this);
	QFont strong = headline->font();
	strong.setBold(true);
	headline->setFont(strong);
	headline->setWordWrap(true);
	column->addWidget(headline);

	hint = new QLabel(this);
	hint->setWordWrap(true);
	hint->setStyleSheet(QStringLiteral("color: %1;").arg(tokens.mutedText));
	column->addWidget(hint);

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
	column->addWidget(tree, 1);

	// What went wrong and what to do about it, one paragraph per device.
	// The dialog this replaces showed a red mark and left the rest to a log.
	notes = new QLabel(this);
	notes->setObjectName(QStringLiteral("deviceTestNotes"));
	notes->setWordWrap(true);
	notes->setTextFormat(Qt::RichText);
	notes->setTextInteractionFlags(Qt::TextSelectableByMouse);
	notes->setVisible(false);
	column->addWidget(notes);

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
	closeButton = new SkinButton(tr("Close"), true, this);
	buttons->addWidget(stopButton);
	buttons->addWidget(closeButton);
	column->addLayout(buttons);
	connect(stopButton, &QPushButton::clicked, this, &QDialog::reject);
	connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

void DeviceTestView::setRows(const QVector<Row>& rows)
{
	tree->clear();
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
		item->setData(0, DeviceListDelegate::StateTextRole, row.state);
		item->setData(0, DeviceListDelegate::DefaultDeviceRole, row.defaultDevice);
		item->setData(0, DeviceListDelegate::InputSideRole, row.input);
		item->setData(0, DeviceListDelegate::ReportRole, static_cast<int>(row.report));
		// The device's own instrument carries the answer: lit while the APO
		// is alive, armed while the check runs, out when it found nothing.
		const bool lit = row.report != DeviceReport::Dead;
		const bool settled = row.report == DeviceReport::Alive || row.report == DeviceReport::Degraded
			|| row.report == DeviceReport::Untestable;
		item->setData(0, DeviceListDelegate::InstalledRole,
			lit && (settled || row.report == DeviceReport::Unchecked));
		item->setData(0, DeviceListDelegate::UnavailableRole, row.report == DeviceReport::Untestable);
		item->setData(0, Qt::CheckStateRole, lit ? Qt::Checked : Qt::Unchecked);
		item->setFlags(Qt::ItemIsEnabled);
	}
}

void DeviceTestView::setRecord(const QVector<RecordLine>& lines)
{
	const SkinTokens& tokens = DeviceSkinPainter::activeTokens();
	record->clear();
	for (const RecordLine& line : lines)
	{
		if (line.error)
			record->append(QStringLiteral("<span style=\"color:%1\">%2</span>").arg(tokens.danger, line.text.toHtmlEscaped()));
		else
			record->append(line.text.toHtmlEscaped());
	}
}

DeviceTestView::ProblemText DeviceTestView::describe(Problem problem, const QString& detail)
{
	switch (problem)
	{
	case Problem::NoAnswerEveryMode:
		return {tr("The APO does not work"),
			tr("All three install modes were tried and Windows did not load the APO on this device in any of them."),
			tr("The cause could not be determined. Update the device's driver and check again; some drivers load no APO at all.")};
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
			tr("If you do not use those effects, turn off Use original APO in the troubleshooting options.")};
	case Problem::ModeChangeRefused:
		return {tr("The install mode could not be changed"),
			tr("The check tried to install in another mode and Windows refused the change. The device was left as it was before."),
			tr("Close programs that manage audio devices, then check again.")};
	case Problem::InstallRefused:
		return {tr("Not installed"),
			tr("Windows refused the change to this device. The device was left as it was before."),
			tr("Close programs that manage audio devices, then install again.")};
	case Problem::InstallLeftPartial:
		return {tr("Partly changed"),
			tr("Windows refused the change, and undoing it did not finish either. The device may be left partly changed."),
			tr("Restart the computer before installing again.")};
	case Problem::DeviceDisabled:
		return {tr("Not checked: the device is disabled"),
			tr("The device is disabled in Windows, so nothing could be played through it."),
			tr("Enable the device, then open the Device Selector and press OK to check again.")};
	case Problem::DeviceUnplugged:
		return {tr("Not checked: the device is unplugged"),
			tr("Nothing is plugged into this device, so nothing could be played through it."),
			tr("Plug it in, then open the Device Selector and press OK to check again.")};
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
			tr("Close security programs that block changes to system settings, then open the Device Selector and press OK.")};
	case Problem::ResultChannelBroken:
		return {QString(),
			tr("The check lost its connection to the APO before every device had answered."),
			tr("Open the Device Selector and press OK to check again.")};
	case Problem::StoppedByUser:
		return {QString(),
			tr("The check was stopped. The APO is installed, but whether it works was not checked."),
			tr("Open the Device Selector and press OK to check again.")};
	}
	return {};
}

void DeviceTestView::setNotes(const QVector<Note>& list)
{
	const SkinTokens& tokens = DeviceSkinPainter::activeTokens();
	QString html;
	for (const Note& note : list)
	{
		html += QStringLiteral("<p style=\"margin:0 0 8px 0\">");
		if (!note.subject.isEmpty())
			html += QStringLiteral("<b>%1</b><br>").arg(note.subject.toHtmlEscaped());
		html += note.text.cause.toHtmlEscaped();
		html += QStringLiteral("<br>") + note.text.action.toHtmlEscaped();
		if (!note.system.isEmpty())
			html += QStringLiteral("<br><span style=\"color:%1; font-family:'%2'\">%3</span>")
				.arg(tokens.mutedText, tokens.monoFontFamily, note.system.toHtmlEscaped());
		html += QStringLiteral("</p>");
	}
	notes->setText(html);
	notes->setVisible(!list.isEmpty());
}

void DeviceTestView::setPhase(Phase phase, int done, int total, int failed)
{
	switch (phase)
	{
	case Phase::Running:
		headline->setText(tr("Checking that the APO works on each device (%1 of %2)").arg(done).arg(total));
		hint->setText(tr("Windows audio restarts during the check, so sound stops for a few seconds."));
		break;
	case Phase::Passed:
		headline->setText(tr("The APO works on every device."));
		hint->setText(tr("The equalizer is applied from now on."));
		break;
	case Phase::Problem:
		headline->setText(tr("The APO does not work on %n device(s).", nullptr, failed));
		hint->setText(tr("The other devices are not affected."));
		break;
	case Phase::Stopped:
		headline->setText(tr("The check did not finish."));
		hint->setText(QString());
		break;
	}
	hint->setVisible(!hint->text().isEmpty());

	const bool running = phase == Phase::Running;
	stopButton->setVisible(running);
	closeButton->setVisible(!running);
	recordHeader->setChecked(false);
	record->setVisible(false);
}

void DeviceTestView::loadPreview(Preview preview)
{
	auto threadText = [](const char* source) {
		return QCoreApplication::translate("DeviceTestThread", source);
	};
	const QString alive = tr("The APO works");
	const QString waiting = tr("Checking");

	QVector<Row> rows;
	QVector<RecordLine> lines;
	lines.append({threadText("Restarting audio service..."), false});
	lines.append({threadText("Checking APO installation..."), false});

	switch (preview)
	{
	case Preview::Running:
		rows.append({QStringLiteral("Speakers"), QStringLiteral("TOPPING USB DAC"), alive, DeviceReport::Alive, false, true});
		rows.append({QStringLiteral("CABLE Input"), QStringLiteral("VB-Audio Virtual Cable"),
			tr("Pre-mix works. Checking post-mix"), DeviceReport::Waiting, false, false});
		rows.append({QStringLiteral("Headphones"), QStringLiteral("Realtek(R) Audio"), waiting, DeviceReport::Waiting, false, false});
		rows.append({QStringLiteral("Microphone"), QStringLiteral("USB Audio Device"), waiting, DeviceReport::Waiting, true, true});
		setRows(rows);
		setRecord(lines);
		setPhase(Phase::Running, 1, 4, 0);
		break;
	case Preview::Passed:
		rows.append({QStringLiteral("Speakers"), QStringLiteral("TOPPING USB DAC"), alive, DeviceReport::Alive, false, true});
		rows.append({QStringLiteral("CABLE Input"), QStringLiteral("VB-Audio Virtual Cable"), alive, DeviceReport::Alive, false, false});
		rows.append({QStringLiteral("Headphones"), QStringLiteral("Realtek(R) Audio"), alive, DeviceReport::Alive, false, false});
		rows.append({QStringLiteral("Microphone"), QStringLiteral("USB Audio Device"), alive, DeviceReport::Alive, true, true});
		lines.append({threadText("Checks done. No problems were detected."), false});
		setRows(rows);
		setRecord(lines);
		setPhase(Phase::Passed, 4, 4, 0);
		break;
	case Preview::Problem:
		rows.append({QStringLiteral("Speakers"), QStringLiteral("TOPPING USB DAC"), alive, DeviceReport::Alive, false, true});
		rows.append({QStringLiteral("CABLE Input"), QStringLiteral("VB-Audio Virtual Cable"),
			describe(Problem::OriginalApoFailed).row, DeviceReport::Degraded, false, false});
		rows.append({QStringLiteral("Headphones"), QStringLiteral("Realtek(R) Audio"),
			describe(Problem::NoAnswerEveryMode).row, DeviceReport::Dead, false, false});
		rows.append({QStringLiteral("Digital Output"), QStringLiteral("NVIDIA High Definition Audio"),
			describe(Problem::DeviceUnplugged).row, DeviceReport::Untestable, false, false});
		rows.append({QStringLiteral("Microphone"), QStringLiteral("USB Audio Device"), alive, DeviceReport::Alive, true, true});
		lines.append({threadText("Trying other configurations..."), false});
		lines.append({threadText("Restarting audio service..."), false});
		lines.append({threadText("Checking APO installation..."), false});
		lines.append({QCoreApplication::translate("DeviceTestThread", "Check failed for %n device(s).", nullptr, 1), true});
		lines.append({QCoreApplication::translate("DeviceTestThread", "Checks done. Problems were detected for %n device(s).", nullptr, 1), true});
		setRows(rows);
		setRecord(lines);
		setPhase(Phase::Problem, 5, 5, 1);
		setNotes({{QStringLiteral("Headphones"), describe(Problem::NoAnswerEveryMode), QString()},
			{QStringLiteral("CABLE Input"), describe(Problem::OriginalApoFailed), QString()},
			{QStringLiteral("Digital Output"), describe(Problem::DeviceUnplugged), QString()}});
		break;
	case Preview::Stopped:
	{
		const QString unchecked = tr("Not checked");
		rows.append({QStringLiteral("Speakers"), QStringLiteral("TOPPING USB DAC"), unchecked, DeviceReport::Unchecked, false, true});
		rows.append({QStringLiteral("CABLE Input"), QStringLiteral("VB-Audio Virtual Cable"), unchecked, DeviceReport::Unchecked, false, false});
		rows.append({QStringLiteral("Headphones"), QStringLiteral("Realtek(R) Audio"), unchecked, DeviceReport::Unchecked, false, false});
		lines.clear();
		lines.append({threadText("Restarting audio service..."), false});
		lines.append({threadText("Restart failed."), true});
		setRows(rows);
		setRecord(lines);
		setPhase(Phase::Stopped, 0, 3, 0);
		setNotes({{QString(), describe(Problem::AudioRestartFailed),
			QStringLiteral("Error while stopping service \"audiosrv\": Access is denied. (5)")}});
		break;
	}
	}
}
