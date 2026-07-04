#include <sstream>
#include <QDrag>
#include <QElapsedTimer>
#include <QLabel>
#include <QMimeData>
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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include "helpers/StringHelper.h"
#include "helpers/LogHelper.h"
#include "helpers/ChannelHelper.h"
#include "Editor/helpers/GUIChannelHelper.h"
#include "Editor/helpers/GUIHelper.h"
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


void MainWindow::on_mainToolBar_visibilityChanged(bool visible)
{
	ui->actionToolbar->setChecked(visible);
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
	// partial swap is exactly the modern-chrome-around-legacy-rows mixture this
	// mode used to show. Restart into the chosen presentation instead.
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
}

void MainWindow::darkThemeToggled(bool checked)
{
	skinDark = checked;
	// Clear rows before the global stylesheet swap (see skinSelected) so the
	// re-polish does not run over thousands of soon-to-be-rebuilt card widgets.
	for (int i = 0; i < ui->tabWidget->count(); i++)
	{
		FilterTable* filterTable = filterTableForTab(i);
		if (filterTable != nullptr)
			filterTable->clearRows();
	}
	SkinManager::instance()->applySkin(skinId, skinDark);
	skinId = SkinManager::instance()->currentSkinId();
	// Rebuild rows so painted card/routing widgets pick up the new palette.
	for (int i = 0; i < ui->tabWidget->count(); i++)
	{
		FilterTable* filterTable = filterTableForTab(i);
		if (filterTable != nullptr)
			filterTable->updateGuis();
	}
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
	ui->centralWidget->setVisible(!graphFullscreen);
	ui->mainToolBar->setVisible(!graphFullscreen && ui->actionToolbar->isChecked());
	ui->analysisDockWidget->setVisible(true);
}

void MainWindow::nativeTitleBarToggled(bool checked)
{
	QAction* action = qobject_cast<QAction*>(sender());

	// Frame styles can only change cleanly before the window exists, so this
	// follows the language flow: persist the choice and offer a restart.
	if (QMessageBox::question(this, tr("Restart required"), tr("Configuration Editor will be restarted to apply the changed settings. Proceed?")) == QMessageBox::Yes)
	{
		QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
		settings.setValue("interface/nativeTitleBar", checked);
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

