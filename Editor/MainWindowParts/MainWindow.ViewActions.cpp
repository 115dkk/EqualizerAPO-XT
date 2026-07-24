#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <QDir>
#include <QDrag>
#include <QElapsedTimer>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointer>
#include <QScreen>
#include <QWindow>
#include <QPushButton>
#include <QStandardItemModel>
#include <QStringBuilder>
#include <QScrollArea>
#include <QFileInfo>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <QTimer>
#include <QToolBar>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include "helpers/StringHelper.h"
#include "helpers/LogHelper.h"
#include "helpers/ChannelHelper.h"
#include "Editor/helpers/GUIChannelHelper.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/helpers/EditorSettings.h"
#include "version.h"
#include "FilterTable.h"
#include "MainWindow.h"
#include "SkinManager.h"
#include "ui_MainWindow.h"

using std::find;
using std::list;
using std::set;
using std::shared_ptr;
using std::string;
using std::stringstream;
using std::vector;
using std::wstring;


namespace
{
// Companion tools (DeviceSelector, UpdateChecker) dress themselves from
// interface/skin + interface/dark at startup. savePreferences() writes the
// pair only when the Editor closes, so a freshly picked skin stayed
// invisible to a tool launched right after the switch - persist immediately.
void persistSkinChoice(const QString& skinId, bool dark)
{
	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	EditorSettings::writeSkinChoice(settings, { skinId, dark });
}
}

void MainWindow::on_mainToolBar_visibilityChanged(bool visible)
{
	ui->actionToolbar->setChecked(visible);
}

