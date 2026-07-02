/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	A skin contributes one IRoutingRenderer. Given the same routing data
	(std::vector<Assignment>), each skin's renderer produces a completely
	different RoutingView widget: a crosspoint matrix, a step list, a node
	graph, soft chips, a hardware patch-bay, and so on. The routing data
	therefore lives in one form and only the presentation differs per skin.
	Copy uses the symmetric default; MultiConvolution rides the same renderers
	with a RoutingPortModel that fixes the source side to the IR file's
	channels and locks factors to unity.
*/

#pragma once

#include <vector>
#include <QStringList>
#include <QWidget>

#include "filters/CopyFilter.h"

// Base class for every per-skin routing widget. The host (FilterCardRow for
// Copy, MultiConvolutionCardEditor for MultiConvolution) connects
// routingChanged() and reads back assignments() to serialise edits.
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

// How the routing's source side is populated and edited. The default
// reproduces Copy: sources grow from the assignments plus every device
// channel, and every connection carries an editable factor. MultiConvolution
// supplies a fixed source-port list (the impulse-response file's channels,
// labelled "0".."N-1") and locks factors to unity, so interaction reduces to
// connect / disconnect.
struct RoutingPortModel
{
	// When non-empty, the source side is exactly this list (in order); the view
	// offers no other sources and no way to add one.
	QStringList fixedSources;

	// False hides factor labels and editors; connections are unity only.
	bool allowFactors = true;

	bool fixedSourceMode() const { return !fixedSources.isEmpty(); }
};

class IRoutingRenderer
{
public:
	virtual ~IRoutingRenderer() = default;

	// Build a fresh routing view for the given assignments. channelNames is the
	// channel layout in scope (used for target seeding / labelling); it may be
	// empty when channels are not yet known. portModel selects between Copy's
	// symmetric behaviour (default) and a fixed-source command such as
	// MultiConvolution.
	virtual RoutingView* create(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
		QWidget* parent) = 0;

	// Short identifier, mainly for diagnostics.
	virtual const char* id() const = 0;
};
