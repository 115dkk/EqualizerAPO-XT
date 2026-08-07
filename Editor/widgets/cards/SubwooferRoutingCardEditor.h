#pragma once

#include <optional>
#include <string>
#include <vector>

#include "SubwooferRouting/State.h"
#include "Editor/IFilterGUI.h"
#include "filters/subwooferRouting/SubwooferRoutingCommand.h"

class SubwooferRoutingCardView;
class FilterTable;
class QToolButton;

namespace subwooferroutingeditor
{
subroute::SubwooferRoutingState buildDefaultState(
	const std::vector<std::wstring>& deviceChannels);
}

class SubwooferRoutingCardEditor : public IFilterGUI
{
	Q_OBJECT

public:
	SubwooferRoutingCardEditor(FilterTable* filterTable,
		const SubwooferRoutingCommand& command, const QString& configPath,
		unsigned deviceSampleRate, QWidget* parent = nullptr);

	SubwooferRoutingCardEditor(FilterTable* filterTable,
		const SubwooferRoutingCommand& command, const QString& configPath,
		unsigned deviceSampleRate, const QString& originalParameters,
		const QString& parseError, QWidget* parent = nullptr);

	void store(QString& command, QString& parameters) override;
	void configureChannels(
		std::vector<std::wstring>& channelNames) override;

protected:
	virtual void openFullEditor();

private:
	void loadCommand(const SubwooferRoutingCommand& command,
		const QString& parseError);
	void applyPreset(const std::string& presetId);
	void refreshCard();
	QString resolvedProfilePath(const QString& writtenPath) const;

	FilterTable* filterTable = nullptr;
	QString configPath;
	unsigned deviceSampleRate = 0;
	SubwooferRoutingCommand::Form form =
		SubwooferRoutingCommand::Form::State;
	std::optional<subroute::SubwooferRoutingState> currentState;
	QString originalStatePayload;
	QString originalProfileParameters;
	QString profilePath;
	bool profileMissing = false;
	QString loadError;
	SubwooferRoutingCardView* view = nullptr;
	QToolButton* openButton = nullptr;
	QToolButton* presetButton = nullptr;
};