void MainWindow::startSkinSwitchStorm()
{
	// A diagnostic session must not overwrite the user's preferences, even if
	// it crashes or the window closes before the final step.
	noSavePreferences = true;
	noSaveFilePreferences = true;
	skinPersistenceSuppressed = true;

	// Steps: "skin <id> <dark|light>" (direct action trigger),
	// "menuskin <id> <dark|light>" (a REAL popup menu opened and clicked with
	// synthesized mouse events - the path a mouse user actually takes),
	// "resize <width>", "fullscreen" (toggle), "showtoolbar" (View re-check).
	static const char* const storm[] = {
		// Matrix early on purpose: its toolbar overlay boards were the styled-
		// background cover that blanked the strip for every skin after it.
		"menuskin studio dark", "menuskin matrix dark", "menuskin soft dark",
		"menuskin rack dark", "menuskin studio dark", "menuskin minimal dark",
		"menuskin matrix dark", "menuskin rack dark", "menuskin studio dark",
		"fullscreen", "fullscreen", "resize 900", "resize 1900",
		"skin rack dark", "skin studio dark",
	};
	const int stormCount = int(sizeof(storm) / sizeof(storm[0]));

	auto stepIndex = std::make_shared<int>(0);
	auto failures = std::make_shared<int>(0);

	// Screenshots judge what the flags cannot: real screen pixels of the
	// window's top strip, so a toolbar that claims to be visible while its
	// controls are not painted is caught (field report: "the commands say it
	// is there, but the screen quietly loses it").
	const QString shotDir = QDir::tempPath() + QStringLiteral("/eapo-storm-shots");
	QDir().mkpath(shotDir);
	qWarning("Storm: screenshots in %s", qPrintable(QDir::toNativeSeparators(shotDir)));

	connect(ui->mainToolBar, &QToolBar::visibilityChanged, this, [](bool visible) {
		qWarning("Storm: toolbar visibilityChanged(%d)", visible ? 1 : 0);
	});

	const auto checkToolbar = [this, failures, shotDir](const QString& afterStep, int shotIndex) {
		QToolBar* toolBar = ui->mainToolBar;
		int hidden = 0;
		QStringList hiddenNames;
		QStringList geometryDump;
		for (QAction* action : toolBar->actions())
		{
			QWidget* item = toolBar->widgetForAction(action);
			if (item == nullptr)
				continue;
			const QString label = !item->objectName().isEmpty() ? item->objectName() : action->objectName();
			// isHidden and isVisible disagree exactly when the show-state is
			// corrupted (shown flag set, WA_WState_Visible never delivered) -
			// the widget then lays out but never paints.
			QString flags;
			if (item->isHidden())
				flags += QStringLiteral(" HIDDEN");
			if (!item->isVisible())
				flags += QStringLiteral(" NOTVIS");
			if (!item->testAttribute(Qt::WA_WState_Visible))
				flags += QStringLiteral(" NOWSTATE");
			if (!item->updatesEnabled())
				flags += QStringLiteral(" NOUPD");
			if (item->visibleRegion().isEmpty())
				flags += QStringLiteral(" NOREGION");
			if (item->parentWidget() != toolBar)
				flags += QStringLiteral(" PARENT=%1/%2")
					.arg(item->parentWidget() != nullptr ? QLatin1String(item->parentWidget()->metaObject()->className()) : QLatin1String("null"))
					.arg(item->parentWidget() != nullptr ? item->parentWidget()->objectName() : QString());
			geometryDump.append(QStringLiteral("%1@%2,%3 %4x%5%6")
				.arg(label).arg(item->x()).arg(item->y()).arg(item->width()).arg(item->height())
				.arg(flags));
			// The rail-ear zones follow rack's chrome and the async menu click
			// makes their state race the check tick - logged above, never
			// counted. The format badge hides itself while the stream is
			// native; its visibility is data, not layout.
			if (item->objectName() == QLatin1String("RackToolbarEarSpacer")
				|| item->objectName() == QLatin1String("DeviceFormatBadge"))
				continue;
			if (item->isHidden())
			{
				hidden++;
				hiddenNames.append(label);
			}
		}
		qWarning("Storm %s: toolbar visible=%d geom %d,%d %dx%d hint %dx%d window %dx%d hiddenItems=%d [%s] actionChecked=%d",
			qPrintable(afterStep), toolBar->isVisible() ? 1 : 0,
			toolBar->geometry().x(), toolBar->geometry().y(), toolBar->width(), toolBar->height(),
			toolBar->sizeHint().width(), toolBar->sizeHint().height(),
			width(), height(), hidden, qPrintable(hiddenNames.join(QLatin1Char(','))),
			ui->actionToolbar->isChecked() ? 1 : 0);
		qWarning("Storm %s: items %s", qPrintable(afterStep), qPrintable(geometryDump.join(QStringLiteral(" | "))));

		// Ground truth beside the widgetForAction view: the toolbar's actual
		// child widgets. A divergence here (buttons missing from the child
		// list, or present but invisible) is what the action-based dump above
		// cannot see.
		QStringList childDump;
		for (QObject* child : toolBar->children())
		{
			QWidget* widget = qobject_cast<QWidget*>(child);
			if (widget == nullptr)
				continue;
			childDump.append(QStringLiteral("%1/%2@%3,%4 %5x%6%7%8")
				.arg(QLatin1String(widget->metaObject()->className()))
				.arg(widget->objectName())
				.arg(widget->x()).arg(widget->y()).arg(widget->width()).arg(widget->height())
				.arg(!widget->isVisible() ? QStringLiteral(" NOTVIS") : QString())
				.arg(widget->testAttribute(Qt::WA_StyledBackground) ? QStringLiteral(" SBG") : QString()));
		}
		qWarning("Storm %s: children %s", qPrintable(afterStep), qPrintable(childDump.join(QStringLiteral(" | "))));

		// Native-window census: a child promoted to a native HWND (winId on a
		// card frame propagates native-ness to ancestors and, by default, to
		// their siblings) composites separately and can drop out of both the
		// screen and QWidget::render while every logical flag stays healthy.
		int nativeCount = 0;
		QStringList nativeNames;
		for (QWidget* widget : QApplication::allWidgets())
		{
			if (widget->internalWinId() != 0 && widget->window() == this && widget != this)
			{
				nativeCount++;
				if (nativeNames.size() < 12)
					nativeNames.append(!widget->objectName().isEmpty()
						? widget->objectName() : QLatin1String(widget->metaObject()->className()));
			}
		}
		qWarning("Storm %s: natives=%d [%s]; toolbar native=%d mapped=%d outsideWS=%d inPaint=%d effect=%d",
			qPrintable(afterStep), nativeCount, qPrintable(nativeNames.join(QLatin1Char(','))),
			toolBar->internalWinId() != 0 ? 1 : 0,
			toolBar->testAttribute(Qt::WA_Mapped) ? 1 : 0,
			toolBar->testAttribute(Qt::WA_OutsideWSRange) ? 1 : 0,
			toolBar->testAttribute(Qt::WA_WState_InPaintEvent) ? 1 : 0,
			toolBar->graphicsEffect() != nullptr ? 1 : 0);

		// Real pixels: the window's top band (title + menu + toolbar + tab
		// row) from the screen itself, not a widget render.
		if (windowHandle() != nullptr && windowHandle()->screen() != nullptr)
		{
			const QPoint origin = mapToGlobal(QPoint(0, 0));
			QPixmap shot = windowHandle()->screen()->grabWindow(0, origin.x(), origin.y(), width(), 230);
			shot.save(QStringLiteral("%1/step%2.png").arg(shotDir).arg(shotIndex, 2, 10, QLatin1Char('0')));
		}
		// The widget-render path next to the screen path: if this one shows
		// the controls while the screen band is empty, the loss is below the
		// paint system (backing store / composition); if both are empty, it
		// is widget paint logic. A near-uniform render is the field bug (the
		// styled-background overlay cover) regardless of what the flags say.
		const QImage rendered = toolBar->grab().toImage().convertToFormat(QImage::Format_RGB32);
		rendered.save(QStringLiteral("%1/render%2.png").arg(shotDir).arg(shotIndex, 2, 10, QLatin1Char('0')));
		bool renderedBlank = false;
		if (toolBar->isVisible() && rendered.width() >= 10 && rendered.height() >= 4)
		{
			const QRgb cornerPixel = rendered.pixel(1, 1);
			qint64 same = 0;
			qint64 total = 0;
			for (int y = 0; y < rendered.height(); y += 2)
			{
				for (int x = 0; x < rendered.width(); x += 2)
				{
					total++;
					if (rendered.pixel(x, y) == cornerPixel)
						same++;
				}
			}
			renderedBlank = same * 100 >= total * 99;
			if (renderedBlank)
				qWarning("Storm %s: toolbar rendered blank (controls not painted)", qPrintable(afterStep));
		}

		// Hidden items only count against a window that has honest room for
		// the whole train and is not in graph fullscreen (which hides the bar).
		const bool roomy = width() >= toolBar->sizeHint().width() + 40;
		if (!graphFullscreen && (!toolBar->isVisible() || (roomy && hidden > 0) || renderedBlank))
			(*failures)++;
	};

	QTimer* timer = new QTimer(this);
	timer->setInterval(900);
	connect(timer, &QTimer::timeout, this, [this, timer, stepIndex, failures, checkToolbar, stormCount]() {
		// The previous step has had a full event-loop turn to settle; judge it.
		if (*stepIndex > 0)
			checkToolbar(QStringLiteral("step %1 [%2]").arg(*stepIndex)
				.arg(QLatin1String(storm[*stepIndex - 1])), *stepIndex);
		else
			checkToolbar(QStringLiteral("baseline"), 0);

		if (*stepIndex >= stormCount)
		{
			timer->stop();
			qWarning("Storm: done, %d steps, failures=%d", stormCount, *failures);
			std::fflush(nullptr);
			std::_Exit(*failures > 0 ? 1 : 0);
		}

		const QStringList step = QString::fromLatin1(storm[*stepIndex]).split(QLatin1Char(' '));
		(*stepIndex)++;
		if (step.value(0) == QLatin1String("resize"))
		{
			resize(step.value(1).toInt(), height());
		}
		else if (step.value(0) == QLatin1String("fullscreen"))
		{
			toggleGraphFullscreen();
		}
		else if (step.value(0) == QLatin1String("showtoolbar"))
		{
			// The user's recovery path: View > Toolbar re-check.
			ui->actionToolbar->setChecked(true);
			on_actionToolbar_triggered(true);
		}
		else if (step.value(0) == QLatin1String("healrepaint"))
		{
			ui->mainToolBar->repaint();
		}
		else if (step.value(0) == QLatin1String("healhideshow"))
		{
			ui->mainToolBar->hide();
			ui->mainToolBar->show();
		}
		else if (step.value(0) == QLatin1String("healreseat"))
		{
			removeToolBar(ui->mainToolBar);
			addToolBar(Qt::TopToolBarArea, ui->mainToolBar);
			ui->mainToolBar->show();
		}
		else if (step.value(0) == QLatin1String("menuskin"))
		{
			// The mouse user's actual path: the real Interface popup opens on
			// screen and the skin entry is clicked with synthesized mouse
			// events, so menu show/close interleaves with the stylesheet swap
			// exactly as it does live.
			const bool dark = step.value(2) == QLatin1String("dark");
			if (darkThemeAction != nullptr && darkThemeAction->isChecked() != dark)
				darkThemeAction->setChecked(dark);
			QAction* target = nullptr;
			if (skinActionGroup != nullptr)
			{
				for (QAction* action : skinActionGroup->actions())
				{
					if (action->data().toString() == step.value(1))
					{
						target = action;
						break;
					}
				}
			}
			QMenu* menu = target != nullptr ? qobject_cast<QMenu*>(target->parent()) : nullptr;
			if (menu != nullptr)
			{
				menu->popup(mapToGlobal(QPoint(140, 90)));
				QPointer<QMenu> menuGuard(menu);
				QPointer<QAction> targetGuard(target);
				QTimer::singleShot(250, this, [menuGuard, targetGuard]() {
					if (menuGuard.isNull() || targetGuard.isNull() || !menuGuard->isVisible())
						return;
					const QPointF local = QRectF(menuGuard->actionGeometry(targetGuard)).center();
					const QPointF global = menuGuard->mapToGlobal(local.toPoint());
					QCoreApplication::postEvent(menuGuard, new QMouseEvent(QEvent::MouseButtonPress,
						local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier));
					QCoreApplication::postEvent(menuGuard, new QMouseEvent(QEvent::MouseButtonRelease,
						local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier));
				});
			}
		}
		else if (step.value(0) == QLatin1String("skin"))
		{
			const bool dark = step.value(2) == QLatin1String("dark");
			if (darkThemeAction != nullptr && darkThemeAction->isChecked() != dark)
				darkThemeAction->setChecked(dark);
			if (skinActionGroup != nullptr)
			{
				for (QAction* action : skinActionGroup->actions())
				{
					if (action->data().toString() == step.value(1))
					{
						action->setChecked(true);
						action->trigger();
						break;
					}
				}
			}
		}
	});
	timer->start();
}

