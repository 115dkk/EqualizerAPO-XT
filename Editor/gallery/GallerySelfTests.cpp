/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	The card self tests: --selftest-vst (store/parse round trip and the
	channel-fill menus), --routing-edit-test, --scroll-bench and
	--power-toggle-test.
*/

#include "SkinGallery.h"
#include "Editor/gallery/GallerySupport.h"
#include <numbers>
// For the two gates moved out of main.cpp (audit #275 B7): the VST
// round-trip self test and the analysis layout probe.
#include <optional>
#include <unordered_map>
#include <QBoxLayout>
#include <QDockWidget>
#include <QFileInfo>
#include <QLocale>
#include <QStyle>
#include <QTimer>
#include "filters/VSTPluginFilter.h"
#include "filters/VSTPluginFilterFactory.h"
#include "guis/VSTPluginFilterGUI.h"
#include "widgets/cards/VSTCardEditor.h"
#include "widgets/cards/VSTSlotFillRail.h"
#include "MainWindow.h"
#include "diagnostics/ToolbarPixelProbe.h"
#include "widgets/MainToolbarKit.h"
#include "SubwooferRouting/Preset.h"
#include "SubwooferRouting/StateCodec.h"
#include "widgets/subwooferrouting/SubwooferRoutingDefaults.h"

#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPixmap>
#include <QPointer>
#include <QRadioButton>
#include <QToolButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QSpinBox>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QTranslator>
#include <QTreeView>
#include <QUrl>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "Editor/FilterTable.h"
#include "Editor/SkinManager.h"
#include "Editor/guis/CopyFilterGUI.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/skins/ISkin.h"
#include "Editor/skins/Skins.h"
#include "Editor/widgets/AddCardRow.h"
#include "Editor/widgets/EqGraphView.h"
#include "Editor/widgets/SegmentedControl.h"
#include "Editor/analysis/AnalysisMetric.h"
#include "Editor/analysis/AnalysisResponse.h"
#include "filters/BiQuad.h"
#include "Editor/skins/shared/SkinFileIcons.h"
#include "Editor/widgets/FilterCardRow.h"
#include "Editor/widgets/CommandRowFrame.h"
#include "Editor/widgets/FilterInsertSeam.h"
#include "Editor/widgets/FilterPickerView.h"
#include "Editor/widgets/SkinComboBox.h"
#include "Editor/widgets/TitleBar.h"
#include "Editor/widgets/UpdateToast.h"
#include "Editor/widgets/subwooferrouting/SubwooferRoutingEditorDialog.h"
#include "Editor/widgets/cards/SubwooferRoutingCardEditor.h"
#include "Editor/widgets/routing/IRoutingRenderer.h"

using namespace SkinGalleryDetail;

