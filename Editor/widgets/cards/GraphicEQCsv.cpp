/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "GraphicEQCsv.h"

#include <algorithm>

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

namespace
{
const QRegularExpression& numberPattern()
{
	static const QRegularExpression pattern("[-+]?[0-9]*\\.?[0-9]+([eE][-+]?[0-9]+)?");
	return pattern;
}
}

void GraphicEQCsv::appendNodes(QTextStream& text, std::vector<FilterNode>& nodes)
{
	while (!text.atEnd())
	{
		QString line = text.readLine();
		if (line.startsWith('*'))
			continue;

		if (!line.contains('.'))
			line = line.replace(',', '.');
		QRegularExpressionMatchIterator it = numberPattern().globalMatch(line);
		while (it.hasNext())
		{
			QRegularExpressionMatch match = it.next();
			bool ok;
			const double freq = match.captured().toDouble(&ok);
			if (ok && it.hasNext())
			{
				match = it.next();
				const double gain = match.captured().toDouble(&ok);
				if (ok)
					nodes.push_back(FilterNode(freq, gain));
			}
		}
	}
}

std::vector<FilterNode> GraphicEQCsv::readNodes(const QStringList& paths)
{
	std::vector<FilterNode> nodes;
	for (const QString& path : paths)
	{
		QFile file(path);
		if (file.open(QFile::ReadOnly))
		{
			QTextStream stream(&file);
			appendNodes(stream, nodes);
		}
	}
	std::sort(nodes.begin(), nodes.end());
	return nodes;
}

void GraphicEQCsv::writeNodes(QTextStream& text, const std::vector<FilterNode>& nodes)
{
	for (const FilterNode& node : nodes)
		text << node.freq << '\t' << node.dbGain << '\n';
	text.flush();
}

bool GraphicEQCsv::writeNodesToFile(const QString& path, const std::vector<FilterNode>& nodes)
{
	QFile file(path);
	if (!file.open(QFile::WriteOnly | QFile::Truncate))
		return false;
	QTextStream stream(&file);
	writeNodes(stream, nodes);
	return true;
}

std::vector<FilterNode> GraphicEQCsv::inverted(std::vector<FilterNode> nodes)
{
	for (FilterNode& node : nodes)
		node.dbGain = -node.dbGain;
	return nodes;
}
