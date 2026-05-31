/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Studio (glass) skin's Copy renderer: an interactive node / signal-flow graph
	of input channels, output channels and weighted connections. Reuses the
	proven CopyFilterGUIScene (drag to connect, double-click to set a factor) so
	editing is fully supported; the glass skin styles it with soft, glowing
	connections. Best when the user wants to trace how a signal flows.
*/

#pragma once

#include "IRoutingRenderer.h"
#include "filters/CopyFilter.h"

class CopyFilterGUIScene;
class QGraphicsView;

class CurvedNodeView : public RoutingView
{
	Q_OBJECT

public:
	CurvedNodeView(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames, QWidget* parent);

	std::vector<Assignment> assignments() const override;

private:
	CopyFilterGUIScene* scene = nullptr;
	QGraphicsView* view = nullptr;
};

class CurvedNodeRoutingRenderer : public IRoutingRenderer
{
public:
	RoutingView* create(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames, QWidget* parent) override;
	const char* id() const override { return "curved-node"; }
};
