/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "RoutingGridModel.h"
#include "CopyRoutingAdapter.h"
#include "RoutingFold.h"

namespace
{
// Case-insensitive port lookup with the legacy scene's aliases: "SUB" finds
// the LFE chip (and vice versa), and a leading digit reads as the 1-based
// position among the seeded channels, exactly like ChannelLayout.
int findPort(const QStringList& ports, int seededCount, const QString& written)
{
	if (written.isEmpty())
		return -1;

	for (int i = 0; i < ports.size(); i++)
		if (ports[i].compare(written, Qt::CaseInsensitive) == 0)
			return i;

	const QString upper = written.toUpper();
	const QString alias = upper == QLatin1String("SUB") ? QStringLiteral("LFE")
		: upper == QLatin1String("LFE") ? QStringLiteral("SUB") : QString();
	if (!alias.isEmpty())
		for (int i = 0; i < ports.size(); i++)
			if (ports[i].compare(alias, Qt::CaseInsensitive) == 0)
				return i;

	if (written[0].isDigit())
	{
		bool ok = false;
		const int position = written.toInt(&ok);
		if (ok && position >= 1 && position <= seededCount)
			return position - 1;
	}

	return -1;
}
}

void RoutingGridModel::load(const std::vector<Assignment>& assignments,
	const std::vector<std::wstring>& channelNames, const PortConfig& config)
{
	this->config = config;
	inputs.clear();
	outputs.clear();
	constInputIndex = -1;
	traceList.clear();
	emitOrder.clear();

	if (config.fixedSourceMode())
	{
		// The top row is exactly the given port list: no aliases, no numeric
		// positions (the labels ARE numbers) and no constant input.
		inputs = config.fixedSources;
		seededInputs = 0;
	}
	else
	{
		for (const std::wstring& name : channelNames)
			inputs.append(QString::fromStdWString(name));
		seededInputs = inputs.size();
		constInputIndex = inputs.size();
		inputs.append(QString());
	}

	for (const std::wstring& name : channelNames)
		outputs.append(QString::fromStdWString(name));
	seededOutputs = outputs.size();

	for (const Assignment& assignment : assignments)
	{
		const QString target = QString::fromStdWString(assignment.targetChannel);
		if (target.isEmpty())
			continue;
		const int output = resolveOutput(target);
		if (!emitOrder.contains(output))
			emitOrder.append(output);

		for (const Assignment::Summand& summand : assignment.sourceSum)
		{
			const QString channel = QString::fromStdWString(summand.channel);
			if (channel == QLatin1String(" "))
				continue;

			Trace trace;
			trace.output = output;
			trace.factor = summand.factor;
			trace.isDecibel = summand.isDecibel;
			if (channel.isEmpty())
			{
				// A value summand feeds from the constant port (Copy mode
				// only; the mapping grammar cannot produce one).
				if (constInputIndex < 0)
					continue;
				trace.input = constInputIndex;
			}
			else
			{
				trace.input = resolveInput(channel);
			}
			traceList.append(trace);
		}
	}
}

const QStringList& RoutingGridModel::inputPorts() const
{
	return inputs;
}

const QStringList& RoutingGridModel::outputPorts() const
{
	return outputs;
}

bool RoutingGridModel::constInput(int index) const
{
	return index == constInputIndex && constInputIndex >= 0;
}

const QVector<RoutingGridModel::Trace>& RoutingGridModel::traces() const
{
	return traceList;
}

bool RoutingGridModel::allowFactors() const
{
	return config.allowFactors;
}

int RoutingGridModel::addOutput(const QString& name)
{
	const QString trimmed = name.trimmed();
	if (trimmed.isEmpty())
		return -1;
	const int existing = findPort(outputs, seededOutputs, trimmed);
	if (existing >= 0)
		return existing;
	outputs.append(trimmed);
	return outputs.size() - 1;
}

int RoutingGridModel::seededInputCount() const
{
	return seededInputs;
}

int RoutingGridModel::seededOutputCount() const
{
	return seededOutputs;
}

