/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "CopyRoutingAdapter.h"

#include <QSet>

using std::vector;
using std::wstring;

std::vector<Assignment> CopyRoutingAdapter::parse(const QString& parameters)
{
	// Mirrors CopyFilterFactory::createFilter so editor and runtime agree.
	vector<Assignment> assignments;

	const QStringList assignmentStrings = parameters.split(QLatin1Char(' '), Qt::SkipEmptyParts);
	for (const QString& chunk : assignmentStrings)
	{
		const int eq = chunk.indexOf(QLatin1Char('='));
		if (eq < 0)
			continue;

		Assignment assignment;
		assignment.targetChannel = chunk.left(eq).toStdWString();
		const QString source = chunk.mid(eq + 1);

		const QStringList summands = source.split(QLatin1Char('+'), Qt::SkipEmptyParts);
		for (const QString& summandStr : summands)
		{
			const QStringList factors = summandStr.split(QLatin1Char('*'), Qt::SkipEmptyParts);
			QString factor;
			QString channel;
			if (factors.size() == 2)
			{
				factor = factors[0];
				channel = factors[1];
			}
			else if (factors.size() == 1)
			{
				if (factors[0] == QLatin1String("0") || factors[0].contains(QLatin1Char('.')))
					factor = factors[0];
				else
					channel = factors[0];
			}

			Assignment::Summand summand;
			if (factor.isEmpty())
			{
				summand.factor = 1.0;
				summand.isDecibel = false;
			}
			else
			{
				summand.factor = factor.toDouble();
				summand.isDecibel = factor.size() > 2 && factor.right(2).toLower() == QLatin1String("db");
			}
			summand.channel = channel.toStdWString();
			assignment.sourceSum.push_back(summand);
		}

		if (assignment.targetChannel != L"" && !assignment.sourceSum.empty())
			assignments.push_back(assignment);
	}

	return assignments;
}

QString CopyRoutingAdapter::serialize(const std::vector<Assignment>& assignments)
{
	// Mirrors CopyFilterGUI::store so a parse -> serialize round-trip is lossless.
	QString parameters;
	bool firstAssignment = true;

	for (const Assignment& assignment : assignments)
	{
		if (assignment.targetChannel == L"")
			continue;

		bool firstSummand = true;
		for (const Assignment::Summand& summand : assignment.sourceSum)
		{
			if (summand.channel == L" ")
				continue;

			if (firstSummand)
			{
				firstSummand = false;
				if (firstAssignment)
					firstAssignment = false;
				else
					parameters += QLatin1Char(' ');
				parameters += QString::fromStdWString(assignment.targetChannel);
				parameters += QLatin1Char('=');
			}
			else
			{
				parameters += QLatin1Char('+');
			}

			const bool hasChannel = summand.channel != L"";
			const bool hasFactor = !hasChannel || summand.factor != 1.0 || summand.isDecibel;

			if (hasFactor)
			{
				QString factorString;
				factorString.setNum(summand.factor);
				if (factorString != QLatin1String("0") && !factorString.contains(QLatin1Char('.')))
					factorString += QLatin1String(".0");
				parameters += factorString;
				if (summand.isDecibel)
					parameters += QLatin1String("dB");
			}

			if (hasFactor && hasChannel)
				parameters += QLatin1Char('*');

			if (hasChannel)
				parameters += QString::fromStdWString(summand.channel);
		}
	}

	return parameters;
}

bool CopyRoutingAdapter::isVirtualChannel(const QString& channel)
{
	static const QSet<QString> physical = {
		QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("C"),
		QStringLiteral("LFE"), QStringLiteral("SUB"),
		QStringLiteral("SL"), QStringLiteral("SR"),
		QStringLiteral("RL"), QStringLiteral("RR"),
		QStringLiteral("BL"), QStringLiteral("BR"),
		QStringLiteral("SBL"), QStringLiteral("SBR"),
		QStringLiteral("RC"), QStringLiteral("FLC"), QStringLiteral("FRC")
	};
	return !physical.contains(channel.toUpper());
}

QString CopyRoutingAdapter::channelColor(const QString& channel)
{
	// Fixed hues mirror the redesign mock-up (special-filters.jsx CH_COLORS).
	static const QHash<QString, QString> colors = {
		{ QStringLiteral("L"), QStringLiteral("#ef4444") },
		{ QStringLiteral("R"), QStringLiteral("#3b82f6") },
		{ QStringLiteral("C"), QStringLiteral("#22c55e") },
		{ QStringLiteral("LFE"), QStringLiteral("#f59e0b") },
		{ QStringLiteral("SUB"), QStringLiteral("#f59e0b") },
		{ QStringLiteral("SL"), QStringLiteral("#a855f7") },
		{ QStringLiteral("SR"), QStringLiteral("#ec4899") },
		{ QStringLiteral("RL"), QStringLiteral("#f97316") },
		{ QStringLiteral("RR"), QStringLiteral("#06b6d4") },
		{ QStringLiteral("SBL"), QStringLiteral("#8b5cf6") },
		{ QStringLiteral("SBR"), QStringLiteral("#14b8a6") }
	};

	QString key = channel.toUpper();
	if (colors.contains(key))
		return colors.value(key);
	// Virtual channels: derive from their trailing physical-ish suffix or fall
	// back to a neutral slate.
	if (key.startsWith(QLatin1Char('V')) && key.size() > 1)
	{
		const QString base = key.mid(1);
		if (colors.contains(base))
			return colors.value(base);
	}
	return QStringLiteral("#94a3b8");
}

CopyRoutingAdapter::Cell CopyRoutingAdapter::Matrix::cell(int outRow, int inCol) const
{
	return cells.value(indexOf(outRow, inCol), Cell());
}

CopyRoutingAdapter::Matrix CopyRoutingAdapter::buildMatrix(const std::vector<Assignment>& assignments)
{
	Matrix matrix;

	// Inputs in first-seen order across all summands.
	QSet<QString> seenInputs;
	for (const Assignment& assignment : assignments)
	{
		const QString target = QString::fromStdWString(assignment.targetChannel);
		if (target.isEmpty())
			continue;
		matrix.outputs.append(target);
		for (const Assignment::Summand& summand : assignment.sourceSum)
		{
			const QString channel = QString::fromStdWString(summand.channel);
			if (channel.isEmpty() || channel == QLatin1String(" "))
				continue;
			if (!seenInputs.contains(channel))
			{
				seenInputs.insert(channel);
				matrix.inputs.append(channel);
			}
		}
	}

	for (int outRow = 0; outRow < matrix.outputs.size(); ++outRow)
	{
		const Assignment& assignment = assignments[outRow];
		for (const Assignment::Summand& summand : assignment.sourceSum)
		{
			const QString channel = QString::fromStdWString(summand.channel);
			const int inCol = matrix.inputs.indexOf(channel);
			if (inCol < 0)
				continue;
			Cell cell;
			cell.factor = summand.factor;
			cell.isDecibel = summand.isDecibel;
			cell.present = true;
			matrix.cells.insert(matrix.indexOf(outRow, inCol), cell);
		}
	}

	return matrix;
}
