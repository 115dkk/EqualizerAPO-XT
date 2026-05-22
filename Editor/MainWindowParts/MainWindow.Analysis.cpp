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
		for (int i = 0; i < ui->tabWidget->count(); i++)
		{
			QScrollArea* scrollArea = qobject_cast<QScrollArea*>(ui->tabWidget->widget(i));
			FilterTable* filterTable = qobject_cast<FilterTable*>(scrollArea->widget());

			if (filterTable->getConfigPath().length() > 0)
			{
				save(filterTable, filterTable->getConfigPath());

				QString tabText = ui->tabWidget->tabText(i);
				if (tabText.endsWith('*'))
					ui->tabWidget->setTabText(i, tabText.left(tabText.length() - 1));
			}
		}
	}
}

void MainWindow::on_tabWidget_currentChanged(int index)
{
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
	analysisThread->beginGetResult();
	int sampleRate = analysisThread->getFreqDataSampleRate();
	int latency = analysisThread->getLatency();
	analysisPlotScene->setFreqData(analysisThread->getFreqData(), analysisThread->getFreqDataLength(), sampleRate);
	if (eqGraphView != nullptr)
		eqGraphView->setNodes(analysisPlotScene->getNodes(), static_cast<unsigned>(sampleRate), ui->analysisChannelComboBox->currentText());

	auto setSeverity = [](QLabel* label, const char* severity)
	{
		if (label->property("severity").toString() == QLatin1String(severity))
			return;
		label->setProperty("severity", QString::fromLatin1(severity));
		label->style()->unpolish(label);
		label->style()->polish(label);
		label->update();
	};

	double peakGain = analysisThread->getPeakGain();
	ui->peakGainValueLabel->setText(tr("%0 dB").arg(peakGain, 0, 'f', 1));
	setSeverity(ui->peakGainValueLabel, peakGain > 0 ? "critical" : "normal");

	ui->latencyValueLabel->setText(tr("%0 ms (%1 s.)").arg(latency * 1000.0 / sampleRate, 0, 'f', 1).arg(latency));

	ui->initTimeValueLabel->setText(tr("%0 ms").arg(analysisThread->getInitializationTime(), 0, 'f', 1));

	double cpuUsage = analysisThread->getProcessingTime() * 100.0 / (analysisThread->getProcessedFrames() * 1000.0 / sampleRate);
	ui->cpuUsageValueLabel->setText(tr("%0 %").arg(cpuUsage, 0, 'f', 1));
	setSeverity(ui->cpuUsageValueLabel, cpuUsage >= 50 ? "critical" : (cpuUsage >= 20 ? "warning" : "normal"));

	analysisThread->endGetResult();
}


void MainWindow::startAnalysis()
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
			QScrollArea* scrollArea = qobject_cast<QScrollArea*>(ui->tabWidget->currentWidget());
			if (scrollArea != nullptr)
			{
				FilterTable* filterTable = qobject_cast<FilterTable*>(scrollArea->widget());

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

