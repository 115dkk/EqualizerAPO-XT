/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	StudioRoutingModel is the widget-free working model behind the studio
	skin's Light Trace routing view: input ports (top row), output ports
	(bottom row) and the traces between them, with the seeding and alias
	rules ported from the legacy CopyFilterGUIScene::load so existing config
	strings resolve to the same chips ("LFE"/"SUB" aliases, 1-based numeric
	positions, the constant-input port). The model preserves load order, so a
	parse -> edit-free serialize round-trip is stable. EditorLogicTests pins
	these bytes.
*/

#pragma once

#include <vector>
#include <QString>
#include <QStringList>
#include <QVector>

#include "filters/CopyFilter.h"

class StudioRoutingModel
{
public:
	// The view maps RoutingPortModel (IRoutingRenderer.h) onto this so the
	// model stays free of widget headers.
	struct PortConfig
	{
		QStringList fixedSources;
		bool allowFactors = true;
		bool fixedSourceMode() const { return !fixedSources.isEmpty(); }
	};

	struct Trace
	{
		int input = -1;
		int output = -1;
		double factor = 1.0;
		bool isDecibel = false;
	};

	void load(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames, const PortConfig& config);

	// Display names. In Copy mode the last input port may be the constant
	// port (empty name, drawn as "const"); constInput() tells it apart.
	const QStringList& inputPorts() const;
	const QStringList& outputPorts() const;
	bool constInput(int index) const;

	const QVector<Trace>& traces() const;
	bool allowFactors() const;

	// Returns the (existing or new) output port index; empty names are
	// rejected with -1. New names become virtual output chips.
	int addOutput(const QString& name);

	// How many ports were seeded from the device channel layout; ports beyond
	// these counts came from the config line or the user (virtual channels).
	// The view's channel fold keys off this split.
	int seededInputCount() const;
	int seededOutputCount() const;

	// Removes the named channel entirely: its output port, its input port and
	// every trace touching either (case-insensitive). Returns true when a
	// trace was removed, i.e. the serialized line changes.
	bool removeChannel(const QString& name);

	// Connects input -> output at unity gain (0.0 from the constant port,
	// matching the legacy scene's constant-summand behaviour).
	void addTrace(int input, int output);

	// Moves every existing endpoint on one side from one port to another.
	// This is the model operation behind dragging a connected chip along its
	// own row to correct a mistaken source or target. Factors and opposite
	// endpoints are preserved. Returns false when nothing was connected.
	bool rewirePort(bool inputSide, int fromPort, int toPort);
	void removeTrace(int index);

	// The factor editor's commit: empty text removes the trace, a "db"
	// suffix (any case) sets decibel mode, ',' reads as '.'; unparsable
	// text leaves the trace unchanged. Ignored when factors are locked.
	void setFactorText(int index, const QString& text);

	// Assignments in load order (then first-connection order for outputs
	// that gained traces later); outputs whose sum is empty are skipped,
	// matching the serializer.
	std::vector<Assignment> assignments() const;

private:
	int resolveInput(const QString& written);
	int resolveOutput(const QString& written);

	PortConfig config;
	QStringList inputs;
	QStringList outputs;
	// How many ports were seeded from the channel layout; numeric 1-based
	// positions resolve only within this prefix.
	int seededInputs = 0;
	int seededOutputs = 0;
	int constInputIndex = -1;
	QVector<Trace> traceList;
	// Output emit order: load-order targets first, later-connected outputs
	// appended when their first trace arrives.
	QVector<int> emitOrder;
};
