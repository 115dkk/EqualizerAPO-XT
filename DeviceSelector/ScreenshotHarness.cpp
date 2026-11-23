/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "ScreenshotHarness.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QToolTip>

#include "DeviceCaption.h"
#include "DeviceSelector.h"
#include "DeviceTestDialog.h"
#include "DeviceTestView.h"
#include "PreviewDevices.h"
#include "skins/DeviceSkinPainter.h"
#include "Editor/skins/SkinThemeData.h"

namespace
{
// The tooltip of one of the troubleshooting options, saved on its own (it is
// wider than the dialog). A tooltip label matches only the sheets' QToolTip
// selectors, never their QWidget rule, so while the QToolTip rules named no
// font the label kept the system message face (Malgun Gothic 9pt on a
// Korean Windows) beside a skinned dialog. Fails when the label's first face
// is not one of the skin's.
bool saveTooltipShot(QDialog& dialog, const QString& file)
{
	QCheckBox* option = dialog.findChild<QCheckBox*>(QStringLiteral("parallelConvolutionCheckBox"));
	if (option == nullptr)
	{
		fprintf(stderr, "DeviceSelector shots: no option to show a tooltip for\n");
		return false;
	}
	QToolTip::showText(option->mapToGlobal(QPoint(option->width() / 3, option->height() / 2)), option->toolTip(), option);
	// The label exists at once; with the desktop's tooltip fade on it only
	// turns visible a moment later, and grab() does not need it visible.
	QWidget* tip = nullptr;
	QElapsedTimer waited;
	waited.start();
	while (tip == nullptr && waited.elapsed() < 2000)
	{
		QApplication::processEvents(QEventLoop::AllEvents, 50);
		for (QWidget* widget : QApplication::topLevelWidgets())
		{
			if (widget->inherits("QTipLabel"))
				tip = widget;
		}
	}
	bool saved = false;
	if (tip == nullptr)
		fprintf(stderr, "DeviceSelector shots: the tooltip did not open\n");
	else
	{
		const SkinTokens& tokens = DeviceSkinPainter::activeTokens();
		const QString face = tip->font().families().value(0);
		if (face != tokens.fontFamily && face != tokens.monoFontFamily)
			fprintf(stderr, "DeviceSelector shots: the tooltip is set in \"%s\", not in the skin's face\n", qPrintable(face));
		else if (!tip->grab().save(file))
			fprintf(stderr, "DeviceSelector shots: failed to save %s\n", qPrintable(file));
		else
			saved = true;
	}
	QToolTip::hideText();
	QApplication::processEvents();
	return saved;
}
}