// Mechanical round-trip check for VST plugin data: parse a VSTPlugin line, feed
// the parsed library, opaque state and options into the real VSTPluginFilterGUI,
// call its store(), reparse the result and confirm ChunkData, parameters,
// StereoInput and the hidden Input/Output contract survive. Returns 0 on success,
// 1 on any loss.
int SkinGallery::runVstRoundTripSelfTest()
{
	struct Case { const wchar_t* name = nullptr; std::wstring params; };
	const Case cases[] = {
		{ L"chunkData", L"Library \"fake plugin.dll\" ChunkData \"QUJDREVGR0g=\"" },
		{ L"paramMap", L"Library fake.dll Gain 0.5 Mix 0.25 Width 1" },
		{ L"paramMap-quoted-name", L"Library fake.dll \"Dry/Wet\" 0.75 Output 0.5" },
		{ L"stereoInput-chunk", L"Library fake.dll StereoInput 1 ChunkData \"QUJDREVGR0g=\"" },
		{ L"stereoInput-params", L"Library fake.dll StereoInput 1 Gain 0.5" },
		{ L"busContract", L"Library fake.vst3 Input Stereo Output 7.1 Gain 0.5" },
		{ L"busContract-slotFill",
		  L"Library fake.vst3 Input 5.1 InputChannels L,R,C,-,SL,SR Output 5.1 OutputChannels L,R,C,LFE,RL,RR Gain 0.5" }
	};

	int failures = 0;
	for (const Case& c : cases)
	{
		VSTPluginFilterFactory factory;
		std::wstring command = L"VSTPlugin";
		std::wstring params = c.params;
		FilterVector filters = factory.createFilter(L"", command, params);
		if (filters.empty())
		{
			fprintf(stderr, "[VST selftest] %ls: parse produced no filter\n", c.name);
			failures++;
			continue;
		}
		VSTPluginFilter* f0 = static_cast<VSTPluginFilter*>(filters[0].get());
		std::wstring chunk0 = f0->getChunkData();
		std::unordered_map<std::wstring, float> map0 = f0->getParamMap();
		const bool stereo0 = f0->getStereoInput();
		const std::optional<VST3BusContract> bus0 = f0->getBusContract();
		const std::vector<std::wstring> fillIn0 = f0->getInputChannels();
		const std::vector<std::wstring> fillOut0 = f0->getOutputChannels();

		VSTPluginFilterGUI gui(f0->getLibrary(), chunk0, map0, stereo0, bus0, fillIn0, fillOut0);
		QString outCommand, outParams;
		gui.store(outCommand, outParams);

		std::wstring command2 = outCommand.toStdWString();
		std::wstring params2 = outParams.toStdWString();
		FilterVector filters2 = factory.createFilter(L"", command2, params2);
		if (filters2.empty())
		{
			fprintf(stderr, "[VST selftest] %ls: re-parse produced no filter (params='%ls')\n", c.name, params2.c_str());
			failures++;
			continue;
		}
		VSTPluginFilter* f1 = static_cast<VSTPluginFilter*>(filters2[0].get());
		std::wstring chunk1 = f1->getChunkData();
		std::unordered_map<std::wstring, float> map1 = f1->getParamMap();
		const bool stereo1 = f1->getStereoInput();
		const std::optional<VST3BusContract> bus1 = f1->getBusContract();
		const bool sameBusContract = bus0.has_value() == bus1.has_value()
			&& (!bus0 || (bus0->input == bus1->input && bus0->output == bus1->output));
		const bool sameSlotFill = fillIn0 == f1->getInputChannels() && fillOut0 == f1->getOutputChannels();
		bool ok = (chunk0 == chunk1) && (map0 == map1) && (stereo0 == stereo1) && sameBusContract && sameSlotFill;
		if (!ok)
		{
			failures++;
			fprintf(stderr, "[VST selftest] %ls: LOSS. chunk %ls->%ls, params %zu->%zu, stereoInput %d->%d, bus %d->%d, fill %zu/%zu->%zu/%zu\n",
				c.name, chunk0.c_str(), chunk1.c_str(), map0.size(), map1.size(), stereo0 ? 1 : 0,
				stereo1 ? 1 : 0, bus0 ? 1 : 0, bus1 ? 1 : 0,
				fillIn0.size(), fillOut0.size(), f1->getInputChannels().size(), f1->getOutputChannels().size());
			for (auto& kv : map0)
			{
				auto it = map1.find(kv.first);
				if (it == map1.end())
					fprintf(stderr, "    dropped param '%ls'=%g\n", kv.first.c_str(), kv.second);
				else if (it->second != kv.second)
					fprintf(stderr, "    param '%ls' %g -> %g\n", kv.first.c_str(), kv.second, it->second);
			}
		}
		else
		{
			fprintf(stderr, "[VST selftest] %ls: OK (chunk len %zu, %zu params preserved)\n",
				c.name, chunk0.size(), map0.size());
		}
	}

	fprintf(stderr, "[VST selftest] %s (%d failure(s))\n", failures == 0 ? "PASS" : "FAIL", failures);
	return failures == 0 ? 0 : 1;
}

