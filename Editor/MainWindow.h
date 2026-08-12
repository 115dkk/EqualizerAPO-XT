/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2015  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

#include <functional>
#include "services/registry/RegistryPaths.h"
#include <memory>
#include <string>
#include <vector>
#include <QMainWindow>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QActionGroup>
#include <QTimer>

#include "FilterTable.h"
#include "devices/DeviceAPOInfo.h"
#include "Editor/AnalysisThread.h"
#include "Editor/widgets/EqGraphView.h"
#include "services/registry/WindowsRegistry.h"

#define EDITOR_PER_FILE_REGPATH EDITOR_REGPATH L"\\file-specific"

namespace Ui {
class MainWindow;
}

class QLabel;
class TitleBar;
class UpdateToast;
class UpdateSession;
namespace SkinSwitchStorm { void run(MainWindow& window); }

// MainWindow's implementation is split across several translation units (all
// listed in Editor.pro SOURCES). When looking for a method, check the matching
// part file:
//   MainWindow.cpp                             - ctor/dtor, shared setup, doChecks
//   MainWindowParts/MainWindow.Analysis.cpp    - instant mode + analysis panel
//   MainWindowParts/MainWindow.Device.cpp      - device/channel selection + tabs
//   MainWindowParts/MainWindow.Edit.cpp        - cut/copy/paste edit actions
//   MainWindowParts/MainWindow.FileActions.cpp - open/save/recent menu actions
//   MainWindowParts/MainWindow.FileIO.cpp      - config load/save
//   MainWindowParts/MainWindow.Frame.cpp       - custom window chrome / title bar
//   MainWindowParts/MainWindow.Preferences.cpp - settings load/store
//   MainWindowParts/MainWindow.ViewActions.cpp - view/skin/reset actions
class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit MainWindow(QDir configDir, const UpdateSession* updateSession, QWidget* parent = 0,
		bool analysisLayoutTestMode = false);
	~MainWindow();
	void doChecks();
	void runDeviceSelector();
	void load(QString path);
	void save(FilterTable* filterTable, QString path);
	bool isEmpty();
	bool shouldRestart();
	void startAnalysis();
	// Builds the analysis graph's metric switch and its base-delay option,
	// restores both from settings, and wires them to the graph. Deliberately
	// does not touch the analysis thread: which quantity is on screen is a
	// display choice derived from the response already in hand.
	void setupAnalysisMetricControls();

protected:
	void closeEvent(QCloseEvent* event) override;
	// Custom window chrome: removes the native caption while keeping native
	// move/snap/resize (see MainWindowParts/MainWindow.Frame.cpp).
	bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private slots:
	void deviceSelected(int index);
	void channelConfigurationSelected(int index);
	void linesChanged();

	bool on_tabWidget_tabCloseRequested(int index);
	void on_actionOpen_triggered();
	void on_actionSave_triggered();
	void on_actionSaveAs_triggered();
	void on_actionNew_triggered();
	void recentFileSelected();

	void on_actionUndo_triggered();
	void on_actionRedo_triggered();
	void on_actionCut_triggered();
	void on_actionCopy_triggered();
	void on_actionPaste_triggered();
	void on_actionDelete_triggered();
	void on_actionSelectAll_triggered();

	void instantModeEnabled(bool enabled);
	void on_tabWidget_currentChanged(int index);
	void on_startFromComboBox_activated(int index);
	void on_analysisChannelComboBox_activated(int index);
	void on_resolutionSpinBox_valueChanged(int value);
	void updateAnalysisPanel();

	void on_mainToolBar_visibilityChanged(bool visible);
	void on_analysisDockWidget_visibilityChanged(bool visible);
	void on_actionToolbar_triggered(bool checked);
	void on_actionAnalysisPanel_triggered(bool checked);

	void on_actionApoSettings_triggered();
	void on_actionOpenProgramFolder_triggered();
	void languageSelected(bool selected);
	void interfaceModeSelected(QAction* action);
	void skinSelected(QAction* action);
	void darkThemeToggled(bool checked);
	void knobRangeSelected(QAction* action);
	void on_graphPositionComboBox_currentIndexChanged(int index);
	void nativeTitleBarToggled(bool checked);
	void toggleGraphFullscreen();
	void on_actionResetAllGlobalPreferences_triggered();
	void on_actionResetAllFileSpecificPreferences_triggered();

