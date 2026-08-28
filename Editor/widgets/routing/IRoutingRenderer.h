/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

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
#include <QLayout>
#include <QStringList>
#include <QWidget>

#include "filters/CopyFilter.h"

struct SkinTokens;

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

	// Offscreen gallery hook (SkinGallery): drive a state the renderer cannot
	// reach without a real pointer. Supported states: "expanded" reveals the
	// folded channels, "addChannel" opens the add-channel editor with a sample
	// name typed in, "editSource:<target>" opens a view's inline source
	// editor on that target's first summand. Default no-op for renderers
	// without those affordances.
	virtual void galleryShowcase(const QString& state) { Q_UNUSED(state); }

	// The sources a view offers to add to target's sum, in the order its
	// menu or hint lists them; empty for views that have no per-target menu
	// (the matrix-shaped views expose every column at once). Lets the
	// routing-edit gate hold the hint list without driving a modal menu.
	virtual QStringList sourceCandidates(const QString& target) const
	{
		Q_UNUSED(target);
		return QStringList();
	}

	// Add source at unity to target's sum: the commit behind a per-target
	// add menu. False when target is not a row of this view, the source is
	// already in the sum, or the view has no such operation; on success the
	// view emits routingChanged.
	virtual bool connectSource(const QString& target, const QString& source)
	{
		Q_UNUSED(target);
		Q_UNUSED(source);
		return false;
	}

protected:
	// The Copy card hosts this view in a height-pinning scroll wrapper that
	// follows the content by watching its Resize events
	// (FilterCardRow::watchEditorScroll). A pure size-hint change never
	// reaches that wrapper - the viewport chain has no layout to carry the
	// LayoutRequest - so edits that change the content size (the channel
	// fold above all) must resize to the new hint explicitly; the wrapper
	// then re-pins its height.
	void syncSizeToHint()
	{
		updateGeometry();
		resize(sizeHint());

		// MultiConvolution nests the routing view two layouts below the editor
		// widget that FilterCardRow's height-pinning scroll watches. Resizing
		// only this leaf leaves those layouts' cached size hints unchanged, so
		// an expanded target fold is painted into the old collapsed height and
		// clipped. Invalidate the size-hint chain and notify each ancestor;
		// the watched editor then emits a fresh LayoutRequest and the card
		// scroll repins itself to the expanded content.
		for (QWidget* ancestor = parentWidget(); ancestor != nullptr;
			ancestor = ancestor->parentWidget())
		{
			if (ancestor->layout() != nullptr)
				ancestor->layout()->invalidate();
			ancestor->updateGeometry();
		}
	}

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
	// MultiConvolution. tokens is the building skin's palette; the views used
	// to pull SkinManager::instance() mid-paint instead (audit #275 B1), and
	// rows rebuild on every skin switch, so construction injection is safe.
	virtual RoutingView* create(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
		QWidget* parent, const SkinTokens& tokens) = 0;

	// Short identifier, mainly for diagnostics.
	virtual const char* id() const = 0;
};