bool RoutingGridModel::removeChannel(const QString& name)
{
	bool changed = false;

	// Output side: drop the port, its traces and its emit-order slot, then
	// close the index gap every stored reference straddles.
	int output = -1;
	for (int i = 0; i < outputs.size(); i++)
		if (outputs[i].compare(name, Qt::CaseInsensitive) == 0)
		{
			output = i;
			break;
		}
	if (output >= 0)
	{
		for (int i = traceList.size() - 1; i >= 0; i--)
		{
			if (traceList[i].output != output)
				continue;
			traceList.removeAt(i);
			changed = true;
		}
		for (Trace& trace : traceList)
			if (trace.output > output)
				trace.output--;
		emitOrder.removeAll(output);
		for (int& order : emitOrder)
			if (order > output)
				order--;
		outputs.removeAt(output);
		if (output < seededOutputs)
			seededOutputs--;
	}

	// Input side: same closure for the source port (the constant port is
	// nameless and never matches).
	int input = -1;
	for (int i = 0; i < inputs.size(); i++)
		if (i != constInputIndex && inputs[i].compare(name, Qt::CaseInsensitive) == 0)
		{
			input = i;
			break;
		}
	if (input >= 0)
	{
		for (int i = traceList.size() - 1; i >= 0; i--)
		{
			if (traceList[i].input != input)
				continue;
			traceList.removeAt(i);
			changed = true;
		}
		for (Trace& trace : traceList)
			if (trace.input > input)
				trace.input--;
		if (constInputIndex > input)
			constInputIndex--;
		inputs.removeAt(input);
		if (input < seededInputs)
			seededInputs--;
	}

	return changed;
}

void RoutingGridModel::addTrace(int input, int output)
{
	if (input < 0 || input >= inputs.size() || output < 0 || output >= outputs.size())
		return;

	Trace trace;
	trace.input = input;
	trace.output = output;
	// The constant port contributes a value, so a fresh connection from it
	// writes 0.0 (the legacy scene's behaviour); everywhere else unity.
	trace.factor = constInput(input) ? 0.0 : 1.0;
	trace.isDecibel = false;
	traceList.append(trace);
	if (!emitOrder.contains(output))
		emitOrder.append(output);
}

bool RoutingGridModel::rewirePort(bool inputSide, int fromPort, int toPort)
{
	const int portCount = inputSide ? inputs.size() : outputs.size();
	if (fromPort < 0 || fromPort >= portCount || toPort < 0
		|| toPort >= portCount || fromPort == toPort)
		return false;

	bool changed = false;
	for (Trace& trace : traceList)
	{
		int& endpoint = inputSide ? trace.input : trace.output;
		if (endpoint != fromPort)
			continue;
		endpoint = toPort;
		changed = true;
	}
	if (!changed || inputSide)
		return changed;

	// Moving a target also moves its serialization slot. If the destination
	// already has a slot, the sums merge there and the now-empty old slot
	// disappears; otherwise replace the old slot in place to retain order.
	const int oldOrder = emitOrder.indexOf(fromPort);
	const int newOrder = emitOrder.indexOf(toPort);
	if (newOrder >= 0)
	{
		if (oldOrder >= 0)
			emitOrder.removeAt(oldOrder);
	}
	else if (oldOrder >= 0)
	{
		emitOrder[oldOrder] = toPort;
	}
	else
	{
		emitOrder.append(toPort);
	}
	return true;
}

void RoutingGridModel::removeTrace(int index)
{
	if (index >= 0 && index < traceList.size())
		traceList.removeAt(index);
}

bool RoutingGridModel::setFactorText(int index, const QString& text)
{
	if (index < 0 || index >= traceList.size() || !config.allowFactors)
		return false;

	if (text.trimmed().isEmpty())
	{
		traceList.removeAt(index);
		return true;
	}

	Assignment::Summand parsed;
	if (!commitFactor(parsed, text))
		return false;
	traceList[index].factor = parsed.factor;
	traceList[index].isDecibel = parsed.isDecibel;
	return true;
}