private:
	friend void SkinSwitchStorm::run(MainWindow& window);
	void startSkinSwitchStorm();
	void applySkinAndRebuild();
	void executeStartAnalysis();
	FilterTable* addTab(QString title, QString tooltip, QString configPath, QList<QString> lines);
	void getDeviceAndChannelMask(std::shared_ptr<AbstractAPOInfo>* selectedDevice, int* channelMask);
	void updateDeviceFormatBadge(const std::shared_ptr<AbstractAPOInfo>& apoInfo);
	bool askForClose(int tabIndex);
	void loadPreferences();
	void savePreferences();
	void updateRecentFiles();
	void setupRedesignActions();
	void setupWindowChrome();
	void applyRedesignPreferences();
	// Re-tint the toolbar and menu action icons from the active skin's
	// tokens; wired to SkinManager::skinChanged in the constructor so every
	// switch path (menu, shortcut, preferences) re-dresses the chrome.
	void dressSkinChrome();
	void syncKnobRangeActions();
	void setCurrentRenderMode(FilterTable::RenderMode mode);
	// Polls the process-owned UpdateSession for a staged background update and raises the
	// bottom toast once when one appears (the download itself stays silent).
	void watchForPendingUpdate();
	FilterTable* filterTableForTab(int tabIndex) const;
	FilterTable* currentFilterTable() const;
	void forEachFilterTable(const std::function<void(int, FilterTable*)>& visitor) const;
	void updateDirtyStatus();
	// Grey the Edit-menu undo/redo entries out while the active tab's history
	// has nothing to step to; without this they always render enabled and
	// silently no-op, which reads as "undo/redo is gone".
	void updateUndoRedoActions();
	template<class T> QList<T> toQList(const std::vector<T>& vector);

	std::unique_ptr<Ui::MainWindow> ui;

	QDir configDir;
	QCheckBox* instantModeCheckBox;
	QLabel* dirtyStatusLabel = nullptr;
	QComboBox* deviceComboBox;
	QLabel* deviceFormatBadge = nullptr;
	QComboBox* channelConfigurationComboBox;
	QList<std::shared_ptr<AbstractAPOInfo>> outputDevices;
	QList<std::shared_ptr<AbstractAPOInfo>> inputDevices;
	std::shared_ptr<AbstractAPOInfo> defaultOutputDevice;
	EqGraphView* eqGraphView = nullptr;
	std::unique_ptr<AnalysisThread> analysisThread;
	QTimer* analysisDebounceTimer = nullptr;
	bool restart = false;
	bool noSavePreferences = false;
	bool noSaveFilePreferences = false;
	bool skinPersistenceSuppressed = false;
	QStringList recentFiles;
	QString skinId = QStringLiteral("studio");
	bool skinDark = true;
	bool graphFullscreen = false;
	// Snapshot for leaving graph fullscreen: hiding the toolbar on the way in
	// unchecks actionToolbar through the visibilityChanged sync, so the
	// action's checked state cannot say whether the user wanted the bar.
	bool toolbarVisibleBeforeGraphFullscreen = true;
	// 0 = top, 1 = bottom (default, matching the original Equalizer APO), 2 = right.
	int graphDockPosition = 1;
	FilterTable::RenderMode currentRenderMode = FilterTable::ModernCards;
	QActionGroup* interfaceModeActionGroup = nullptr;
	QActionGroup* skinActionGroup = nullptr;
	QAction* darkThemeAction = nullptr;
	QAction* fullscreenGraphAction = nullptr;
	QActionGroup* knobRangeActionGroup = nullptr;
	// Custom window chrome (frameless caption); nullptr when the
	// interface/nativeTitleBar escape hatch keeps the stock caption.
	TitleBar* titleBar = nullptr;
	bool useCustomFrame = true;
	// Bottom-centre notice for the staged auto-update (created on demand).
	UpdateToast* updateToast = nullptr;
	QTimer* updateNoticeTimer = nullptr;
	const UpdateSession* updateSession = nullptr;
	bool analysisLayoutTestMode = false;
};

Q_DECLARE_METATYPE(std::shared_ptr<AbstractAPOInfo>)
