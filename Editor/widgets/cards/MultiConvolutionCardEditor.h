/*
    This file is part of EqualizerAPO-XT, a system-wide equalizer.

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

#include <vector>

#include "Editor/IFilterGUI.h"
#include "filters/MultiConvolutionCommand.h"

class FilterTable;
class FileReferenceController;
class QLabel;
class QToolButton;
class QVBoxLayout;
class ReferenceCardView;
class RoutingView;

// Modern card body for a "MultiConvolution:" line. Two stacked parts: the
// named impulse-response reference (same skin view, readout and import
// affordance as ConvolutionCardEditor) and, under it, the channel mapping as
// the active skin's routing view - the same per-skin presentation Copy gets,
// but with a fixed source side (the IR file's channels "0".."N-1") and no
// factors. Output ports are the channels in scope at this row plus any
// virtual channel the user adds; edits serialize back through
// MultiConvolutionRoutingAdapter into the "L=0+1 R=2+3 <file>" mapping form.
class MultiConvolutionCardEditor : public IFilterGUI
{
	Q_OBJECT

public:
	MultiConvolutionCardEditor(FilterTable* filterTable,
		const std::vector<MultiConvolutionCommand::Mapping>& mappings,
		const QString& path, QWidget* parent = nullptr);

	void store(QString& command, QString& parameters) override;
	// The channels that exist at this row seed the routing view's output side.
	void configureChannels(std::vector<std::wstring>& channelNames) override;

private slots:
	void chooseFile();
	void pathCommitted(const QString& text);
	void importToConfig();
	void routingEdited();
	void addOutputChannel();

private:
	QString resolvedAbsolutePath() const;
	unsigned currentDeviceSampleRate() const;
	void updateFileInfo();
	void rebuildRoutingView();

	FilterTable* filterTable = nullptr;
	FileReferenceController* reference = nullptr;
	// The mapping state of the line; the routing view edits it. A simple-form
	// line stays simple until the routing is touched.
	std::vector<MultiConvolutionCommand::Mapping> mappings;
	// Channels in scope at this row (configureChannels) and virtual outputs the
	// user added in this session; both seed the routing view's output side.
	std::vector<std::wstring> rowChannels;
	std::vector<std::wstring> extraTargets;
	// Channel count of the resolved IR file; 0 while missing/unreadable, in
	// which case the mapping is not editable (an edit could not know what
	// "every channel" means and would persist a wrong expansion).
	int fileChannelCount = 0;

	ReferenceCardView* view = nullptr;
	// The whole mapping block (caption, + button, routing view). Hidden while
	// no readable file is selected: the reference view's own status grammar
	// already says what is missing, and a mapping that cannot be edited would
	// only dangle under it as loose text.
	QWidget* mappingArea = nullptr;
	QVBoxLayout* routingLayout = nullptr;
	QLabel* mappingCaption = nullptr;
	RoutingView* routingView = nullptr;
	QToolButton* addChannelButton = nullptr;
	QToolButton* chooseButton = nullptr;
	QToolButton* importButton = nullptr;
};