std::vector<Assignment> RoutingGridModel::assignments() const
{
	std::vector<Assignment> result;
	for (int output : emitOrder)
	{
		Assignment assignment;
		assignment.targetChannel = outputs[output].toStdWString();
		for (const Trace& trace : traceList)
		{
			if (trace.output != output || trace.input < 0)
				continue;
			Assignment::Summand summand;
			summand.factor = trace.factor;
			summand.isDecibel = trace.isDecibel;
			if (!constInput(trace.input))
				summand.channel = inputs[trace.input].toStdWString();
			assignment.sourceSum.push_back(summand);
		}
		if (!assignment.sourceSum.empty())
			result.push_back(assignment);
	}
	return result;
}

int RoutingGridModel::resolveInput(const QString& written)
{
	const int found = findPort(inputs, config.fixedSourceMode() ? 0 : seededInputs, written);
	if (found >= 0)
		return found;
	// An unknown source (a hand-written virtual channel, or a stale mapping
	// index) still gets a chip so the connection stays visible and removable.
	if (constInputIndex >= 0)
	{
		inputs.insert(constInputIndex, written);
		constInputIndex++;
		// Traces recorded earlier keep indices below the insertion point only
		// if nothing before it moved; inserting before the constant port
		// shifts just the constant port itself, which no earlier trace can
		// reference before load ends (const traces resolve at read time).
		for (Trace& trace : traceList)
			if (trace.input == constInputIndex - 1)
				trace.input = constInputIndex;
		return constInputIndex - 1;
	}
	inputs.append(written);
	return inputs.size() - 1;
}

int RoutingGridModel::resolveOutput(const QString& written)
{
	const int found = findPort(outputs, seededOutputs, written);
	if (found >= 0)
		return found;
	outputs.append(written);
	return outputs.size() - 1;
}

int RoutingGridModel::rowIndexOf(const std::vector<Assignment>& assignments, const QString& target)
{
	for (int i = 0; i < static_cast<int>(assignments.size()); i++)
		if (QString::fromStdWString(assignments[i].targetChannel).compare(target, Qt::CaseInsensitive) == 0)
			return i;
	return -1;
}

int RoutingGridModel::summandIndex(const Assignment& assignment, const QString& channel,
	Qt::CaseSensitivity sensitivity)
{
	for (int i = 0; i < static_cast<int>(assignment.sourceSum.size()); i++)
		if (QString::fromStdWString(assignment.sourceSum[i].channel).compare(channel, sensitivity) == 0)
			return i;
	return -1;
}

bool RoutingGridModel::commitFactor(Assignment::Summand& summand, const QString& text)
{
	return RoutingFold::parseFactor(text, summand);
}

bool RoutingGridModel::commitFactor(std::vector<Assignment>& assignments, int row,
	const QString& channel, const QString& text, bool allowFactors)
{
	if (!allowFactors || row < 0 || row >= static_cast<int>(assignments.size()) || channel.isEmpty())
		return false;

	Assignment& assignment = assignments[row];
	const int index = summandIndex(assignment, channel);
	if (text.trimmed().isEmpty())
	{
		if (index >= 0)
			assignment.sourceSum.erase(assignment.sourceSum.begin() + index);
		// The grids historically notify even when an empty cell is cleared.
		return true;
	}

	Assignment::Summand parsed;
	if (!commitFactor(parsed, text))
		return false;
	if (index >= 0)
	{
		assignment.sourceSum[index].factor = parsed.factor;
		assignment.sourceSum[index].isDecibel = parsed.isDecibel;
	}
	else
	{
		parsed.channel = channel.toStdWString();
		assignment.sourceSum.push_back(parsed);
	}
	return true;
}