void MainWindow::on_analysisDockWidget_visibilityChanged(bool visible)
{
	ui->actionAnalysisPanel->setChecked(visible);

	if (visible)
		startAnalysis();
}

void MainWindow::on_actionToolbar_triggered(bool checked)
{
	ui->mainToolBar->setVisible(checked);
}

void MainWindow::on_actionAnalysisPanel_triggered(bool checked)
{
	ui->analysisDockWidget->setVisible(checked);
}

void MainWindow::interfaceModeSelected(QAction* action)
{
	if (action == nullptr)
		return;

	FilterTable::RenderMode mode = static_cast<FilterTable::RenderMode>(action->data().toInt());
	if (mode == currentRenderMode)
		return;

	// The two modes are whole presentations, not just row widgets: heritage
	// (legacy rows) runs unskinned on the native style, frame, font engine and
	// system fonts. None of that can swap cleanly inside a live process, and a
	// partial swap mixes modern chrome around legacy rows. Restart into the
	// chosen presentation instead.
	if (QMessageBox::question(this, tr("Restart required"), tr("Configuration Editor will be restarted to apply the changed settings. Proceed?")) == QMessageBox::Yes)
	{
		// savePreferences() persists interface/legacyRows from this member on
		// close, so setting it is what makes the restart come back changed.
		currentRenderMode = mode;
		restart = true;
		close();
	}
	else
	{
		for (QAction* other : interfaceModeActionGroup->actions())
			other->setChecked(static_cast<FilterTable::RenderMode>(other->data().toInt()) == currentRenderMode);
	}
}