// DeviceSelector's shipped manifest requires elevation. Refusing elevated
// captures would disable its existing --skin-shots entry (audit #467 TD-75).
// --skin-shots <outDir>: renders the dialog with canned devices for every
// skin x dark/light in three states (rest, hovered row, troubleshooting
// open) on the offscreen platform, each window with the skin's caption, plus
// an option's tooltip. The review gate's capture source and the
// skin work's regression harness - no registry writes, no COM. Renders in
// the user's language (translators install before the harness runs), so
// byte-comparison only holds for a fixed language setting.
int ScreenshotHarness::runSkinShots(QApplication& app)
{
	const QStringList args = app.arguments();
	const int flagIndex = args.indexOf(QStringLiteral("--skin-shots"));
	if (flagIndex < 0 || flagIndex + 1 >= args.size())
	{
		fprintf(stderr, "usage: DeviceSelector --skin-shots <outDir>\n");
		return 2;
	}
	QDir outDir(args[flagIndex + 1]);
	if (!outDir.exists() && !QDir().mkpath(outDir.absolutePath()))
	{
		fprintf(stderr, "DeviceSelector shots: cannot create %s\n", qPrintable(outDir.absolutePath()));
		return 2;
	}

	// The captions the live dialog mounts unless the Editor keeps the native
	// one; the shots show what a skinned session sees.
	DeviceCaption::enable(true);

	int failures = 0;
	// From the roster, so a new skin appears in the review captures without
	// anybody remembering this list.
	const QStringList skins = SkinThemeData::ids();
	for (const QString& skinId : skins)
	{
		for (int darkIndex = 0; darkIndex < 2; darkIndex++)
		{
			const bool dark = darkIndex == 0;
			SkinThemeData::applyToApplication(app, skinId, dark);
			DeviceSkinPainter::setActiveTheme(skinId, dark);

			DeviceSelector dialog(PreviewDevices::playback(), PreviewDevices::capture());
			// The size the dialog would open at, before the harness pins its
			// own: the one number the "opens too narrow" report is about.
			if (skinId == skins.first() && !dark)
				fprintf(stderr, "DeviceSelector shots: initial size %dx%d\n", dialog.width(), dialog.height());
			dialog.resize(760, 700);
			dialog.show();
			QApplication::processEvents();
			// One pending install so the will-install state shows.
			dialog.previewCheckDevice(0, 1);
			QApplication::processEvents();

			const QString mode = dark ? QStringLiteral("dark") : QStringLiteral("light");
			auto save = [&](const QString& state) {
				const QString file = outDir.filePath(
					QStringLiteral("devsel_%1_%2_%3.png").arg(skinId, mode, state));
				if (!dialog.grab().save(file))
				{
					fprintf(stderr, "DeviceSelector shots: failed to save %s\n", qPrintable(file));
					failures++;
				}
			};

			save(QStringLiteral("normal"));
			dialog.previewHoverDevice(0, 2);
			QApplication::processEvents();
			save(QStringLiteral("hover"));
			dialog.previewSelectDevice(0, 0);
			dialog.previewOpenTroubleshooting();
			QApplication::processEvents();
			save(QStringLiteral("options"));
			if (!saveTooltipShot(dialog, outDir.filePath(QStringLiteral("devsel_%1_%2_tooltip.png").arg(skinId, mode))))
				failures++;
			// The endpoint's ASIO entry ticked: the same ASIO options unfold
			// under it.
			dialog.previewAsioEntry();
			QApplication::processEvents();
			save(QStringLiteral("asioentry"));
			// The ASIO target's own options page (the installed ASIO row of
			// the preview roster), for the judging material.
			dialog.previewSelectDevice(0, 4);
			QApplication::processEvents();
			save(QStringLiteral("asio"));
			// Buffer removal ticked: the wait time unfolds beside it.
			dialog.previewRemoveBuffer();
			QApplication::processEvents();
			save(QStringLiteral("asiowait"));
			// Back on the endpoint with "Send" ticked: the receive resampling
			// option unfolds to its right.
			dialog.previewSelectDevice(0, 0);
			QApplication::processEvents();
			dialog.previewReceive();
			QApplication::processEvents();
			save(QStringLiteral("receive"));

			// The installation check in its four states.
			const std::pair<DeviceTestView::Preview, const char*> previews[] = {
				{DeviceTestView::Preview::Running, "running"},
				{DeviceTestView::Preview::Passed, "passed"},
				{DeviceTestView::Preview::Problem, "problem"},
				{DeviceTestView::Preview::Stopped, "stopped"}};
			for (const auto& preview : previews)
			{
				DeviceTestView view;
				view.loadPreview(preview.first);
				view.resize(620, 760);
				view.show();
				QApplication::processEvents();
				QApplication::processEvents();
				const QString file = outDir.filePath(
					QStringLiteral("devtest_%1_%2_%3.png").arg(skinId, mode, QLatin1String(preview.second)));
				if (!view.grab().save(file))
					failures++;
			}

			// The live dialog with nothing to check: two changes the install
			// step could not make. No thread runs and nothing is written.
			{
				DeviceTestDialog::InstallFailure refused;
				refused.guid = QStringLiteral("{preview-refused}");
				refused.connection = QStringLiteral("Headphones");
				refused.device = QStringLiteral("Realtek(R) Audio");
				refused.message = QStringLiteral("Error while writing registry value FxProperties: Access is denied. (5)");
				DeviceTestDialog::InstallFailure partial = refused;
				partial.guid = QStringLiteral("{preview-partial}");
				partial.connection = QStringLiteral("Microphone");
				partial.device = QStringLiteral("USB Audio Device");
				partial.input = true;
				partial.leftPartial = true;
				DeviceTestDialog dialog({refused, partial}, {}, false);
				dialog.resize(620, 760);
				dialog.show();
				QApplication::processEvents();
				QApplication::processEvents();
				if (!dialog.grab().save(outDir.filePath(QStringLiteral("devtest_%1_%2_refused.png").arg(skinId, mode))))
					failures++;
			}
		}
	}

	fprintf(stderr, "DeviceSelector shots: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