int SkinGallery::runVstFillSelfTest()
{
	// A 7.1 endpoint (the layout Windows offers for the field setup); the
	// row itself negotiates Stereo in / 5.1 out like a stereo upmixer. Each
	// case lists the rows above the VST row and the channels its fill menus
	// must offer: the device set, a Channel row's narrowed selection, and
	// the device set again when that Channel row is powered off (the engine
	// skips commented lines).
	struct Case { const char* name = nullptr; QList<QString> rowsAbove; QStringList expected; };
	const QStringList deviceChannels = {
		QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("C"), QStringLiteral("LFE"),
		QStringLiteral("RL"), QStringLiteral("RR"), QStringLiteral("SL"), QStringLiteral("SR")};
	const Case cases[] = {
		{ "device", {}, deviceChannels },
		{ "channel-narrowed", { QStringLiteral("Channel: L R") },
		  { QStringLiteral("L"), QStringLiteral("R") } },
		{ "channel-commented", { QStringLiteral("# Channel: L R") }, deviceChannels }
	};
	const QString vstLine = QStringLiteral("VSTPlugin: Library example.vst3 Input Stereo Output 5.1");

	int failures = 0;
	for (const Case& c : cases)
	{
		for (int modeIndex = 0; modeIndex < 2; modeIndex++)
		{
			const bool legacy = modeIndex == 1;
			const QByteArray label = QByteArray(c.name) + (legacy ? "/legacy" : "/cards");
			QScrollArea scrollArea;
			scrollArea.setWidgetResizable(true);
			scrollArea.resize(960, 720);
			FilterTable* table = new FilterTable();
			table->setRenderMode(legacy ? FilterTable::LegacyRows : FilterTable::ModernCards);
			scrollArea.setWidget(table);
			QList<std::shared_ptr<AbstractAPOInfo>> outputDevices, inputDevices;
			galleryDevices(outputDevices, inputDevices);
			std::shared_ptr<AbstractAPOInfo> device =
				std::make_shared<GalleryAPOInfo>(L"Speakers", L"Example Audio", false, true, 8, 0x63F);
			table->updateDeviceAndChannelMask(device, 0x63F);
			table->initialize(&scrollArea, outputDevices, inputDevices);
			QList<QString> lines = c.rowsAbove;
			lines.append(vstLine);
			table->setLines(QString(), lines);
			table->updateGuis();
			scrollArea.show();
			QApplication::processEvents();

			// Both presentations report the menu of every slot; the cards
			// path through the rail cells, the legacy row through its combos
			// after the fold latch is opened (a defaulted fill starts folded,
			// exactly as the field report found it).
			QList<QStringList> menus;
			if (legacy)
			{
				VSTPluginFilterGUI* gui = table->findChild<VSTPluginFilterGUI*>();
				if (gui == nullptr)
				{
					fprintf(stderr, "[VST fill selftest] %s: no VSTPluginFilterGUI row\n", label.constData());
					failures++;
					continue;
				}
				QCheckBox* latch = gui->findChild<QCheckBox*>();
				if (latch != nullptr && !latch->isChecked())
					latch->setChecked(true);
				QApplication::processEvents();
				for (QComboBox* combo : gui->findChildren<QComboBox*>())
				{
					if (!combo->objectName().isEmpty())
						continue;
					QStringList items;
					for (int i = 0; i < combo->count(); i++)
						items.append(combo->itemData(i).toString());
					menus.append(items);
				}
			}
			else
			{
				for (const VSTSlotFillCell* cell : table->findChildren<VSTSlotFillCell*>())
					menus.append(cell->channelChoices());
			}

			// Stereo in + 5.1 out: two input slots and six output slots.
			if (menus.size() != 8)
			{
				fprintf(stderr, "[VST fill selftest] %s: expected 8 slot menus, found %d\n",
					label.constData(), int(menus.size()));
				failures++;
				continue;
			}
			bool ok = true;
			for (const QStringList& menu : menus)
			{
				QStringList channels = menu;
				channels.removeAll(QStringLiteral("-"));
				if (channels != c.expected)
					ok = false;
			}
			if (!ok)
			{
				failures++;
				fprintf(stderr, "[VST fill selftest] %s: expected %s, menus offer:\n", label.constData(),
					c.expected.join(QLatin1Char(',')).toUtf8().constData());
				for (const QStringList& menu : menus)
					fprintf(stderr, "    %s\n", menu.join(QLatin1Char(',')).toUtf8().constData());
			}
			else
			{
				fprintf(stderr, "[VST fill selftest] %s: OK (%s)\n", label.constData(),
					c.expected.join(QLatin1Char(',')).toUtf8().constData());
			}
		}
	}
	fprintf(stderr, "[VST fill selftest] %s (%d failure(s))\n", failures == 0 ? "PASS" : "FAIL", failures);
	return failures == 0 ? 0 : 1;
}

