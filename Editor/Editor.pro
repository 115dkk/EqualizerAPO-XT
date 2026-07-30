#-------------------------------------------------
#
# Project created by QtCreator 2014-10-21T22:44:17
#
#-------------------------------------------------

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = Editor
TEMPLATE = app

# Object files mirror the source tree. Without this, qmake drops every object
# into one directory by basename, and ../filters/velvet/Processor.cpp then
# collides with ../BassManagementCore/src/Processor.cpp (last one wins at
# link time).
CONFIG += object_parallel_to_source

PRECOMPILED_HEADER = stable.h
QMAKE_CXXFLAGS_WARN_ON -= -w34100
QMAKE_LFLAGS += /STACK:32000000
QMAKE_CXXFLAGS_RELEASE += /O2

DEFINES += _UNICODE
DEFINES += MUP_USE_WIDE_STRING
DEFINES += NOMINMAX

SOURCES += main.cpp\
	../helpers/LogHelper.cpp \
	../helpers/StringHelper.cpp \
	../helpers/RegistryHelper.cpp \
	../helpers/IRegistry.cpp \
	../helpers/RegistryTransaction.cpp \
	../helpers/WindowsVersion.cpp \
	../helpers/AudioEngineAccess.cpp \
	../helpers/InstallDiagnostics.cpp \
	../helpers/ServiceHelper.cpp \
	../helpers/ApoRegistration.cpp \
	../helpers/AudioFormatProbe.cpp \
	../helpers/VelopackBootstrap.cpp \
	../parser/LogicalOperators.cpp \
	IFilterGUIFactory.cpp \
	FilterGUIFactoryRegistry.cpp \
	IFilterGUI.cpp \
	guis/PreampFilterGUI.cpp \
	guis/PreampFilterGUIFactory.cpp \
	guis/CommentFilterGUIFactory.cpp \
	guis/CommentFilterGUI.cpp \
	FilterTable.cpp \
	FilterTableParts/FilterTable.Clipboard.cpp \
	FilterTableParts/FilterTable.DragDrop.cpp \
	FilterTableParts/FilterTable.Events.cpp \
	FilterTableParts/FilterTable.Model.cpp \
	FilterTableParts/FilterTable.Mouse.cpp \
	../filters/PreampCommand.cpp \
	../filters/PreampFilter.cpp \
	../filters/PreampFilterFactory.cpp \
	../helpers/MemoryHelper.cpp \
	../helpers/ParallelExecutor.cpp \
	FilterTableRow.cpp \
	FilterTemplate.cpp \
	guis/DeviceFilterGUI.cpp \
	guis/DeviceFilterGUIFactory.cpp \
	../devices/DeviceAPOInfo.cpp \
	../devices/DeviceAPOInfo.Install.cpp \
	../devices/DeviceAPOInfo.Load.cpp \
	../devices/DeviceAPOInfo.State.cpp \
	../devices/DeviceAPOInfo.Uninstall.cpp \
	../devices/DeviceInstallReport.cpp \
	guis/DeviceFilterGUIDialog.cpp \
	../filters/DeviceCommand.cpp \
	../filters/DeviceFilterFactory.cpp \
	guis/ChannelFilterGUIDialog.cpp \
	guis/ChannelFilterGUI.cpp \
	guis/ChannelFilterGUIFactory.cpp \
	guis/BiQuadFilterGUI.cpp \
	guis/BiQuadWidthConversion.cpp \
	../filters/BiQuad.cpp \
	../filters/BiQuadCommand.cpp \
	../filters/BiQuadFilter.cpp \
	../filters/BiQuadFilterFactory.cpp \
	guis/BiQuadFilterGUIFactory.cpp \
	guis/CopyFilterGUIFactory.cpp \
	guis/CopyFilterGUI.cpp \
	guis/CopyFilterGUIConnectionItem.cpp \
	guis/CopyFilterGUIChannelItem.cpp \
	../filters/CopyFilter.cpp \
	../filters/CopyFilterFactory.cpp \
	../engine/IFilter.cpp \
	guis/CopyFilterGUIScene.cpp \
	guis/CopyFilterGUIForm.cpp \
	guis/CopyFilterGUIRow.cpp \
	helpers/GUIChannelHelper.cpp \
	../helpers/ChannelHelper.cpp \
	guis/DelayFilterGUI.cpp \
	guis/DelayFilterGUIFactory.cpp \
	../filters/DelayCommand.cpp \
	../filters/DelayFilter.cpp \
	../filters/DelayFilterFactory.cpp \
	guis/IncludeFilterGUI.cpp \
	guis/IncludeFilterGUIFactory.cpp \
	helpers/GUIHelper.cpp \
	helpers/QtAppBootstrap.cpp \
	widgets/ResizingLineEdit.cpp \
	widgets/ChannelGraphScene.cpp \
	widgets/ChannelGraphItem.cpp \
	guis/ChannelFilterGUIScene.cpp \
	guis/ChannelFilterGUIChannelItem.cpp \
	guis/GraphicEQFilterGUIFactory.cpp \
	guis/GraphicEQFilterGUI.cpp \
	../filters/GraphicEQCommand.cpp \
	../filters/GraphicEQFilter.cpp \
	../filters/GraphicEQFilterFactory.cpp \
	../libHybridConv-0.1.1/libHybridConv_eapo.cpp \
	../helpers/FftwPlanningPolicy.cpp \
	../helpers/GainIterator.cpp \
	guis/GraphicEQFilterGUIScene.cpp \
	widgets/FrequencyPlotView.cpp \
	widgets/FrequencyPlotHRuler.cpp \
	widgets/FrequencyPlotVRuler.cpp \
	widgets/FrequencyPlotItem.cpp \
	guis/GraphicEQFilterGUIItem.cpp \
	guis/GraphicEQFilterGUIView.cpp \
	widgets/FrequencyPlotScene.cpp \
	widgets/CompactToolBar.cpp \
	guis/ConvolutionFilterGUIFactory.cpp \
	guis/ConvolutionFilterGUI.cpp \
	guis/MultiConvolutionFilterGUIFactory.cpp \
	guis/MultiConvolutionFilterGUI.cpp \
	guis/SpatialFilterGUIFactory.cpp \
	guis/BassManagementFilterGUIFactory.cpp \
	helpers/ConvolutionPathHelper.cpp \
	helpers/DisableWheelFilter.cpp \
	widgets/EscapableLineEdit.cpp \
	MainWindow.cpp \
	MainWindowParts/MainWindow.Analysis.cpp \
	MainWindowParts/MainWindow.Device.cpp \
	MainWindowParts/MainWindow.Edit.cpp \
	MainWindowParts/MainWindow.FileActions.cpp \
	MainWindowParts/MainWindow.FileIO.cpp \
	MainWindowParts/MainWindow.Frame.cpp \
	MainWindowParts/MainWindow.Preferences.cpp \
	MainWindowParts/MainWindow.ViewActions.cpp \
	diagnostics/SkinSwitchStorm.cpp \
	ConfigFileCodec.cpp \
	guis/StageFilterGUI.cpp \
	guis/StageFilterGUIFactory.cpp \
	guis/ExpressionFilterGUIFactory.cpp \
	widgets/ResizeCorner.cpp \
	analysis/AnalysisResponse.cpp \
	analysis/ResponseCurveBuilder.cpp \
	../engine/FilterEngine.cpp \
	../engine/FilterEngine.Configuration.cpp \
	../engine/FilterEngine.Process.cpp \
	../engine/FilterEngine.Runtime.cpp \
	../engine/ConfigWatcher.cpp \
	../filters/FilterFactoryRegistry.cpp \
	../engine/FilterConfiguration.cpp \
	../filters/ChannelCommand.cpp \
	../filters/ChannelFilterFactory.cpp \
	../filters/ExpressionCommand.cpp \
	../filters/ExpressionFilterFactory.cpp \
	../filters/IfCommand.cpp \
	../filters/IfFilterFactory.cpp \
	../filters/StageCommand.cpp \
	../filters/StageFilterFactory.cpp \
	../filters/ConvolutionCommand.cpp \
	../filters/ConvolutionFilterFactory.cpp \
	../filters/IIRCommand.cpp \
	../filters/IIRFilter.cpp \
	../filters/IIRFilterFactory.cpp \
	../filters/IncludeCommand.cpp \
	../filters/IncludeFilterFactory.cpp \
	../filters/ChannelFilter.cpp \
	../filters/ConvolutionFilter.cpp \
	../filters/ConvolutionFilePath.cpp \
	../filters/MultiConvolutionCommand.cpp \
	../filters/MultiConvolutionFilter.cpp \
	../filters/MultiConvolutionFilterFactory.cpp \
	../filters/bassManagement/BassManagementCommand.cpp \
	../filters/bassManagement/BassManagementFilter.cpp \
	../filters/bassManagement/BassManagementFilterFactory.cpp \
	../BassManagementCore/src/Compiler.cpp \
	../BassManagementCore/src/Graph.cpp \
	../BassManagementCore/src/Json.cpp \
	../BassManagementCore/src/Preset.cpp \
	../BassManagementCore/src/Processor.cpp \
	../BassManagementCore/src/StateCodec.cpp \
	../filters/HilbertCommand.cpp \
	../filters/HilbertFilter.cpp \
	../filters/HilbertFilterFactory.cpp \
	../filters/VelvetCommand.cpp \
	../filters/VelvetFilter.cpp \
	../filters/VelvetFilterFactory.cpp \
	../filters/velvet/Processor.cpp \
	../engine/ConfigurationFileReader.cpp \
	../filters/IrCache.cpp \
	../parser/ParserExtensions.cpp \
	../parser/EngineParser.cpp \
	../parser/RegexFunctions.cpp \
	../parser/RegistryFunctions.cpp \
	../parser/StringOperators.cpp \
	AnalysisThread.cpp \
	widgets/ExponentialSpinBox.cpp \
	FilterTableMimeData.cpp \
	CustomStyle.cpp \
	../devices/AbstractAPOInfo.cpp \
	../devices/VoicemeeterAPOInfo.cpp \
	../helpers/AbstractLibrary.cpp \
	../helpers/VST3PluginIIDs.cpp \
	../helpers/VSTPluginLibrary.cpp \
	guis/VSTPluginFilterGUI.cpp \
	guis/VSTPluginFilterGUIFactory.cpp \
	guis/VSTPluginFilterGUIDialog.cpp \
	../filters/VSTPluginCommand.cpp \
	../filters/VSTPluginFilter.cpp \
	../filters/VSTPluginFilterFactory.cpp \
	../helpers/VSTPluginInstance.cpp \
	../helpers/VSTPluginInstance.Editor.cpp \
	../helpers/VSTPluginInstance.State.cpp \
	../helpers/VSTPluginInstance.VST2.cpp \
	../helpers/VSTPluginInstance.VST3.cpp \
	../helpers/PerfProfile.cpp \
	guis/LoudnessCorrectionFilterGUI.cpp \
	guis/LoudnessCorrectionFilterGUIFactory.cpp \
	../filters/loudnessCorrection/LoudnessCorrectionCommand.cpp \
	../filters/loudnessCorrection/LoudnessCorrectionFilter.cpp \
	../filters/loudnessCorrection/LoudnessCorrectionFilterFactory.cpp \
	../filters/loudnessCorrection/VolumeController.cpp \
	guis/LoudnessCorrectionFilterGUIDialog.cpp \
	helpers/CrashHandler.cpp \
	helpers/QtSndfileHandle.cpp \
	SkinGallery.cpp \
	SkinManager.cpp \
	skins/ISkin.cpp \
	skins/pickers/MatrixFilterPicker.cpp \
	skins/RackChrome.cpp \
	skins/Skins.cpp \
	skins/SkinFileIcons.cpp \
	skins/SkinThemeData.cpp \
	skins/StudioSkin.cpp \
	skins/MinimalSkin.cpp \
	skins/SoftSkin.cpp \
	skins/RackSkin.cpp \
	skins/MatrixSkin.cpp \
	skins/pickers/StudioFilterPicker.cpp \
	skins/pickers/MinimalFilterPicker.cpp \
	skins/pickers/SoftFilterPicker.cpp \
	skins/pickers/RackFilterPicker.cpp \
	widgets/AddCardRow.cpp \
	widgets/ActivatableListChrome.cpp \
	widgets/AudioKnob.cpp \
	widgets/FilterInsertSeam.cpp \
	widgets/GraphicEQPlotWidget.cpp \
	widgets/FlowLayout.cpp \
	widgets/CommandRowFrame.cpp \
	widgets/SkinComboBox.cpp \
	widgets/cards/ChannelCardEditor.cpp \
	widgets/cards/ChannelSelectionModel.cpp \
	widgets/cards/ConvolutionCardEditor.cpp \
	widgets/cards/BassManagementCardEditor.cpp \
	widgets/cards/BassManagementCardView.cpp \
	widgets/cards/MultiConvolutionCardEditor.cpp \
	widgets/cards/CommentCardEditor.cpp \
	widgets/cards/DeviceCardEditor.cpp \
	widgets/cards/StageCardEditor.cpp \
	widgets/cards/StageSelectionModel.cpp \
	widgets/cards/DeviceSelectionModel.cpp \
	widgets/cards/FilterCardEditorFactory.cpp \
	widgets/cards/DelayCardEditor.cpp \
	widgets/cards/FilterCardEditorRegistry.cpp \
	widgets/cards/FileReferenceController.Dialogs.cpp \
	widgets/cards/FileReferenceController.cpp \
	widgets/cards/GraphicEQCardEditor.cpp \
	widgets/cards/IIRCardEditor.cpp \
	widgets/cards/AllPassCardEditor.cpp \
	widgets/cards/HilbertCardEditor.cpp \
	widgets/cards/VelvetCardEditor.cpp \
	widgets/cards/VelvetImpulsePreview.cpp \
	widgets/cards/FilterCardEditorRouter.cpp \
	analysis/AnalysisViewController.cpp \
	widgets/cards/IncludeCardEditor.cpp \
	widgets/cards/PreampCardEditor.cpp \
	widgets/cards/ScalarKnobCardEditor.cpp \
	widgets/cards/ReferenceCardView.cpp \
	widgets/cards/DefaultReferenceCardView.cpp \
	widgets/cards/VSTCardEditor.cpp \
	skins/cards/StudioReferenceCardView.cpp \
	skins/cards/StudioBassManagementCardView.cpp \
	skins/cards/MinimalBassManagementCardView.cpp \
	skins/cards/SoftBassManagementCardView.cpp \
	skins/cards/RackBassManagementCardView.cpp \
	skins/cards/MatrixBassManagementCardView.cpp \
	skins/cards/MinimalReferenceCardView.cpp \
	skins/cards/SoftReferenceCardView.cpp \
	skins/cards/RackReferenceCardView.cpp \
	skins/cards/MatrixReferenceCardView.cpp \
	widgets/ElidedLabel.cpp \
	widgets/EditableValue.cpp \
	widgets/EditableValueText.cpp \
	widgets/ChBadge.cpp \
	widgets/EqGraphView.cpp \
	widgets/SegmentedControl.cpp \
	widgets/FilterCardModel.cpp \
	widgets/FilterCardRow.cpp \
	widgets/FilterListModel.cpp \
	widgets/FilterListUndo.cpp \
	widgets/FilterPickerView.cpp \
	widgets/UpdateToast.cpp \
	widgets/MainToolbarKit.cpp \
	widgets/ValueScrubBox.cpp \
	widgets/DialogChrome.cpp \
	widgets/TitleBar.cpp \
	widgets/routing/CopyRoutingAdapter.cpp \
	widgets/routing/RoutingFold.cpp \
	widgets/routing/MultiConvolutionRoutingAdapter.cpp \
	widgets/routing/CrosspointMatrixRoutingRenderer.cpp \
	widgets/routing/StepListRoutingRenderer.cpp \
	widgets/routing/BlockChipRoutingRenderer.cpp \
	widgets/routing/LightTraceRoutingRenderer.cpp \
	widgets/routing/StudioRoutingModel.cpp \
	widgets/routing/HardwarePatchbayRoutingRenderer.cpp \
	widgets/MiddleClickTabWidget.cpp \
	import/ConfigDependencyScanner.cpp \
	import/ImportDialog.cpp \
	import/ImportExecutor.cpp \
	import/LegacyMigration.cpp \
	import/LegacyMigrationPolicy.cpp \
	widgets/MiddleClickTabBar.cpp

