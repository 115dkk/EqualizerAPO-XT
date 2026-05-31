/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	CopyRoutingAdapter is the single, skin-independent place that converts a
	Copy command's parameter string to/from the engine's std::vector<Assignment>
	(filters/CopyFilter.h) and derives a display-oriented crosspoint matrix view
	of the same data. Every skin's IRoutingRenderer consumes these structures so
	the routing data lives in exactly one form and only the presentation differs.
*/

#pragma once

#include <vector>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QHash>

#include "filters/CopyFilter.h"

class CopyRoutingAdapter
{
public:
	// Parse "L=R VSL=0.866*L+-0.5*R" into assignments. Mirrors the engine's
	// CopyFilterFactory::createFilter parsing so the editor and runtime agree.
	static std::vector<Assignment> parse(const QString& parameters);

	// Serialise assignments back to a parameter string. Mirrors
	// CopyFilterGUI::store so a parse/serialise round-trip is lossless.
	static QString serialize(const std::vector<Assignment>& assignments);

	// True for channels that are not part of the standard physical layout
	// (the upmix scratch channels such as VSL/VRR). Used to style them as
	// dashed "virtual" badges, matching the redesign mock-up.
	static bool isVirtualChannel(const QString& channel);

	// Fixed display colour for a channel (physical channels have a stable hue;
	// virtual channels reuse their base colour or a neutral slate).
	static QString channelColor(const QString& channel);

	// ── Crosspoint matrix view ─────────────────────────────────────────────
	// A single source contribution to one output channel.
	struct Cell
	{
		double factor = 1.0;
		bool isDecibel = false;
		bool present = false;
	};

	// Derived grid: outputs are the assignment targets in file order; inputs
	// are every distinct source channel in first-seen order. cell(out,in) holds
	// the coefficient if that routing exists.
	struct Matrix
	{
		QStringList outputs;
		QStringList inputs;

		Cell cell(int outRow, int inCol) const;

		// outRow * inputs.size() + inCol -> Cell
		QHash<int, Cell> cells;
		int indexOf(int outRow, int inCol) const { return outRow * inputs.size() + inCol; }
	};

	static Matrix buildMatrix(const std::vector<Assignment>& assignments);
};