int SkinGallery::runRoutingEditTest()
{
	qWarning("RoutingEditTest: starting");
	QTemporaryDir scratch;
	if (!scratch.isValid())
	{
		qWarning("RoutingEditTest: cannot create a scratch directory");
		return 2;
	}
	qputenv("EAPO_SKIN_GALLERY", "1");
	const QString configPath = buildReferenceFiles(QDir(scratch.path()));
	if (configPath.isEmpty())
	{
		qWarning("RoutingEditTest: cannot write reference target files");
		return 2;
	}

	int failures = 0;
	const auto settle = []() {
		QApplication::processEvents();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QApplication::processEvents();
	};
	const auto check = [&failures](bool ok, const QString& what) {
		if (!ok)
		{
			qWarning("RoutingEditTest: %s", qPrintable(what));
			failures++;
		}
		return ok;
	};
	const auto lineText = [](QScrollArea& scrollArea, int row) {
		const FilterTable* table = qobject_cast<FilterTable*>(scrollArea.widget());
		return table != nullptr && row < table->documentItems().size()
			? table->documentItems().at(row)->text.trimmed() : QString();
	};
	const auto liveView = [](FilterCardRow* row) -> RoutingView* {
		// MultiConvolution rebuilds its view after file metadata arrives and
		// hides the superseded one until deleteLater lands.
		RoutingView* view = nullptr;
		for (RoutingView* candidate : row->findChildren<RoutingView*>())
			if (candidate->isVisible())
				view = candidate;
		return view;
	};
	const auto pressEnter = [](QWidget* editor) {
		QKeyEvent enterPress(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
		QApplication::sendEvent(editor, &enterPress);
	};
	// The source editor's own accounting: text plus the sheet's padding and
	// hairline must fit the field, or the token is unreadable.
	const auto editorFits = [](QLineEdit* editor) {
		return editor->fontMetrics().horizontalAdvance(editor->text()) + 14 <= editor->width()
			&& editor->height() >= editor->fontMetrics().height() + 4;
	};
	// Retype the open source editor's token and commit it with Enter; the
	// editor must be the focus widget the view just opened.
	const auto retype = [&](RoutingView* view, const QString& target, const QString& token) -> QLineEdit* {
		view->grab();
		view->galleryShowcase(QStringLiteral("editSource:") + target);
		QApplication::processEvents();
		QLineEdit* editor = view->findChild<QLineEdit*>(QStringLiteral("StepSourceEditor"));
		if (editor == nullptr || !editor->isVisible())
			return nullptr;
		editor->setText(token);
		pressEnter(editor);
		settle();
		return editor;
	};

	auto stereo = std::make_shared<GalleryAPOInfo>(
		L"Speakers", L"Example Audio", false, true, 2, 0x3);

	// Part 1: the minimal step list over an empty Copy on a stereo endpoint.
	{
		SkinManager::instance()->applySkin(QStringLiteral("minimal"), true);
		QScrollArea scrollArea;
		scrollArea.resize(960, 720);
		QList<FilterCardRow*> rows = buildRows(scrollArea, configPath,
			{ QStringLiteral("Copy:") }, stereo, 0x3);
		RoutingView* view = rows.size() == 1 ? rows[0]->findChild<RoutingView*>() : nullptr;
		if (!check(view != nullptr, QStringLiteral("minimal Copy row has no routing view")))
		{
			failures += 10;
		}
		else
		{
			// Two virtual channels typed at the prompt, the way an upmix is
			// started: they must be offered as sources from then on.
			for (const QString& name : { QStringLiteral("VL"), QStringLiteral("VR") })
			{
				view->galleryShowcase(QStringLiteral("addChannel"));
				QApplication::processEvents();
				QLineEdit* prompt = qobject_cast<QLineEdit*>(QApplication::focusWidget());
				if (!check(prompt != nullptr, QStringLiteral("the add-channel prompt did not take focus")))
					break;
				check(editorFits(prompt) || prompt->text().isEmpty(),
					QStringLiteral("the add-channel prompt clips its text"));
				prompt->setText(name);
				check(editorFits(prompt), QStringLiteral("the add-channel prompt clips '%1'").arg(name));
				pressEnter(prompt);
				settle();
			}
			const QStringList before = view->sourceCandidates(QStringLiteral("L"));
			check(before == QStringList({ QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("VL"), QStringLiteral("VR") }),
				QStringLiteral("first source hint for L is '%1' (expected L R VL VR)").arg(before.join(' ')));

			check(view->connectSource(QStringLiteral("L"), QStringLiteral("L")),
				QStringLiteral("connecting L to L was refused"));
			settle();
			check(lineText(scrollArea, 0) == QStringLiteral("Copy: L=L"),
				QStringLiteral("after L=L the line is '%1'").arg(lineText(scrollArea, 0)));
			check(rows[0]->findChild<RoutingView*>() == view,
				QStringLiteral("the Copy row rebuilt its routing view on an edit"));
			// The report: from the second source on, the hint shrank to R.
			const QStringList after = view->sourceCandidates(QStringLiteral("L"));
			check(after == QStringList({ QStringLiteral("R"), QStringLiteral("VL"), QStringLiteral("VR") }),
				QStringLiteral("second source hint for L is '%1' (expected R VL VR)").arg(after.join(' ')));

			// The source editor: readable, and the token grammar of the line.
			view->grab();
			view->galleryShowcase(QStringLiteral("editSource:L"));
			QApplication::processEvents();
			QLineEdit* editor = view->findChild<QLineEdit*>(QStringLiteral("StepSourceEditor"));
			if (check(editor != nullptr && editor->isVisible(), QStringLiteral("the source editor did not open")))
			{
				check(editor->text() == QStringLiteral("L"),
					QStringLiteral("the source editor holds '%1' (expected L)").arg(editor->text()));
				check(editorFits(editor),
					QStringLiteral("the source editor (%1x%2) clips '%3'")
					.arg(editor->width()).arg(editor->height()).arg(editor->text()));
				editor->setText(QStringLiteral("-0.000dB*LFE"));
				check(editorFits(editor),
					QStringLiteral("the source editor (%1 px) clips a full token").arg(editor->width()));
				editor->setText(QStringLiteral("0.5*R"));
				pressEnter(editor);
				settle();
				check(lineText(scrollArea, 0) == QStringLiteral("Copy: L=0.5*R"),
					QStringLiteral("after retyping 0.5*R the line is '%1'").arg(lineText(scrollArea, 0)));
			}
			if (retype(view, QStringLiteral("L"), QStringLiteral("INV")) != nullptr)
				check(lineText(scrollArea, 0) == QStringLiteral("Copy: L=-1.0*R"),
					QStringLiteral("a bare INV keeps the channel: line is '%1'").arg(lineText(scrollArea, 0)));
			else
				check(false, QStringLiteral("the source editor did not reopen for INV"));
			if (retype(view, QStringLiteral("L"), QStringLiteral("VL")) != nullptr)
				check(lineText(scrollArea, 0) == QStringLiteral("Copy: L=VL"),
					QStringLiteral("a bare channel is unity: line is '%1'").arg(lineText(scrollArea, 0)));
			else
				check(false, QStringLiteral("the source editor did not reopen for VL"));
			if (retype(view, QStringLiteral("L"), QStringLiteral("L+R")) != nullptr)
				check(lineText(scrollArea, 0) == QStringLiteral("Copy: L=VL"),
					QStringLiteral("an unreadable token must leave the line: it is '%1'").arg(lineText(scrollArea, 0)));
			else
				check(false, QStringLiteral("the source editor did not reopen for L+R"));
			if (retype(view, QStringLiteral("L"), QString()) != nullptr)
				check(lineText(scrollArea, 0) == QStringLiteral("Copy:"),
					QStringLiteral("an emptied token removes the source: line is '%1'").arg(lineText(scrollArea, 0)));
			else
				check(false, QStringLiteral("the source editor did not reopen for removal"));
		}
	}

	// Part 2: MultiConvolution over the 4-channel BRIR - a port retyped in
	// the same editor is the port, not a gain.
	{
		SkinManager::instance()->applySkin(QStringLiteral("minimal"), true);
		QScrollArea scrollArea;
		scrollArea.resize(960, 720);
		QList<FilterCardRow*> rows = buildRows(scrollArea, configPath,
			{ QStringLiteral("MultiConvolution: L=0 R=0 brir.wav") }, stereo, 0x3);
		RoutingView* view = rows.size() == 1 ? liveView(rows[0]) : nullptr;
		if (!check(view != nullptr, QStringLiteral("minimal MultiConvolution row has no routing view")))
		{
			failures += 4;
		}
		else
		{
			const QStringList hint = view->sourceCandidates(QStringLiteral("R"));
			check(hint == QStringList({ QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3") }),
				QStringLiteral("MultiConvolution source hint for R is '%1' (expected 1 2 3)").arg(hint.join(' ')));
			view->grab();
			view->galleryShowcase(QStringLiteral("editSource:R"));
			QApplication::processEvents();
			QLineEdit* editor = view->findChild<QLineEdit*>(QStringLiteral("StepSourceEditor"));
			if (check(editor != nullptr && editor->isVisible(), QStringLiteral("the MultiConvolution source editor did not open")))
			{
				check(editor->text() == QStringLiteral("0"),
					QStringLiteral("the MultiConvolution source editor holds '%1' (expected 0)").arg(editor->text()));
				check(editorFits(editor),
					QStringLiteral("the MultiConvolution source editor (%1x%2) clips '%3'")
					.arg(editor->width()).arg(editor->height()).arg(editor->text()));
				editor->setText(QStringLiteral("1"));
				pressEnter(editor);
				settle();
				check(lineText(scrollArea, 0) == QStringLiteral("MultiConvolution: L=0 R=1 brir.wav"),
					QStringLiteral("after retyping port 1 the line is '%1'").arg(lineText(scrollArea, 0)));
			}
		}
	}

	// Shared routing commit contract, through each renderer's real editor:
	// rejected input must not notify the host, not just retain the bytes.
	for (ISkin* skin : Skins::all())
	{
		const QString name = skin->id();
		SkinManager::instance()->applySkin(name, true);
		QScrollArea scrollArea;
		scrollArea.resize(960, 720);
		QList<FilterCardRow*> rows = buildRows(scrollArea, configPath,
			{ QStringLiteral("Copy: VC=0.5*L") }, stereo, 0x3);
		RoutingView* view = rows.size() == 1 ? liveView(rows[0]) : nullptr;
		if (!check(view != nullptr, QStringLiteral("%1: routing commit view missing").arg(name)))
			continue;

		int notifications = 0;
		const QMetaObject::Connection connection = QObject::connect(view, &RoutingView::routingChanged,
			view, [&notifications]() { notifications++; });
		const auto commit = [&](const QString& text) {
			view->grab();
			QLineEdit* editor = nullptr;
			// Hit rectangles belong to the skin. Find its real double-click
			// target without duplicating that geometry in the gate or moving
			// the desktop pointer. No preceding press toggles a grid cell.
			for (int y = 2; y < view->height() && editor == nullptr; y += 4)
				for (int x = 2; x < view->width() && editor == nullptr; x += 4)
				{
					const QPointF local(x, y);
					QMouseEvent event(QEvent::MouseButtonDblClick, local,
						QPointF(view->mapToGlobal(local.toPoint())), Qt::LeftButton,
						Qt::LeftButton, Qt::NoModifier);
					QApplication::sendEvent(view, &event);
					for (QLineEdit* candidate : view->findChildren<QLineEdit*>())
						if (candidate->isVisible())
							editor = candidate;
				}
			if (!check(editor != nullptr, QStringLiteral("%1: routing editor did not open").arg(name)))
				return;
			editor->setText(text);
			pressEnter(editor);
			settle();
		};
		for (const QString& invalid : { QStringLiteral("inf"), QStringLiteral("bad*L") })
		{
			const QString before = lineText(scrollArea, 0);
			// A bare word is a channel in the step-list grammar, so qualify
			// infinity as a factor there rather than testing a valid name.
			commit(name == QLatin1String("minimal") && invalid == QLatin1String("inf")
				? QStringLiteral("inf*L") : invalid);
			check(notifications == 0, QStringLiteral("%1: rejected routing text emitted a change").arg(name));
			check(lineText(scrollArea, 0) == before,
				QStringLiteral("%1: rejected routing text changed the line").arg(name));
		}
		commit(QStringLiteral("INV"));
		check(notifications == 1, QStringLiteral("%1: INV did not emit exactly one change").arg(name));
		check(lineText(scrollArea, 0) == QStringLiteral("Copy: VC=-1.0*L"),
			QStringLiteral("%1: INV did not invert the source").arg(name));
		if (name == QLatin1String("soft") || name == QLatin1String("minimal"))
		{
			commit(QStringLiteral("L"));
			check(lineText(scrollArea, 0) == QStringLiteral("Copy: VC=L"),
				QStringLiteral("%1: re-entering a source did not restore unity").arg(name));
		}
		const int beforeAdd = notifications;
		const QString lineBeforeAdd = lineText(scrollArea, 0);
		for (const QString& channel : { QStringLiteral("NewBus"), QStringLiteral("newbus"), QStringLiteral("VC") })
		{
			view->galleryShowcase(QStringLiteral("addChannel"));
			QApplication::processEvents();
			QLineEdit* editor = qobject_cast<QLineEdit*>(QApplication::focusWidget());
			if (!check(editor != nullptr, QStringLiteral("%1: channel editor missing").arg(name)))
				break;
			editor->setText(channel);
			pressEnter(editor);
			settle();
		}
		// Qt delivers routingChanged through the connected lambda during
		// pressEnter/settle; cppcheck cannot see that callback write.
		// cppcheck-suppress knownConditionTrueFalse
		check(notifications == beforeAdd && lineText(scrollArea, 0) == lineBeforeAdd,
			QStringLiteral("%1: empty channel additions changed the routing").arg(name));
		QObject::disconnect(connection);
	}

	// Part 3: ALL releases on the next pick, in every skin, on both cards.
	for (ISkin* skin : Skins::all())
	{
		const QString name = skin->id();
		SkinManager::instance()->applySkin(skin->id(), true);
		QScrollArea scrollArea;
		scrollArea.resize(960, 720);
		QList<FilterCardRow*> rows = buildRows(scrollArea, configPath,
			{ QStringLiteral("Channel: ALL"), QStringLiteral("Device: all") }, stereo, 0x3);
		if (!check(rows.size() == 2, QStringLiteral("%1: expected 2 rows").arg(name)))
			continue;

		QToolButton* seat = nullptr;
		const QToolButton* allChannels = nullptr;
		for (QToolButton* button : rows[0]->findChildren<QToolButton*>())
		{
			if (button->objectName() != QStringLiteral("ChannelChip"))
				continue;
			if (button->property("allChannels").toBool())
				allChannels = button;
			else if (button->text() == QStringLiteral("R"))
				seat = button;
		}
		if (check(seat != nullptr && allChannels != nullptr, QStringLiteral("%1: Channel card chips not found").arg(name)))
		{
			check(allChannels->isChecked(), QStringLiteral("%1: ALL is not checked for 'Channel: ALL'").arg(name));
			check(seat->isEnabled(), QStringLiteral("%1: the R chip is inert while ALL is on").arg(name));
			seat->click();
			settle();
			check(lineText(scrollArea, 0) == QStringLiteral("Channel: R"),
				QStringLiteral("%1: after picking R the line is '%2'").arg(name, lineText(scrollArea, 0)));
			check(!allChannels->isChecked(), QStringLiteral("%1: ALL stayed checked after the pick").arg(name));
			check(seat->isChecked(), QStringLiteral("%1: the picked R chip is not checked").arg(name));
		}

		QToolButton* device = nullptr;
		const QToolButton* allDevices = nullptr;
		for (QToolButton* button : rows[1]->findChildren<QToolButton*>())
		{
			if (button->objectName() != QStringLiteral("DeviceChip"))
				continue;
			if (button->property("allDevices").toBool())
				allDevices = button;
			else if (device == nullptr && button->isVisible())
				device = button;
		}
		if (check(device != nullptr && allDevices != nullptr, QStringLiteral("%1: Device card chips not found").arg(name)))
		{
			check(allDevices->isChecked(), QStringLiteral("%1: All devices is not checked for 'Device: all'").arg(name));
			check(device->isEnabled(), QStringLiteral("%1: the device chip is inert while all is on").arg(name));
			device->click();
			settle();
			const QString line = lineText(scrollArea, 1);
			check(line.startsWith(QStringLiteral("Device: ")) && line != QStringLiteral("Device: all") && line.size() > 8,
				QStringLiteral("%1: after picking a device the line is '%2'").arg(name, line));
			check(!allDevices->isChecked(), QStringLiteral("%1: All devices stayed checked after the pick").arg(name));
			check(device->isChecked(), QStringLiteral("%1: the picked device chip is not checked").arg(name));
		}
	}

	qWarning("RoutingEditTest: %s (%d failure(s))", failures == 0 ? "PASS" : "FAIL", failures);
	const int status = failures == 0 ? 0 : 1;
	std::fflush(nullptr);
	std::_Exit(status);
}

int SkinGallery::runScrollBench()
{
	qWarning("ScrollBench: starting");
	QTemporaryDir scratch;
	if (!scratch.isValid())
		return 2;
	qputenv("EAPO_SKIN_GALLERY", "1");
	const QString configPath = buildReferenceFiles(QDir(scratch.path()));
	if (configPath.isEmpty())
		return 2;

	QList<QString> lines;
	for (int repeat = 0; repeat < 6; repeat++)
		for (const GalleryRow& row : galleryRows())
			lines.append(row.line);

	for (ISkin* skin : Skins::all())
	{
		QScrollArea scrollArea;
		// A maximized QHD editor: the card column is ~2500 px wide.
		scrollArea.resize(2560, 1300);
		SkinManager::instance()->applySkin(skin->id(), true);
		buildRows(scrollArea, configPath, lines);
		FilterTable* table = qobject_cast<FilterTable*>(scrollArea.widget());
		if (table == nullptr)
			continue;
		table->openConfig(QString());
		QApplication::processEvents();

		QScrollBar* bar = scrollArea.verticalScrollBar();
		bar->setValue(0);
		QApplication::processEvents();
		// Force real rasterization per step: grab() renders the viewport
		// through the same paint path the screen would use.
		QElapsedTimer timer;
		timer.start();
		int steps = 0;
		for (int value = 0; value <= bar->maximum() && steps < 40; value += 120, steps++)
		{
			bar->setValue(value);
			QApplication::processEvents();
			scrollArea.viewport()->grab();
		}
		const qint64 elapsed = timer.elapsed();
		qWarning("ScrollBench: %s %d steps in %lld ms (%.1f ms/step)",
			qPrintable(skin->id()), steps, static_cast<long long>(elapsed),
			steps > 0 ? double(elapsed) / steps : 0.0);
	}
	std::fflush(nullptr);
	std::_Exit(0);
}

int SkinGallery::runPowerToggleTest()
{
	qWarning("PowerToggleTest: starting");
	QTemporaryDir scratch;
	if (!scratch.isValid())
	{
		qWarning("PowerToggleTest: cannot create a scratch directory");
		return 2;
	}
	qputenv("EAPO_SKIN_GALLERY", "1");
	const QString configPath = buildReferenceFiles(QDir(scratch.path()));
	if (configPath.isEmpty())
	{
		qWarning("PowerToggleTest: cannot write reference target files");
		return 2;
	}

	// Gain-less biquads (the field report) and full-grammar controls must
	// keep a real editor through load and an off/on round trip; a peaking
	// line missing its gain is one the ENGINE rejects ("no gain given"),
	// so its raw presentation is correct and only the text round trip is
	// held. Every line must survive the toggle byte-identically.
	struct Case { QString line; bool editor = true; };
	const QList<Case> cases = {
		{ QStringLiteral("Filter: ON NO Fc 800 Hz"), true },
		{ QStringLiteral("Filter: ON AP Fc 900 Hz BW Oct 1"), true },
		{ QStringLiteral("Filter: ON LP Fc 5000 Hz"), true },
		{ QStringLiteral("Filter: ON HPQ Fc 80 Hz Q 0.5"), true },
		{ QStringLiteral("Filter: ON BP Fc 1000 Hz Q 2"), true },
		{ QStringLiteral("Filter 1: ON PK Fc 1000 Hz Gain 6 dB Q 0.71"), true },
		{ QStringLiteral("Filter: ON PK Fc 1000 Hz Q 1"), false },
		{ QStringLiteral("Preamp: -3 dB"), true }
	};
	QList<QString> lines;
	for (const Case& c : cases)
		lines.append(c.line);

	int failures = 0;
	for (ISkin* skin : Skins::all())
	{
		const QString name = skin->id();
		QScrollArea scrollArea;
		scrollArea.resize(960, 720);
		SkinManager::instance()->applySkin(skin->id(), true);
		buildRows(scrollArea, configPath, lines);
		FilterTable* table = qobject_cast<FilterTable*>(scrollArea.widget());
		if (table == nullptr)
		{
			qWarning("PowerToggleTest: %s table construction failed", qPrintable(name));
			failures++;
			continue;
		}
		table->openConfig(QString());

		const auto rowWidget = [table](int index) -> FilterCardRow* {
			QList<FilterCardRow*> rows = table->findChildren<FilterCardRow*>(
				QString(), Qt::FindDirectChildrenOnly);
			std::sort(rows.begin(), rows.end(), [](FilterCardRow* a, FilterCardRow* b) {
				return a->y() < b->y();
			});
			return index < rows.size() ? rows[index] : nullptr;
		};
		const auto powerButton = [](FilterCardRow* row) -> QToolButton* {
			for (QToolButton* button : row->findChildren<QToolButton*>())
			{
				if (button->toolTip() == QStringLiteral("Enable or comment out this command"))
					return button;
			}
			return nullptr;
		};
		const auto settle = []() {
			QApplication::processEvents();
			QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
			QApplication::processEvents();
		};

		for (int i = 0; i < lines.size(); i++)
		{
			const QString original = table->documentItems().at(i)->text;
			if ((table->documentItems().at(i)->gui != nullptr) != cases[i].editor)
			{
				// The load-time half of the regression this gate pins: the
				// audit #275 B4 policy extraction dropped the card-to-chain
				// fallthrough and every plain biquad loaded as a raw row.
				qWarning("PowerToggleTest: %s row %d editor presence %d at load (expected %d)",
					qPrintable(name), i + 1,
					table->documentItems().at(i)->gui != nullptr ? 1 : 0, cases[i].editor ? 1 : 0);
				failures++;
			}
			for (int phase = 0; phase < 2; phase++)
			{
				FilterCardRow* row = rowWidget(i);
				QToolButton* button = row != nullptr ? powerButton(row) : nullptr;
				if (button == nullptr)
				{
					qWarning("PowerToggleTest: %s row %d lost its power button in phase %d",
						qPrintable(name), i + 1, phase);
					failures++;
					break;
				}
				button->setChecked(phase == 1);
				settle();
				const QString text = table->documentItems().at(i)->text;
				const QString expected = phase == 0
					? QStringLiteral("# ") + original : original;
				if (text != expected)
				{
					qWarning("PowerToggleTest: %s row %d phase %d text '%s' (expected '%s')",
						qPrintable(name), i + 1, phase,
						qPrintable(text), qPrintable(expected));
					failures++;
				}
			}
			FilterCardRow* row = rowWidget(i);
			const bool hasGui = table->documentItems().at(i)->gui != nullptr;
			if (hasGui != cases[i].editor)
			{
				qWarning("PowerToggleTest: %s row %d editor presence %d after the toggle (expected %d)",
					qPrintable(name), i + 1, hasGui ? 1 : 0, cases[i].editor ? 1 : 0);
				failures++;
			}
			Q_UNUSED(row);
		}
	}
	qWarning("PowerToggleTest: %s (%d failure(s))", failures == 0 ? "PASS" : "FAIL", failures);
	const int status = failures == 0 ? 0 : 1;
	std::fflush(nullptr);
	std::_Exit(status);
}