HEADERS  += \
	../helpers/FileSharingRetry.h \
	../helpers/LogHelper.h \
	../helpers/StringHelper.h \
	../helpers/RegistryHelper.h \
	../helpers/IRegistry.h \
	../helpers/RegistryTransaction.h \
	../helpers/WindowsVersion.h \
	../helpers/AudioEngineAccess.h \
	../helpers/InstallDiagnostics.h \
	../helpers/ServiceHelper.h \
	../helpers/ApoRegistration.h \
	../helpers/AudioFormatProbe.h \
	../helpers/VelopackBootstrap.h \
	../helpers/UpdateElevationPolicy.h \
	../parser/LogicalOperators.h \
	IFilterGUIFactory.h \
	FilterGUIFactoryRegistry.h \
	helpers/GUIHelper.h \
	helpers/QtAppBootstrap.h \
	stable.h \
	IFilterGUI.h \
	guis/PreampFilterGUI.h \
	guis/PreampFilterGUIFactory.h \
	guis/CommentFilterGUIFactory.h \
	guis/CommentFilterGUI.h \
	FilterTable.h \
	../filters/PreampCommand.h \
	../filters/PreampFilter.h \
	../filters/PreampFilterFactory.h \
	../helpers/MemoryHelper.h \
	FilterTableRow.h \
	FilterTemplate.h \
	guis/DeviceFilterGUI.h \
	guis/DeviceFilterGUIFactory.h \
	../devices/DeviceAPOInfo.h \
	guis/DeviceFilterGUIDialog.h \
	../filters/DeviceCommand.h \
	../filters/DeviceFilterFactory.h \
	guis/ChannelFilterGUIDialog.h \
	guis/ChannelFilterGUI.h \
	guis/ChannelFilterGUIFactory.h \
	guis/BiQuadFilterGUI.h \
	guis/BiQuadWidthConversion.h \
	../filters/BiQuad.h \
	../filters/BiQuadCommand.h \
	../filters/BiQuadFilter.h \
	../filters/BiQuadFilterFactory.h \
	guis/BiQuadFilterGUIFactory.h \
	guis/CopyFilterGUIFactory.h \
	guis/CopyFilterGUI.h \
	guis/CopyFilterGUIConnectionItem.h \
	guis/CopyFilterGUIChannelItem.h \
	../filters/CopyFilter.h \
	../filters/CopyFilterFactory.h \
	../engine/IFilter.h \
	../engine/IFilterFactory.h \
	guis/CopyFilterGUIScene.h \
	guis/CopyFilterGUIForm.h \
	guis/CopyFilterGUIRow.h \
	helpers/GUIChannelHelper.h \
	../helpers/ChannelHelper.h \
	guis/DelayFilterGUI.h \
	guis/DelayFilterGUIFactory.h \
	../filters/DelayCommand.h \
	../filters/DelayFilter.h \
	../filters/DelayFilterFactory.h \
	guis/IncludeFilterGUI.h \
	guis/IncludeFilterGUIFactory.h \
	widgets/ResizingLineEdit.h \
	widgets/ChannelGraphScene.h \
	widgets/ChannelGraphItem.h \
	guis/ChannelFilterGUIScene.h \
	guis/ChannelFilterGUIChannelItem.h \
	guis/GraphicEQFilterGUIFactory.h \
	guis/GraphicEQFilterGUI.h \
	../filters/GraphicEQCommand.h \
	../filters/GraphicEQFilter.h \
	../filters/GraphicEQFilterFactory.h \
	../libHybridConv-0.1.1/libHybridConv_eapo.h \
	../helpers/FftwPlanningPolicy.h \
	../helpers/GainIterator.h \
	guis/GraphicEQFilterGUIScene.h \
	widgets/FrequencyPlotView.h \
	widgets/FrequencyPlotHRuler.h \
	widgets/FrequencyPlotVRuler.h \
	widgets/FrequencyPlotItem.h \
	guis/GraphicEQFilterGUIItem.h \
	guis/GraphicEQFilterGUIView.h \
	widgets/FrequencyPlotScene.h \
	widgets/CompactToolBar.h \
	guis/ConvolutionFilterGUIFactory.h \
	guis/ConvolutionFilterGUI.h \
	guis/MultiConvolutionFilterGUIFactory.h \
	guis/MultiConvolutionFilterGUI.h \
	guis/SpatialFilterGUIFactory.h \
	guis/BassManagementFilterGUIFactory.h \
	helpers/AnalysisWorkerRecovery.h \
	helpers/ConvolutionPathHelper.h \
	helpers/DisableWheelFilter.h \
	widgets/EscapableLineEdit.h \
	../version.h \
	../stdafx.h \
	MainWindow.h \
	ConfigFileCodec.h \
	guis/StageFilterGUI.h \
	guis/StageFilterGUIFactory.h \
	guis/ExpressionFilterGUIFactory.h \
	widgets/ResizeCorner.h \
	analysis/AnalysisMetric.h \
	analysis/AnalysisResponse.h \
	analysis/ResponseCurveBuilder.h \
	../engine/FilterEngine.h \
	../engine/ConfigWatcher.h \
	../engine/FilterConfiguration.h \
	../filters/ChannelFilterFactory.h \
	../filters/ExpressionCommand.h \
	../filters/ExpressionFilterFactory.h \
	../filters/IfCommand.h \
	../filters/IfFilterFactory.h \
	../filters/StageCommand.h \
	../filters/StageFilterFactory.h \
	../filters/ConvolutionFilterFactory.h \
	../filters/IIRCommand.h \
	../filters/IIRFilter.h \
	../filters/IIRFilterFactory.h \
	../filters/IncludeCommand.h \
	../filters/IncludeFilterFactory.h \
	../filters/ChannelCommand.h \
	../filters/ChannelFilter.h \
	../filters/ConvolutionCommand.h \
	../filters/ConvolutionFilter.h \
	../filters/IrCache.h \
	../filters/bassManagement/BassManagementCommand.h \
	../filters/bassManagement/BassManagementFilter.h \
	../filters/bassManagement/BassManagementFilterFactory.h \
	../filters/HilbertCommand.h \
	../filters/HilbertFilter.h \
	../filters/HilbertFilterFactory.h \
	../filters/VelvetCommand.h \
	../filters/VelvetFilter.h \
	../filters/VelvetFilterFactory.h \
	../filters/velvet/Processor.h \
	../parser/RegexFunctions.h \
	../parser/RegistryFunctions.h \
	../parser/ParserExtensions.h \
	../parser/EngineParser.h \
	../parser/StringOperators.h \
	AnalysisThread.h \
	widgets/ExponentialSpinBox.h \
	FilterTableMimeData.h \
	CustomStyle.h \
	../devices/AbstractAPOInfo.h \
	../devices/VoicemeeterAPOInfo.h \
	../helpers/AbstractLibrary.h \
	../helpers/VSTPluginLibrary.h \
	guis/VSTPluginFilterGUI.h \
	guis/VSTPluginFilterGUIFactory.h \
	guis/VSTPluginFilterGUIDialog.h \
	../filters/VSTPluginCommand.h \
	../filters/VSTPluginFilter.h \
	../filters/VSTPluginFilterFactory.h \
	../helpers/VSTPluginInstance.h \
	guis/LoudnessCorrectionFilterGUI.h \
	guis/LoudnessCorrectionFilterGUIFactory.h \
	../filters/loudnessCorrection/LoudnessCorrectionCommand.h \
	../filters/loudnessCorrection/LoudnessCorrectionFilter.h \
	../filters/loudnessCorrection/LoudnessCorrectionFilterFactory.h \
	../filters/loudnessCorrection/VolumeController.h \
	guis/LoudnessCorrectionFilterGUIDialog.h \
	helpers/CrashHandler.h \
	diagnostics/ToolbarPixelProbe.h \
	diagnostics/SkinSwitchStorm.h \
	helpers/QtSndfileHandle.h \
	SkinGallery.h \
	SkinTokens.h \
	SkinManager.h \
	skins/ISkin.h \
	skins/pickers/MatrixFilterPicker.h \
	skins/RackChrome.h \
	skins/Skins.h \
	skins/SkinFileIcons.h \
	skins/SkinChromeOverlay.h \
	skins/SkinPaint.h \
	skins/SkinSupport.h \
	skins/SkinThemeData.h \
	skins/pickers/StudioFilterPicker.h \
	skins/pickers/MinimalFilterPicker.h \
	skins/pickers/SoftFilterPicker.h \
	skins/pickers/RackFilterPicker.h \
	widgets/AddCardRow.h \
	widgets/ActivatableListChrome.h \
	widgets/AudioKnob.h \
	widgets/FilterInsertSeam.h \
	widgets/GraphicEQPlotWidget.h \
	widgets/FlowLayout.h \
	widgets/CommandRowFrame.h \
	widgets/SkinComboBox.h \
	widgets/cards/ChannelCardEditor.h \
	widgets/cards/ChannelSelectionModel.h \
	widgets/cards/ConvolutionCardEditor.h \
	widgets/cards/BassManagementCardEditor.h \
	widgets/cards/BassManagementCardView.h \
	widgets/cards/MultiConvolutionCardEditor.h \
	widgets/cards/CommentCardEditor.h \
	widgets/cards/DeviceCardEditor.h \
	widgets/cards/StageCardEditor.h \
	widgets/cards/StageSelectionModel.h \
	widgets/cards/DeviceSelectionModel.h \
	widgets/cards/FilterCardEditorFactory.h \
	widgets/cards/DelayCardEditor.h \
	widgets/cards/FilterCardEditorRegistry.h \
	widgets/cards/FileReferenceController.h \
	widgets/cards/GraphicEQCardEditor.h \
	widgets/cards/IIRCardEditor.h \
	widgets/cards/AllPassCardEditor.h \
	widgets/cards/HilbertCardEditor.h \
	widgets/cards/VelvetCardEditor.h \
	widgets/cards/VelvetImpulsePreview.h \
	analysis/AnalysisViewController.h \
	widgets/cards/IncludeCardEditor.h \
	widgets/cards/PreampCardEditor.h \
	widgets/cards/ScalarKnobCardEditor.h \
	widgets/cards/ReferenceCardView.h \
	widgets/cards/DefaultReferenceCardView.h \
	widgets/cards/VSTCardEditor.h \
	skins/cards/StudioReferenceCardView.h \
	skins/cards/StudioBassManagementCardView.h \
	skins/cards/MinimalBassManagementCardView.h \
	skins/cards/SoftBassManagementCardView.h \
	skins/cards/RackBassManagementCardView.h \
	skins/cards/MatrixBassManagementCardView.h \
	skins/cards/MinimalReferenceCardView.h \
	skins/cards/SoftReferenceCardView.h \
	skins/cards/RackReferenceCardView.h \
	skins/cards/MatrixReferenceCardView.h \
	widgets/ElidedLabel.h \
	widgets/EditableValue.h \
	widgets/EditableValueText.h \
	widgets/ChBadge.h \
	widgets/EqGraphView.h \
	widgets/SegmentedControl.h \
	widgets/FilterCardModel.h \
	widgets/FilterCardRow.h \
	widgets/FilterListModel.h \
	widgets/FilterListUndo.h \
	widgets/FilterPickerView.h \
	widgets/UpdateToast.h \
	widgets/MainToolbarKit.h \
	widgets/ValueScrubBox.h \
	widgets/DialogChrome.h \
	widgets/TitleBar.h \
	widgets/routing/CopyRoutingAdapter.h \
	widgets/routing/RoutingFold.h \
	widgets/routing/MultiConvolutionRoutingAdapter.h \
	widgets/routing/IRoutingRenderer.h \
	widgets/routing/CrosspointMatrixRoutingRenderer.h \
	widgets/routing/StepListRoutingRenderer.h \
	widgets/routing/BlockChipRoutingRenderer.h \
	widgets/routing/LightTraceRoutingRenderer.h \
	widgets/routing/StudioRoutingModel.h \
	widgets/routing/HardwarePatchbayRoutingRenderer.h \
	widgets/MiddleClickTabWidget.h \
	import/ConfigDependencyScanner.h \
	import/ImportDialog.h \
	import/ImportExecutor.h \
	import/ImportManifest.h \
	import/LegacyMigration.h \
	import/LegacyMigrationPolicy.h \
	widgets/MiddleClickTabBar.h

