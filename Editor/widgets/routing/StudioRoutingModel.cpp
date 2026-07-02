/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "StudioRoutingModel.h"

namespace
{
// Case-insensitive port lookup with the legacy scene's aliases: "SUB" finds
// the LFE chip (and vice versa), and a leading digit reads as the 1-based
// position among the seeded channels, exactly like ChannelHelper.
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

void StudioRoutingModel::load(const std::vector<Assignment>& assignments,
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

QStringList StudioRoutingModel::inputPorts() const
{
	return inputs;
}

QStringList StudioRoutingModel::outputPorts() const
{
	return outputs;
}

bool StudioRoutingModel::constInput(int index) const
{
	return index == constInputIndex && constInputIndex >= 0;
}

const QVector<StudioRoutingModel::Trace>& StudioRoutingModel::traces() const
{
	return traceList;
}

bool StudioRoutingModel::allowFactors() const
{
	return config.allowFactors;
}

int StudioRoutingModel::addOutput(const QString& name)
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

void StudioRoutingModel::addTrace(int input, int output)
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

void StudioRoutingModel::removeTrace(int index)
{
	if (index >= 0 && index < traceList.size())
		traceList.removeAt(index);
}

void StudioRoutingModel::setFactorText(int index, const QString& text)
{
	if (index < 0 || index >= traceList.size())
		return;
	if (!config.allowFactors)
		return;

	QString raw = text.trimmed();
	if (raw.isEmpty())
	{
		traceList.removeAt(index);
		return;
	}

	bool isDecibel = false;
	if (raw.right(2).compare(QLatin1String("db"), Qt::CaseInsensitive) == 0)
	{
		isDecibel = true;
		raw = raw.left(raw.size() - 2).trimmed();
	}
	raw.replace(QLatin1Char(','), QLatin1Char('.'));

	bool ok = false;
	const double factor = raw.toDouble(&ok);
	if (!ok)
		return;

	traceList[index].factor = factor;
	traceList[index].isDecibel = isDecibel;
}

std::vector<Assignment> StudioRoutingModel::assignments() const
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

int StudioRoutingModel::resolveInput(const QString& written)
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

int StudioRoutingModel::resolveOutput(const QString& written)
{
	const int found = findPort(outputs, seededOutputs, written);
	if (found >= 0)
		return found;
	outputs.append(written);
	return outputs.size() - 1;
}