void MainWindow::skinSelected(QAction* action)
{
	if (action == nullptr)
		return;

	skinId = action->data().toString();
	applySkinAndRebuild();
}

void MainWindow::applySkinAndRebuild()
{
	// Tear the rows down BEFORE swapping the global stylesheet. qApp->setStyleSheet
	// re-polishes every live widget, and a loaded config inflates the tree to
	// thousands of widgets; re-polishing them only to immediately rebuild them in
	// updateGuis() below made each skin switch cost seconds. Clearing first lets
	// the stylesheet apply against just the window chrome, and the rebuilt rows
	// are polished a single time when they are created.
	for (int i = 0; i < ui->tabWidget->count(); i++)
	{
		FilterTable* filterTable = filterTableForTab(i);
		if (filterTable != nullptr)
			filterTable->clearRows();
	}
	SkinManager::instance()->applySkin(skinId, skinDark);
	skinId = SkinManager::instance()->currentSkinId();
	// Each skin supplies its own Copy routing renderer (node graph, crosspoint
	// matrix, step list, ...) and per-skin card chrome. Those widgets are built
	// once when the row is created, so the rows must be rebuilt for the new
	// skin to take effect — a plain repaint only re-colours the old widgets and
	// left every skin showing the studio node graph for Copy.
	for (int i = 0; i < ui->tabWidget->count(); i++)
	{
		FilterTable* filterTable = filterTableForTab(i);
		if (filterTable != nullptr)
			filterTable->updateGuis();
	}
	if (!skinPersistenceSuppressed)
		persistSkinChoice(skinId, skinDark);
}

void MainWindow::darkThemeToggled(bool checked)
{
	skinDark = checked;
	applySkinAndRebuild();
}

void MainWindow::on_graphPositionComboBox_currentIndexChanged(int index)
{
	if (index < 0 || index > 2 || index == graphDockPosition)
		return;

	graphDockPosition = index;
	applyRedesignPreferences();
}