FORMS    += \
	guis/PreampFilterGUI.ui \
	guis/CommentFilterGUI.ui \
	FilterTableRow.ui \
	guis/DeviceFilterGUI.ui \
	guis/DeviceFilterGUIDialog.ui \
	guis/ChannelFilterGUIDialog.ui \
	guis/ChannelFilterGUI.ui \
	guis/BiQuadFilterGUI.ui \
	guis/CopyFilterGUI.ui \
	guis/CopyFilterGUIRow.ui \
	guis/DelayFilterGUI.ui \
	guis/IncludeFilterGUI.ui \
	guis/GraphicEQFilterGUI.ui \
	guis/ConvolutionFilterGUI.ui \
	MainWindow.ui \
	guis/StageFilterGUI.ui \
	guis/VSTPluginFilterGUI.ui \
	guis/VSTPluginFilterGUIDialog.ui \
	guis/LoudnessCorrectionFilterGUI.ui \
	guis/LoudnessCorrectionFilterGUIDialog.ui

# Dependency paths with environment variable support and fallbacks
LIBSNDFILE_INCLUDE = $$(LIBSNDFILE_INCLUDE)
isEmpty(LIBSNDFILE_INCLUDE) {
	LIBSNDFILE_INCLUDE = $$PWD/../deps/libsndfile/include
}
LIBSNDFILE_LIB = $$(LIBSNDFILE_LIB)
isEmpty(LIBSNDFILE_LIB) {
	LIBSNDFILE_LIB = $$PWD/../deps/libsndfile/build/Release
}

