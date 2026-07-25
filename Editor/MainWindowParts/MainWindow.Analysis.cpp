#include <sstream>
#include <QDrag>
#include <QElapsedTimer>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QStandardItemModel>
#include <QStringBuilder>
#include <QStyle>
#include <QScrollArea>
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
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
#include "ui_MainWindow.h"

using std::find;
using std::list;
using std::set;
using std::shared_ptr;
using std::string;
using std::stringstream;
using std::vector;
using std::wstring;


void MainWindow::instantModeEnabled(bool enabled)
{
	if (enabled)
	{
		forEachFilterTable([&](int i, FilterTable* filterTable) {
			if (filterTable->getConfigPath().length() > 0)
			{
				save(filterTable, filterTable->getConfigPath());

				QString tabText = ui->tabWidget->tabText(i);
				if (tabText.endsWith('*'))
					ui->tabWidget->setTabText(i, tabText.left(tabText.length() - 1));
			}
		});
		updateDirtyStatus();
	}
}

void MainWindow::on_tabWidget_currentChanged(int index)
{
	updateDirtyStatus();
	updateUndoRedoActions();
	startAnalysis();
}

void MainWindow::on_startFromComboBox_activated(int index)
{
	startAnalysis();
}

void MainWindow::on_analysisChannelComboBox_activated(int index)
{
	startAnalysis();
}

void MainWindow::on_resolutionSpinBox_valueChanged(int value)
{
	startAnalysis();
}

void MainWindow::updateAnalysisPanel()
{
	auto result = analysisThread->lockResult();
	const std::shared_ptr<const AnalysisResponse> response = result.response();
	const int sampleRate = static_cast<int>(response->sampleRate);
	const int latency = response->latencyFrames;
	const QString errorText = result.errorText();
	analysisPlotScene->setResponse(*response);
	if (eqGraphView != nullptr)
		eqGraphView->setResponse(response, ui->analysisChannelComboBox->currentText());

	// Hand the engine's per-line load facts to every open tab whose file took
	// part in this load. A tab whose file was
	// not part of the analyzed chain receives an empty set, which clears any
	// stale facts from a previous device/config selection.
	{
		const std::vector<ConfigLoadTraceEntry>& trace = result.loadTrace();
		forEachFilterTable([&](int, FilterTable* filterTable) {
			if (filterTable->getConfigPath().isEmpty())
				return;
			const QString tabPath = QDir::toNativeSeparators(filterTable->getConfigPath());
			QVector<ConfigLoadTraceEntry> tabFacts;
			for (const ConfigLoadTraceEntry& entry : trace)
			{
				const QString entryPath = QDir::toNativeSeparators(QString::fromStdWString(entry.file));
				if (entryPath.compare(tabPath, Qt::CaseInsensitive) == 0)
					tabFacts.append(entry);
			}
			filterTable->setLoadTraceFacts(tabFacts);
		});
	}

	auto setSeverity = [](QLabel* label, const char* severity)
	{
		if (label->property("severity").toString() == QLatin1String(severity))
			return;
		label->setProperty("severity", QString::fromLatin1(severity));
		label->style()->unpolish(label);
		label->style()->polish(label);
		label->update();
	};

	if (!errorText.isEmpty())
	{
		ui->peakGainValueLabel->setText(tr("Analysis failed"));
		ui->peakGainValueLabel->setToolTip(errorText);
		setSeverity(ui->peakGainValueLabel, "critical");
		const QString unavailable = QString::fromUtf8("\xE2\x80\x94");
		ui->latencyValueLabel->setText(unavailable);
		ui->initTimeValueLabel->setText(unavailable);
		ui->cpuUsageValueLabel->setText(unavailable);
		setSeverity(ui->cpuUsageValueLabel, "normal");
		return;
	}
	ui->peakGainValueLabel->setToolTip(QString());

	double peakGain = result.peakGain();
	ui->peakGainValueLabel->setText(tr("%0 dB").arg(peakGain, 0, 'f', 1));
	setSeverity(ui->peakGainValueLabel, peakGain > 0 ? "critical" : "normal");

	int processedFrames = result.processedFrames();
	if (sampleRate <= 0 || processedFrames <= 0)
	{
		ui->latencyValueLabel->setText(QString::fromUtf8("\xE2\x80\x94"));
		ui->cpuUsageValueLabel->setText(QString::fromUtf8("\xE2\x80\x94"));
		setSeverity(ui->cpuUsageValueLabel, "normal");
		ui->initTimeValueLabel->setText(tr("%0 ms").arg(result.initializationTime(), 0, 'f', 1));
		return;
	}

	ui->latencyValueLabel->setText(tr("%0 ms (%1 s.)").arg(latency * 1000.0 / sampleRate, 0, 'f', 1).arg(latency));

	ui->initTimeValueLabel->setText(tr("%0 ms").arg(result.initializationTime(), 0, 'f', 1));

	double cpuUsage = result.processingTime() * 100.0 / (processedFrames * 1000.0 / sampleRate);
	ui->cpuUsageValueLabel->setText(tr("%0 %").arg(cpuUsage, 0, 'f', 1));
	setSeverity(ui->cpuUsageValueLabel, cpuUsage >= 50 ? "critical" : (cpuUsage >= 20 ? "warning" : "normal"));

}


void MainWindow::startAnalysis()
{
	if (!ui->analysisDockWidget->isVisible())
		return;

	// Debounce: instant-mode saves, slider drags, channel/resolution changes, and
	// tab switches each call startAnalysis. AnalysisThread::setParameters already
	// keeps only the most recent parameters via wakeAll, but the actual analysis
	// run (engine init + up to ten seconds of impulse processing) does not abort
	// mid-flight. Adding a short coalesce window cuts redundant runs while a
	// user is still dragging.
	static constexpr int kAnalysisDebounceMs = 120;
	if (analysisDebounceTimer == nullptr)
	{
		analysisDebounceTimer = new QTimer(this);
		analysisDebounceTimer->setSingleShot(true);
		connect(analysisDebounceTimer, &QTimer::timeout, this, &MainWindow::executeStartAnalysis);
	}
	analysisDebounceTimer->start(kAnalysisDebounceMs);
}

void MainWindow::executeStartAnalysis()
{
	if (!ui->analysisDockWidget->isVisible())
		return;

	shared_ptr<AbstractAPOInfo> selectedDevice;

	int channelMask;
	getDeviceAndChannelMask(&selectedDevice, &channelMask);

	if (selectedDevice != nullptr)
	{
		QString configPath;

		if (ui->startFromComboBox->currentIndex() == 1)
		{
			if (FilterTable* filterTable = currentFilterTable())
			{
				if (filterTable->getConfigPath().length() > 0)
					configPath = filterTable->getConfigPath();
			}
		}

		if (configPath.isEmpty())
			configPath = configDir.absoluteFilePath("config.txt");
		configPath = QDir::toNativeSeparators(configPath);

		analysisThread->setParameters(selectedDevice, channelMask, ui->analysisChannelComboBox->currentIndex(), configPath, ui->resolutionSpinBox->value());
	}
}

