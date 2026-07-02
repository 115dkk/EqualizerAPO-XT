/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	A skin contributes one IRoutingRenderer. Given the same routing data
	(std::vector<Assignment>), each skin's renderer produces a completely
	different RoutingView widget: a crosspoint matrix, a step list, a node
	graph, soft chips, a hardware patch-bay, and so on. The Copy data therefore
	lives in one form and only the presentation differs per skin.
*/

#pragma once

#include <vector>
#include <QWidget>

#include "filters/CopyFilter.h"

// Base class for every per-skin Copy routing widget. The host (FilterCardRow)
// connects routingChanged() and reads back assignments() to serialise edits.
class RoutingView : public QWidget
{
	Q_OBJECT

public:
	explicit RoutingView(QWidget* parent = nullptr) : QWidget(parent) {}

	// Current routing as edited by the user. Reconstructed from the view's own
	// working state so a serialise round-trip reproduces the edits.
	virtual std::vector<Assignment> assignments() const = 0;

signals:
	// Emitted whenever the user changes the routing inside the view.
	void routingChanged();
};

class IRoutingRenderer
{
public:
	virtual ~IRoutingRenderer() = default;

	// Build a fresh routing view for the given assignments. channelNames is the
	// current device channel layout (used for ordering / labelling); it may be
	// empty when channels are not yet known.
	virtual RoutingView* create(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames, QWidget* parent) = 0;

	// Short identifier, mainly for diagnostics.
	virtual const char* id() const = 0;
};