FFTW_INCLUDE = $$(FFTW_INCLUDE)
isEmpty(FFTW_INCLUDE) {
	FFTW_INCLUDE = $$PWD/../deps/fftw/include
}

FFTW_LIB = $$(FFTW_LIB)
isEmpty(FFTW_LIB) {
	FFTW_LIB = $$PWD/../deps/fftw/Release
}

MUPARSERX_INCLUDE = $$(MUPARSERX_INCLUDE)
isEmpty(MUPARSERX_INCLUDE) {
	MUPARSERX_INCLUDE = $$PWD/../deps/muparserx/parser
}

MUPARSERX_LIB = $$(MUPARSERX_LIB)
isEmpty(MUPARSERX_LIB) {
	MUPARSERX_LIB = $$PWD/../deps/muparserx/build/Release
}

VELOPACK_INCLUDE = $$(VELOPACK_INCLUDE)
isEmpty(VELOPACK_INCLUDE) {
	VELOPACK_INCLUDE = $$PWD/../deps/velopack_libc/include
}

VST3_SDK = $$(VST3_SDK)
isEmpty(VST3_SDK) {
	VST3_SDK = $$PWD/../deps/vst3sdk
}

# The Editor compiles the Common DSP sources directly (PreampFilter, BiQuadFilter,
# libHybridConv, FilterEngine.Process), which now include hwy/highway.h. Mirror
# Common.vcxproj's HIGHWAY_INCLUDE so qmake finds the Highway headers.
HIGHWAY_INCLUDE = $$(HIGHWAY_INCLUDE)
isEmpty(HIGHWAY_INCLUDE) {
	HIGHWAY_INCLUDE = $$PWD/../deps/highway
}

