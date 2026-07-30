#pragma once

#include <optional>
#include <string>
#include <vector>

#include "BassManagement/State.h"
#include "Editor/IFilterGUI.h"
#include "filters/bassManagement/BassManagementCommand.h"

class BassManagementCardView;
class FilterTable;
class QToolButton;

namespace bassmanagementeditor
{
bassmgmt::BassManagementState buildDefaultState(
	const std::vector<std::wstring>& deviceChannels);
}

class BassManagementCardEditor : public IFilterGUI
{
	Q_OBJECT

public:
	BassManagementCardEditor(FilterTable* filterTable,
		const BassManagementCommand& command, const QString& configPath,
		unsigned deviceSampleRate, QWidget* parent = nullptr);

	BassManagementCardEditor(FilterTable* filterTable,
		const BassManagementCommand& command, const QString& configPath,
		unsigned deviceSampleRate, const QString& originalParameters,
		const QString& parseError, QWidget* parent = nullptr);

	void store(QString& command, QString& parameters) override;
	void configureChannels(
		std::vector<std::wstring>& channelNames) override;

protected:
	virtual void openFullEditor();

private:
	void loadCommand(const BassManagementCommand& command,
		const QString& parseError);
	void applyPreset(const std::string& presetId);
	void refreshCard();
	QString resolvedProfilePath(const QString& writtenPath) const;

	FilterTable* filterTable = nullptr;
	QString configPath;
	unsigned deviceSampleRate = 0;
	BassManagementCommand::Form form =
		BassManagementCommand::Form::State;
	std::optional<bassmgmt::BassManagementState> currentState;
	QString originalStatePayload;
	QString originalProfileParameters;
	QString profilePath;
	bool profileMissing = false;
	QString loadError;
	BassManagementCardView* view = nullptr;
	QToolButton* openButton = nullptr;
	QToolButton* presetButton = nullptr;
};