void MainWindow::knobRangeSelected(QAction* action)
{
	if (action == nullptr)
		return;

	double range = action->data().toDouble();
	if (range == 0.0)
	{
		// The "Custom..." entry asks for a number instead of carrying one.
		bool ok = false;
		range = QInputDialog::getDouble(this, tr("Knob gain range"),
			tr("Gain knobs will cover ± this many dB:"),
			GUIHelper::knobGainRange(), 1.0, 100.0, 1, &ok);
		if (!ok)
		{
			syncKnobRangeActions();
			return;
		}
	}

	GUIHelper::setKnobGainRange(range);
	syncKnobRangeActions();
	// Rebuild the open rows so existing Preamp / Filter knobs pick up the new span.
	for (int i = 0; i < ui->tabWidget->count(); i++)
	{
		FilterTable* filterTable = filterTableForTab(i);
		if (filterTable != nullptr)
			filterTable->updateGuis();
	}
}

void MainWindow::toggleGraphFullscreen()
{
	graphFullscreen = !graphFullscreen;
	if (graphFullscreen)
	{
		// Remember the toolbar's visibility BEFORE hiding it: the hide fires
		// visibilityChanged, which unchecks actionToolbar, so consulting the
		// action on the way out latched the toolbar hidden forever after one
		// fullscreen round trip - the "toolbar is gone" field report.
		toolbarVisibleBeforeGraphFullscreen = !ui->mainToolBar->isHidden();
		ui->centralWidget->setVisible(false);
		ui->mainToolBar->setVisible(false);
	}
	else
	{
		ui->centralWidget->setVisible(true);
		ui->mainToolBar->setVisible(toolbarVisibleBeforeGraphFullscreen);
	}
	ui->analysisDockWidget->setVisible(true);
	if (fullscreenGraphAction != nullptr)
		fullscreenGraphAction->setChecked(graphFullscreen);
}

void MainWindow::nativeTitleBarToggled(bool checked)
{
	QAction* action = qobject_cast<QAction*>(sender());

	// Frame styles can only change cleanly before the window exists, so this
	// follows the language flow: persist the choice and offer a restart.
	if (QMessageBox::question(this, tr("Restart required"), tr("Configuration Editor will be restarted to apply the changed settings. Proceed?")) == QMessageBox::Yes)
	{
		QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
		settings.setValue(QLatin1String(EditorSettings::Keys::NativeTitleBar), checked);
		restart = true;
		close();
	}
	else if (action != nullptr)
	{
		action->blockSignals(true);
		action->setChecked(!checked);
		action->blockSignals(false);
	}
}

void MainWindow::languageSelected(bool selected)
{
	QAction* action = qobject_cast<QAction*>(sender());

	if (!selected)
	{
		action->setChecked(true);
		return;
	}

	QLocale::Language language = static_cast<QLocale::Language>(action->data().toInt());

	if (QMessageBox::question(this, tr("Restart required"), tr("Configuration Editor will be restarted to apply the changed settings. Proceed?")) == QMessageBox::Yes)
	{
		QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
		if (language == QLocale::AnyLanguage)
		{
			settings.remove("language");
		}
		else
		{
			QString name = QLocale(language).name();
			int index = name.indexOf('_');
			if (index != -1)
				name = name.left(index);
			settings.setValue("language", name);
		}

		restart = true;
		close();
	}
	else
	{
		action->setChecked(false);
	}
}

void MainWindow::on_actionResetAllGlobalPreferences_triggered()
{
	if (QMessageBox::question(this, tr("Restart required"), tr("Configuration Editor will be restarted to apply the changed settings. Proceed?")) == QMessageBox::Yes)
	{
		QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
		for (const QString& key : settings.childGroups())
		{
			if (key != "file-specific")
				settings.remove(key);
		}
		for (const QString& key : settings.childKeys())
			settings.remove(key);

		restart = true;
		noSavePreferences = true;
		close();
	}
}

void MainWindow::on_actionResetAllFileSpecificPreferences_triggered()
{
	if (QMessageBox::question(this, tr("Restart required"), tr("Configuration Editor will be restarted to apply the changed settings. Proceed?")) == QMessageBox::Yes)
	{
		QSettings settings(QString::fromWCharArray(EDITOR_PER_FILE_REGPATH), QSettings::NativeFormat);
		for (const QString& key : settings.childGroups())
			settings.remove(key);
		for (const QString& key : settings.childKeys())
			settings.remove(key);

		restart = true;
		noSaveFilePreferences = true;
		close();
	}
}

