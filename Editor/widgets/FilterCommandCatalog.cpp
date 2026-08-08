/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "FilterCommandCatalog.h"

#include <QCoreApplication>

namespace FilterCommandCatalog
{
namespace
{
// Contexts are pinned to the pre-catalog owners of each string so existing
// translations survive the move (see the header). The factory contexts are
// spelled out per template entry below.
constexpr const char* cardContext = "FilterCardModel";
constexpr const char* pickerContext = "FilterPickerView";
}

const QList<CommandEntry>& commands()
{
	static const QList<CommandEntry> entries = {
		{ "#", "comment", "#", "#94a3b8", "comment-bubble", false,
		  QT_TRANSLATE_NOOP("FilterCardModel", "Comment"),
		  QT_TRANSLATE_NOOP("FilterPickerView", "A note EqualizerAPO skips while processing") },
		{ "Preamp", "preamp", "PRE", "#f59e0b", "preamp-gain", false,
		  QT_TRANSLATE_NOOP("FilterCardModel", "Preamp"),
		  QT_TRANSLATE_NOOP("FilterPickerView", "Applies overall gain before the other filters") },
		{ "Delay", "delay", "DLY", "#14b8a6", "delay-clock", false,
		  QT_TRANSLATE_NOOP("FilterCardModel", "Delay"),
		  QT_TRANSLATE_NOOP("FilterPickerView", "Delays the signal by a time or distance") },
		{ "Hilbert", "hilbert", "H90", "#6366f1", "eq-allpass", false,
		  QT_TRANSLATE_NOOP("FilterCardModel", "Hilbert transform"),
		  QT_TRANSLATE_NOOP("FilterPickerView", "Shifts phase by 90 degrees per channel, as in crossfeed synthesis") },
		{ "Velvet", "velvet", "VEL", "#d946ef", "waveform", false,
		  QT_TRANSLATE_NOOP("FilterCardModel", "Velvet decorrelator"),
		  QT_TRANSLATE_NOOP("FilterPickerView", "Decorrelates channels with sparse velvet noise for a wider image") },
		{ "Filter", "biquad", "BQUAD", "#22c55e", "eq-peaking", false,
		  QT_TRANSLATE_NOOP("FilterCardModel", "Biquad"),
		  nullptr },
		{ "GraphicEQ", "graphiceq", "GEQ", "#8b5cf6", "graphic-eq", false,
		  QT_TRANSLATE_NOOP("FilterCardModel", "Graphic EQ"),
		  QT_TRANSLATE_NOOP("FilterPickerView", "Sets a gain for each graphic-EQ band") },
		{ "Copy", "copy", "CPY", "#06b6d4", "route-channels", true,
		  QT_TRANSLATE_NOOP("FilterCardModel", "Copy"),
		  QT_TRANSLATE_NOOP("FilterPickerView", "Mixes and routes the signal between channels") },
		{ "Channel", "channel", "CH", "#3b82f6", "channel-select", true,
		  QT_TRANSLATE_NOOP("FilterCardModel", "Channel"),
		  QT_TRANSLATE_NOOP("FilterPickerView", "Selects which channels the following filters affect") },
		{ "Include", "include", "INC", "#64748b", "file-include", true,
		  QT_TRANSLATE_NOOP("FilterCardModel", "Include"),
		  QT_TRANSLATE_NOOP("FilterPickerView", "Loads another configuration file here") },
		{ "Convolution", "convolution", "CONV", "#ec4899", "waveform", false,
		  QT_TRANSLATE_NOOP("FilterCardModel", "Convolution"),
		  QT_TRANSLATE_NOOP("FilterPickerView", "Applies an impulse response, such as a room or reverb") },
		{ "MultiConvolution", "convolution", "MCONV", "#ec4899", "multi-convolution", false,
		  QT_TRANSLATE_NOOP("FilterCardModel", "MultiConvolution"),
		  QT_TRANSLATE_NOOP("FilterPickerView", "Convolves several inputs, as in BRIR headphone synthesis") },
		{ "VSTPlugin", "vst", "VST", "#a855f7", "plugin", false,
		  QT_TRANSLATE_NOOP("FilterCardModel", "VST Plugin"),
		  QT_TRANSLATE_NOOP("FilterPickerView", "Runs an external VST audio plugin") },
		{ "Device", "device", "DEV", "#64748b", "device-speaker", false,
		  QT_TRANSLATE_NOOP("FilterCardModel", "Device"),
		  QT_TRANSLATE_NOOP("FilterPickerView", "Limits the following filters to one device") },
		{ "Stage", "stage", "STG", "#f97316", "stage-chain", false,
		  QT_TRANSLATE_NOOP("FilterCardModel", "Stage"),
		  QT_TRANSLATE_NOOP("FilterPickerView", "Chooses the processing stage for the following filters") },
		{ "LoudnessCorrection", "loudness", "LOUD", "#eab308", "loudness", false,
		  QT_TRANSLATE_NOOP("FilterCardModel", "Loudness"),
		  QT_TRANSLATE_NOOP("FilterPickerView", "Compensates hearing at low listening levels") },
		{ "SubwooferRouting", "subwooferrouting", "SUB", "#84cc16", "subwoofer-routing", false,
		  QT_TRANSLATE_NOOP("FilterCardModel", "Subwoofer routing"),
		  QT_TRANSLATE_NOOP("FilterPickerView", "Applies crossover filtering and routes bass and source LFE per speaker group") },
		{ "If", "if", "IF", "#f43f5e", "logic-if", false,
		  QT_TRANSLATE_NOOP("FilterCardModel", "If"),
		  QT_TRANSLATE_NOOP("FilterPickerView", "Applies the following filters only when a condition holds") },
		{ "ElseIf", "if", "ELIF", "#f43f5e", "logic-if", false,
		  QT_TRANSLATE_NOOP("FilterCardModel", "Else if"),
		  QT_TRANSLATE_NOOP("FilterPickerView", "Tries another condition when the previous one failed") },
		{ "Else", "if", "ELSE", "#f43f5e", "logic-if", false,
		  QT_TRANSLATE_NOOP("FilterCardModel", "Else"),
		  QT_TRANSLATE_NOOP("FilterPickerView", "Runs when none of the conditions above matched") },
		{ "EndIf", "if", "ENDIF", "#f43f5e", "logic-if", false,
		  QT_TRANSLATE_NOOP("FilterCardModel", "End if"),
		  QT_TRANSLATE_NOOP("FilterPickerView", "Closes the conditional block") },
		{ "Eval", "eval", "EVAL", "#0ea5e9", "logic-eval", false,
		  QT_TRANSLATE_NOOP("FilterCardModel", "Eval"),
		  QT_TRANSLATE_NOOP("FilterPickerView", "Computes a variable from an expression") }
	};
	return entries;
}

const CommandEntry* entryForKeyword(const QString& canonicalKeyword)
{
	for (const CommandEntry& entry : commands())
		if (canonicalKeyword == QLatin1String(entry.keyword))
			return &entry;
	return nullptr;
}

const CommandEntry* entryForCommandWord(const QString& word)
{
	const QString normalized = word.trimmed();
	if (normalized.compare(QLatin1String("comment"), Qt::CaseInsensitive) == 0)
		return entryForKeyword(QStringLiteral("#"));
	for (const CommandEntry& entry : commands())
		if (normalized.compare(QLatin1String(entry.keyword), Qt::CaseInsensitive) == 0)
			return &entry;
	return nullptr;
}

const QList<BiquadCurveEntry>& biquadCurves()
{
	// Order matters twice: badge matching walks the list with startsWith, and
	// the Filter-line token scan keeps the historical first-match order.
	static const QList<BiquadCurveEntry> entries = {
		{ "PK", "eq-peaking",
		  QT_TRANSLATE_NOOP("FilterPickerView", "Boosts or cuts a band around a center frequency") },
		{ "LP", "eq-lowpass",
		  QT_TRANSLATE_NOOP("FilterPickerView", "Passes the lows and rolls off above the cutoff") },
		{ "HP", "eq-highpass",
		  QT_TRANSLATE_NOOP("FilterPickerView", "Passes the highs and rolls off below the cutoff") },
		{ "BP", "eq-bandpass",
		  QT_TRANSLATE_NOOP("FilterPickerView", "Passes a band around the center and drops the rest") },
		{ "LS", "eq-lowshelf",
		  QT_TRANSLATE_NOOP("FilterPickerView", "Raises or lowers everything below the corner frequency") },
		{ "HS", "eq-highshelf",
		  QT_TRANSLATE_NOOP("FilterPickerView", "Raises or lowers everything above the corner frequency") },
		{ "NO", "eq-notch",
		  QT_TRANSLATE_NOOP("FilterPickerView", "Cuts a narrow band deeply and leaves the rest") },
		{ "AP", "eq-allpass",
		  QT_TRANSLATE_NOOP("FilterPickerView", "Changes phase and group delay around the center frequency. Level remains unchanged.") }
	};
	return entries;
}

const QList<TemplateEntry>& pickerTemplates()
{
	static const QList<TemplateEntry> entries = {
		// General
		{ QT_TRANSLATE_NOOP("CommentFilterGUIFactory", "Comment"), "CommentFilterGUIFactory",
		  "# ", TemplateKind::Literal, nullptr, nullptr },
		// Basic filters
		{ QT_TRANSLATE_NOOP("PreampFilterGUIFactory", "Preamp (Preamplification)"), "PreampFilterGUIFactory",
		  "Preamp: 0 dB", TemplateKind::Literal,
		  QT_TRANSLATE_NOOP("PreampFilterGUIFactory", "Basic filters"), "PreampFilterGUIFactory" },
		{ QT_TRANSLATE_NOOP("CopyFilterGUIFactory", "Copy (Copy between channels)"), "CopyFilterGUIFactory",
		  "Copy: ", TemplateKind::Literal,
		  QT_TRANSLATE_NOOP("CopyFilterGUIFactory", "Basic filters"), "CopyFilterGUIFactory" },
		// Parametric filters
		{ QT_TRANSLATE_NOOP("BiQuadFilterGUIFactory", "Peaking filter"), "BiQuadFilterGUIFactory",
		  "Filter: ON PK Fc 100 Hz Gain 0 dB Q 10", TemplateKind::Literal,
		  QT_TRANSLATE_NOOP("BiQuadFilterGUIFactory", "Parametric filters"), "BiQuadFilterGUIFactory" },
		{ QT_TRANSLATE_NOOP("BiQuadFilterGUIFactory", "Low-pass filter"), "BiQuadFilterGUIFactory",
		  "Filter: ON LP Fc 100 Hz", TemplateKind::Literal,
		  QT_TRANSLATE_NOOP("BiQuadFilterGUIFactory", "Parametric filters"), "BiQuadFilterGUIFactory" },
		{ QT_TRANSLATE_NOOP("BiQuadFilterGUIFactory", "High-pass filter"), "BiQuadFilterGUIFactory",
		  "Filter: ON HP Fc 100 Hz", TemplateKind::Literal,
		  QT_TRANSLATE_NOOP("BiQuadFilterGUIFactory", "Parametric filters"), "BiQuadFilterGUIFactory" },
		{ QT_TRANSLATE_NOOP("BiQuadFilterGUIFactory", "Band-pass filter"), "BiQuadFilterGUIFactory",
		  "Filter: ON BP Fc 100 Hz Q 10", TemplateKind::Literal,
		  QT_TRANSLATE_NOOP("BiQuadFilterGUIFactory", "Parametric filters"), "BiQuadFilterGUIFactory" },
		{ QT_TRANSLATE_NOOP("BiQuadFilterGUIFactory", "Low-shelf filter"), "BiQuadFilterGUIFactory",
		  "Filter: ON LS Fc 100 Hz Gain 0 dB", TemplateKind::Literal,
		  QT_TRANSLATE_NOOP("BiQuadFilterGUIFactory", "Parametric filters"), "BiQuadFilterGUIFactory" },
		{ QT_TRANSLATE_NOOP("BiQuadFilterGUIFactory", "High-shelf filter"), "BiQuadFilterGUIFactory",
		  "Filter: ON HS Fc 100 Hz Gain 0 dB", TemplateKind::Literal,
		  QT_TRANSLATE_NOOP("BiQuadFilterGUIFactory", "Parametric filters"), "BiQuadFilterGUIFactory" },
		{ QT_TRANSLATE_NOOP("BiQuadFilterGUIFactory", "Notch filter"), "BiQuadFilterGUIFactory",
		  "Filter: ON NO Fc 100 Hz", TemplateKind::Literal,
		  QT_TRANSLATE_NOOP("BiQuadFilterGUIFactory", "Parametric filters"), "BiQuadFilterGUIFactory" },
		// Phase & Time. The all-pass pair moved out of "Parametric filters"
		// by design (the factory's comment records why); the section holds
		// the phase-shaping tools wherever their command lives.
		{ QT_TRANSLATE_NOOP("BiQuadFilterGUIFactory", "1st-order all-pass"), "BiQuadFilterGUIFactory",
		  "Filter: ON AP Fc 100 Hz Order 1", TemplateKind::Literal,
		  QT_TRANSLATE_NOOP("BiQuadFilterGUIFactory", "Phase & Time"), "BiQuadFilterGUIFactory" },
		{ QT_TRANSLATE_NOOP("BiQuadFilterGUIFactory", "2nd-order all-pass"), "BiQuadFilterGUIFactory",
		  "Filter: ON AP Fc 100 Hz Q 0.707 Order 2", TemplateKind::Literal,
		  QT_TRANSLATE_NOOP("BiQuadFilterGUIFactory", "Phase & Time"), "BiQuadFilterGUIFactory" },
		{ QT_TRANSLATE_NOOP("DelayFilterGUIFactory", "Delay"), "DelayFilterGUIFactory",
		  "Delay: 0 ms", TemplateKind::Literal,
		  QT_TRANSLATE_NOOP("DelayFilterGUIFactory", "Phase & Time"), "DelayFilterGUIFactory" },
		{ QT_TRANSLATE_NOOP("SpatialFilterGUIFactory", "Hilbert transform"), "SpatialFilterGUIFactory",
		  "Hilbert: Shift=ALL Direction=-90", TemplateKind::Literal,
		  QT_TRANSLATE_NOOP("SpatialFilterGUIFactory", "Phase & Time"), "SpatialFilterGUIFactory" },
		// Graphic equalizers
		{ QT_TRANSLATE_NOOP("GraphicEQFilterGUIFactory", "15-band graphic equalizer"), "GraphicEQFilterGUIFactory",
		  nullptr, TemplateKind::GraphicEQBands15,
		  QT_TRANSLATE_NOOP("GraphicEQFilterGUIFactory", "Graphic equalizers"), "GraphicEQFilterGUIFactory" },
		{ QT_TRANSLATE_NOOP("GraphicEQFilterGUIFactory", "31-band graphic equalizer"), "GraphicEQFilterGUIFactory",
		  nullptr, TemplateKind::GraphicEQBands31,
		  QT_TRANSLATE_NOOP("GraphicEQFilterGUIFactory", "Graphic equalizers"), "GraphicEQFilterGUIFactory" },
		{ QT_TRANSLATE_NOOP("GraphicEQFilterGUIFactory", "Graphic equalizer with variable bands"), "GraphicEQFilterGUIFactory",
		  "GraphicEQ: ", TemplateKind::Literal,
		  QT_TRANSLATE_NOOP("GraphicEQFilterGUIFactory", "Graphic equalizers"), "GraphicEQFilterGUIFactory" },
		// Advanced filters
		{ QT_TRANSLATE_NOOP("ConvolutionFilterGUIFactory", "Convolution (Convolution with impulse response)"), "ConvolutionFilterGUIFactory",
		  "Convolution:", TemplateKind::Literal,
		  QT_TRANSLATE_NOOP("ConvolutionFilterGUIFactory", "Advanced filters"), "ConvolutionFilterGUIFactory" },
		{ QT_TRANSLATE_NOOP("MultiConvolutionFilterGUIFactory", "MultiConvolution (BRIR / multi-input synthesis convolution)"), "MultiConvolutionFilterGUIFactory",
		  "MultiConvolution:", TemplateKind::Literal,
		  QT_TRANSLATE_NOOP("MultiConvolutionFilterGUIFactory", "Advanced filters"), "MultiConvolutionFilterGUIFactory" },
		{ QT_TRANSLATE_NOOP("SpatialFilterGUIFactory", "Velvet decorrelator"), "SpatialFilterGUIFactory",
		  "Velvet: Mode=Dynamic Amount=100% Length=27.5625ms Density=1088.435/s Evolution=5s Transition=250ms Decay=-60dB Variation=2050083136",
		  TemplateKind::Literal,
		  QT_TRANSLATE_NOOP("SpatialFilterGUIFactory", "Advanced filters"), "SpatialFilterGUIFactory" },
		{ QT_TRANSLATE_NOOP("LoudnessCorrectionFilterGUIFactory", "Loudness correction"), "LoudnessCorrectionFilterGUIFactory",
		  "LoudnessCorrection: State 1 ReferenceLevel 0 ReferenceOffset 0 Attenuation 1.0", TemplateKind::Literal,
		  QT_TRANSLATE_NOOP("LoudnessCorrectionFilterGUIFactory", "Advanced filters"), "LoudnessCorrectionFilterGUIFactory" },
		// Plugins
		{ QT_TRANSLATE_NOOP("VSTPluginFilterGUIFactory", "VST plugin"), "VSTPluginFilterGUIFactory",
		  "VSTPlugin:", TemplateKind::Literal,
		  QT_TRANSLATE_NOOP("VSTPluginFilterGUIFactory", "Plugins"), "VSTPluginFilterGUIFactory" },
		// Speaker management
		{ QT_TRANSLATE_NOOP("SubwooferRoutingFilterGUIFactory", "Subwoofer routing (crossover + LFE routing)"), "SubwooferRoutingFilterGUIFactory",
		  nullptr, TemplateKind::SubwooferRoutingDefaultState,
		  QT_TRANSLATE_NOOP("SubwooferRoutingFilterGUIFactory", "Speaker management"), "SubwooferRoutingFilterGUIFactory" },
		// Control and Branching close the list (the sort-last rule the
		// Expression factory used to carry).
		{ QT_TRANSLATE_NOOP("ExpressionFilterGUIFactory", "Eval (Evaluate expression)"), "ExpressionFilterGUIFactory",
		  "Eval: ", TemplateKind::Literal,
		  QT_TRANSLATE_NOOP("ExpressionFilterGUIFactory", "Control"), "ExpressionFilterGUIFactory" },
		{ QT_TRANSLATE_NOOP("IncludeFilterGUIFactory", "Include (Include configuration file)"), "IncludeFilterGUIFactory",
		  "Include:", TemplateKind::Literal,
		  QT_TRANSLATE_NOOP("IncludeFilterGUIFactory", "Control"), "IncludeFilterGUIFactory" },
		{ QT_TRANSLATE_NOOP("DeviceFilterGUIFactory", "Device (Select device)"), "DeviceFilterGUIFactory",
		  "Device: all", TemplateKind::Literal,
		  QT_TRANSLATE_NOOP("DeviceFilterGUIFactory", "Control"), "DeviceFilterGUIFactory" },
		{ QT_TRANSLATE_NOOP("ChannelFilterGUIFactory", "Channel (Select channels)"), "ChannelFilterGUIFactory",
		  "Channel: all", TemplateKind::Literal,
		  QT_TRANSLATE_NOOP("ChannelFilterGUIFactory", "Control"), "ChannelFilterGUIFactory" },
		{ QT_TRANSLATE_NOOP("StageFilterGUIFactory", "Stage (Select processing stage)"), "StageFilterGUIFactory",
		  "Stage: post-mix", TemplateKind::Literal,
		  QT_TRANSLATE_NOOP("StageFilterGUIFactory", "Control"), "StageFilterGUIFactory" },
		{ QT_TRANSLATE_NOOP("ExpressionFilterGUIFactory", "If (Begin conditional section)"), "ExpressionFilterGUIFactory",
		  "If: ", TemplateKind::Literal,
		  QT_TRANSLATE_NOOP("ExpressionFilterGUIFactory", "Branching"), "ExpressionFilterGUIFactory" },
		{ QT_TRANSLATE_NOOP("ExpressionFilterGUIFactory", "ElseIf (Alternative condition)"), "ExpressionFilterGUIFactory",
		  "ElseIf: ", TemplateKind::Literal,
		  QT_TRANSLATE_NOOP("ExpressionFilterGUIFactory", "Branching"), "ExpressionFilterGUIFactory" },
		{ QT_TRANSLATE_NOOP("ExpressionFilterGUIFactory", "Else (Fallback section)"), "ExpressionFilterGUIFactory",
		  "Else:", TemplateKind::Literal,
		  QT_TRANSLATE_NOOP("ExpressionFilterGUIFactory", "Branching"), "ExpressionFilterGUIFactory" },
		{ QT_TRANSLATE_NOOP("ExpressionFilterGUIFactory", "EndIf (End conditional section)"), "ExpressionFilterGUIFactory",
		  "EndIf:", TemplateKind::Literal,
		  QT_TRANSLATE_NOOP("ExpressionFilterGUIFactory", "Branching"), "ExpressionFilterGUIFactory" }
	};
	return entries;
}

QString iconResource(const char* baseName)
{
	if (baseName == nullptr)
		return QString();
	return QStringLiteral(":/icons/modern/%1.svg").arg(QLatin1String(baseName));
}

QString title(const CommandEntry& entry)
{
	return QCoreApplication::translate(cardContext, entry.title);
}

QString description(const CommandEntry& entry)
{
	if (entry.description == nullptr)
		return QString();
	return QCoreApplication::translate(pickerContext, entry.description);
}

QString curveDescription(const BiquadCurveEntry& entry)
{
	return QCoreApplication::translate(pickerContext, entry.description);
}

QString firstOrderAllPassDescription()
{
	return QCoreApplication::translate(pickerContext,
		"First order. Rotates 180 degrees in total, passing 90 degrees at Fc.");
}

QString templateName(const TemplateEntry& entry)
{
	return QCoreApplication::translate(entry.nameContext, entry.name);
}

QStringList templatePath(const TemplateEntry& entry)
{
	if (entry.section == nullptr)
		return QStringList();
	return QStringList(QCoreApplication::translate(entry.sectionContext, entry.section));
}
}