VELOPACK_LIB = $$(VELOPACK_LIB)
isEmpty(VELOPACK_LIB) {
	VELOPACK_LIB = $$PWD/../deps/velopack_libc/lib
}

# velopack_libc ships per-arch import libs in one folder; pick the matching one.
contains(QT_ARCH, arm64) {
	VELOPACK_IMPORT_LIB = velopack_libc_win_arm64_msvc.dll.lib
} else {
	VELOPACK_IMPORT_LIB = velopack_libc_win_x64_msvc.dll.lib
}

# The Editor's auto-update needs to know which release channel it was built for so
# UpdateManager fetches the matching feed (mirrors UpdateChecker.pro).
!isEmpty(EAPO_UPDATE_CHANNEL) {
	DEFINES += EAPO_UPDATE_CHANNEL=\\\"$$EAPO_UPDATE_CHANNEL\\\"
}

INCLUDEPATH += $$PWD/.. $$PWD/../BassManagementCore/include $$LIBSNDFILE_INCLUDE $$FFTW_INCLUDE $$MUPARSERX_INCLUDE $$VELOPACK_INCLUDE $$VST3_SDK $$HIGHWAY_INCLUDE
LIBS += user32.lib advapi32.lib version.lib ole32.lib Shlwapi.lib authz.lib crypt32.lib dbghelp.lib winmm.lib sndfile.lib libfftw3.lib $$VELOPACK_IMPORT_LIB

build_pass:CONFIG(debug, debug|release) {
	LIBS += muparserxd.lib
} else {
	LIBS += muparserx.lib
}

include($$PWD/../common.pri)
QMAKE_LIBDIR += $$LIBSNDFILE_LIB $$FFTW_LIB $$MUPARSERX_LIB $$VELOPACK_LIB

# The Editor deliberately does NOT link Common.lib: every engine source it
# needs is compiled directly (SOURCES above) under the Editor's own SIMD
# flags, so the analysis panel's FilterEngine runs with the variant's /arch.
# Linking the MSBuild lib on top used to silently decide symbol ownership by
# link order and let a missing SOURCES entry be papered over by a copy built
# with different flags. A missing engine file now fails the link loudly -
# add it here AND to Common.vcxproj. (audit #146 TD013, maintainer decision
# 2026-07-04)

RESOURCES += \
	Editor.qrc
TRANSLATIONS += translations/Editor_de.ts \
	translations/Editor_fr.ts \
	translations/Editor_ko.ts \
	translations/Editor_zh_CN.ts

RC_FILE = Editor.rc

DISTFILES += ../uncrustify.cfg