bool RoutingGridModel::commitSource(std::vector<Assignment>& assignments, int row, int summand,
	const QString& text, bool fixedPorts, bool allowFactors)
{
	if (row < 0 || row >= static_cast<int>(assignments.size()) || summand < 0
		|| summand >= static_cast<int>(assignments[row].sourceSum.size()))
		return false;

	Assignment& assignment = assignments[row];
	const QString raw = text.trimmed();
	if (raw.isEmpty())
	{
		assignment.sourceSum.erase(assignment.sourceSum.begin() + summand);
		return true;
	}

	Assignment::Summand& current = assignment.sourceSum[summand];
	Assignment::Summand edited = current;
	if (!RoutingFold::parseSourceToken(raw, fixedPorts, edited)
		|| (!allowFactors && (edited.factor != 1.0 || edited.isDecibel)))
		return false;
	if (edited.channel == current.channel && edited.factor == current.factor
		&& edited.isDecibel == current.isDecibel)
		return false;
	current = edited;
	return true;
}

bool RoutingGridModel::commitChip(std::vector<Assignment>& assignments, int row, int summand,
	const QString& text, const QStringList& channels)
{
	if (row < 0 || row >= static_cast<int>(assignments.size()) || summand < 0
		|| summand >= static_cast<int>(assignments[row].sourceSum.size()))
		return false;

	Assignment& assignment = assignments[row];
	const QString raw = text.trimmed();
	if (raw.isEmpty())
	{
		assignment.sourceSum.erase(assignment.sourceSum.begin() + summand);
		return true;
	}

	Assignment::Summand& current = assignment.sourceSum[summand];
	Assignment::Summand edited = current;
	// Bare integers remain gains in the chip editor, unlike the step list's
	// source-token editor. Only an offered channel can be re-entered here;
	// arbitrary words must still fail instead of declaring a virtual source.
	if (!commitFactor(edited, raw))
	{
		if (!channels.contains(raw, Qt::CaseInsensitive))
			return false;
		edited.channel = raw.toStdWString();
		edited.factor = 1.0;
		edited.isDecibel = false;
	}
	current = edited;
	return true;
}

QStringList RoutingGridModel::sourceChannels(const std::vector<Assignment>& assignments,
	const std::vector<std::wstring>& deviceChannels, const QStringList& fixedSources)
{
	if (!fixedSources.isEmpty())
		return fixedSources;

	QStringList channels;
	auto append = [&channels](const QString& channel) {
		if (!channel.isEmpty() && channel != QLatin1String(" ")
			&& !channels.contains(channel, Qt::CaseInsensitive))
			channels.append(channel);
	};
	for (const std::wstring& channel : deviceChannels)
		append(QString::fromStdWString(channel));
	for (const Assignment& assignment : assignments)
	{
		append(QString::fromStdWString(assignment.targetChannel));
		for (const Assignment::Summand& summand : assignment.sourceSum)
			append(QString::fromStdWString(summand.channel));
	}
	return channels;
}

bool RoutingGridModel::addChannel(std::vector<Assignment>& assignments, QStringList& pinnedChannels,
	const QString& text)
{
	const QString name = text.trimmed();
	if (!RoutingFold::isValidChannelName(name))
		return false;
	CopyRoutingAdapter::ensureTargetChannel(assignments, pinnedChannels, name);
	return true;
}

bool RoutingGridModel::addChannel(QStringList& pinnedChannels, const QString& text)
{
	const QString name = text.trimmed();
	if (!RoutingFold::isValidChannelName(name))
		return false;
	addOutput(name);
	CopyRoutingAdapter::pinChannel(pinnedChannels, name);
	return true;
}

void RoutingGridModel::removePin(QStringList& pinnedChannels, const QString& channel)
{
	for (int i = pinnedChannels.size() - 1; i >= 0; i--)
		if (pinnedChannels[i].compare(channel, Qt::CaseInsensitive) == 0)
			pinnedChannels.removeAt(i);
}

bool RoutingGridModel::removeChannel(std::vector<Assignment>& assignments, QStringList& pinnedChannels,
	const QString& channel)
{
	removePin(pinnedChannels, channel);
	return RoutingFold::removeChannel(assignments, channel);
}
