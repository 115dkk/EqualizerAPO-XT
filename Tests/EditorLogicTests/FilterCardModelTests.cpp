/*
	This file is part of EqualizerAPO-XT.

	FilterCardModel line classification: the card descriptors (badge, title,
	summary, pictogram), the channel/If scope axes, and the prepared card
	build plans that combine both. describeLine() follows the engine's
	registry, so these checks pin the vocabulary line by line.
*/

#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

#include "Editor/widgets/FilterCardModel.h"
#include "filters/FilterFactoryRegistry.h"

#include "EditorLogicTestSupport.h"

void testFilterCardDescriptors()
{
	// Every classification below runs through FilterCardModel::canonicalCommand,
	// which asks the engine's registry. The registry is filled by file-scope
	// statics in the *FilterFactory translation units, which nothing here
	// references by name - that is why this project links Common.lib whole
	// (/WHOLEARCHIVE) instead of on demand. Gate on the vocabulary so a
	// dropped factory object shows up as one clear failure instead of thirty
	// badge mismatches on cards that all fell back to raw text.
	harness.require(!FilterFactoryRegistry::knownConfigCommands().empty(),
		"knownConfigCommands() is empty; no filter factory translation unit is linked into this test binary");

	FilterCardDescriptor preamp = FilterCardModel::describeLine("Preamp: -6 dB");
	expectEqual(preamp.badge, "PRE", "preamp card badge");
	expectEqual(preamp.title, "Preamp", "preamp card title");
	expectEqual(preamp.summary, "-6 dB", "preamp card summary");
	expectTrue(preamp.enabled, "preamp card was marked disabled");

	FilterCardDescriptor disabledFilter = FilterCardModel::describeLine("# Filter: ON PK Fc 1000 Hz Gain -3 dB Q 0.71");
	expectFalse(disabledFilter.enabled, "commented filter was marked enabled");
	expectEqual(disabledFilter.badge, "PK", "disabled biquad badge");
	expectEqual(disabledFilter.title, "Peaking", "disabled biquad title");
	expectEqual(disabledFilter.summary, "Fc 1000 Hz · Gain -3 dB · Q 0.71", "disabled biquad summary");

	FilterCardDescriptor lowShelfCenterFilter = FilterCardModel::describeLine("Filter: ON LSC 12 dB Fc 200 Hz Gain 3 dB");
	expectEqual(lowShelfCenterFilter.badge, "LSC", "low-shelf center badge");
	expectEqual(lowShelfCenterFilter.title, "Low-shelf", "low-shelf center title");
	expectTrue(lowShelfCenterFilter.summary.contains("Center 200 Hz") && lowShelfCenterFilter.summary.contains("Gain 3 dB") && lowShelfCenterFilter.summary.contains("Slope 12 dB/Oct"),
		QStringLiteral("low-shelf center summary missing expected fields: ") + lowShelfCenterFilter.summary);

	FilterCardDescriptor lowShelfCornerFilter = FilterCardModel::describeLine("Filter: ON LS 6 dB Fc 120 Hz Gain -2 dB");
	expectEqual(lowShelfCornerFilter.badge, "LS", "low-shelf corner badge");
	expectEqual(lowShelfCornerFilter.title, "Low-shelf", "low-shelf corner title");
	expectTrue(lowShelfCornerFilter.summary.contains("Corner 120 Hz") && lowShelfCornerFilter.summary.contains("Gain -2 dB") && lowShelfCornerFilter.summary.contains("Slope 6 dB/Oct"),
		QStringLiteral("low-shelf corner summary missing expected fields: ") + lowShelfCornerFilter.summary);

	FilterCardDescriptor pureComment = FilterCardModel::describeLine("    # purely explanatory comment");
	expectFalse(pureComment.enabled, "pure comment was marked enabled");
	expectFalse(pureComment.canToggleEnabled, "pure comment should not expose enable toggling");
	expectEqual(pureComment.badge, "#", "pure comment badge");
	expectEqual(pureComment.title, "Comment", "pure comment title");

	FilterCardDescriptor colonComment = FilterCardModel::describeLine("# TODO: explain headphone preset");
	expectEqual(colonComment.title, "Comment", "colon comment title");
	expectFalse(colonComment.canToggleEnabled, "colon comments should not become disabled commands");

	FilterCardDescriptor graphicEq = FilterCardModel::describeLine("GraphicEQ: 20 -1; 100 0; 1000 2");
	expectEqual(graphicEq.badge, "GEQ", "graphic eq badge");
	expectEqual(graphicEq.summary, "3 bands", "graphic eq band count");

	FilterCardDescriptor hilbert = FilterCardModel::describeLine(
		"Hilbert: Shift=SL,SR Align=L,R Direction=-90");
	expectEqual(hilbert.type, "hilbert", "Hilbert gets its own card type");
	expectEqual(hilbert.badge, "H90", "Hilbert card badge");
	expectEqual(hilbert.title, "Hilbert transform", "Hilbert card title");
	expectTrue(hilbert.summary.contains(QString::fromUtf8("−90°"))
		&& hilbert.summary.contains("2 shifted")
		&& hilbert.summary.contains("2 aligned"),
		QStringLiteral("Hilbert summary exposes phase and both explicit roles: ")
			+ hilbert.summary);
	expectTrue(hilbert.channelBadges == QStringList({"SL", "SR"}),
		"Hilbert header badges identify only phase-shifted channels");
	expectEqual(FilterCardModel::badgeIconResource("hilbert", "H90"),
		":/icons/modern/eq-allpass.svg", "Hilbert uses the phase-family pictogram");

	FilterCardDescriptor velvet = FilterCardModel::describeLine(
		"Velvet: Mode=Dynamic Amount=85% Length=27.5625ms "
		"Density=1088.435/s Evolution=5s Transition=250ms "
		"Decay=-60dB Variation=2050083136");
	expectEqual(velvet.type, "velvet", "Velvet gets its own card type");
	expectEqual(velvet.badge, "VEL", "Velvet card badge");
	expectEqual(velvet.title, "Velvet decorrelator", "Velvet card title");
	expectTrue(velvet.summary.contains("Dynamic")
		&& velvet.summary.contains("85%")
		&& velvet.summary.contains("30 taps/ch"),
		QStringLiteral("Velvet summary exposes mode, mix and sparse size: ")
			+ velvet.summary);
	expectEqual(FilterCardModel::badgeIconResource("velvet", "VEL"),
		":/icons/modern/waveform.svg", "Velvet uses the sparse-waveform pictogram");

	FilterCardDescriptor copy = FilterCardModel::describeLine("Copy: VL=L VR=R L=VL R=VR");
	expectEqual(copy.badge, "CPY", "copy card badge");
	expectEqual(copy.summary, "4 steps, 2 virtual", "copy card summary");
	expectTrue(copy.channelBadges.contains("L") && copy.channelBadges.contains("R"), "copy card did not expose final physical channels");

	FilterCardDescriptor channel = FilterCardModel::describeLine("Channel: L, R");
	expectEqual(channel.badge, "CH", "channel card badge");
	expectEqual(channel.summary, "L R", "channel card summary");
	expectTrue(channel.channelBadges.contains("L") && channel.channelBadges.contains("R"), "channel badges were not parsed");

	// MultiConvolution must get its own card header rather than falling through to
	// the generic TXT descriptor. Its grammar is "<output channel> <IR path>", so
	// the summary leads with the channel and ends with the file name.
	FilterCardDescriptor multiConv = FilterCardModel::describeLine("MultiConvolution: L brir.wav");
	expectEqual(multiConv.badge, "MCONV", "multiconvolution card badge");
	expectEqual(multiConv.title, "MultiConvolution", "multiconvolution card title");
	expectEqual(multiConv.type, "convolution", "multiconvolution shares the convolution row type");
	expectTrue(multiConv.summary.startsWith("L") && multiConv.summary.contains("brir.wav"),
		QStringLiteral("multiconvolution summary should show channel and file: ") + multiConv.summary);

	// A freshly inserted bare "MultiConvolution:" template (no channel/path yet)
	// still classifies as multiconvolution so the header keeps its badge instead of
	// rendering as a generic text row.
	FilterCardDescriptor multiConvBare = FilterCardModel::describeLine("MultiConvolution:");
	expectEqual(multiConvBare.badge, "MCONV", "bare multiconvolution keeps its badge");
	expectEqual(multiConvBare.type, "convolution", "bare multiconvolution keeps convolution styling");

	// The card badges carry the picker's pictograms instead
	// of English monograms. Pin the descriptor-keyed catalog: the biquad
	// prefix folding (LSC rides the low-shelf glyph, HPQ the high-pass one,
	// an unparsed BQUAD falls back to the generic peaking curve), the badge
	// split of the convolution siblings, and the empty fallback that keeps
	// unmapped raw text lines on their monogram.
	expectEqual(FilterCardModel::badgeIconResource("biquad", "PK"), ":/icons/modern/eq-peaking.svg", "peaking badge pictogram");
	expectEqual(FilterCardModel::badgeIconResource("biquad", "LSC"), ":/icons/modern/eq-lowshelf.svg", "LSC badge pictogram folds onto low-shelf");
	expectEqual(FilterCardModel::badgeIconResource("biquad", "HPQ"), ":/icons/modern/eq-highpass.svg", "HPQ badge pictogram folds onto high-pass");
	expectEqual(FilterCardModel::badgeIconResource("biquad", "BQUAD"), ":/icons/modern/eq-peaking.svg", "unparsed biquad falls back to the peaking curve");
	expectEqual(FilterCardModel::badgeIconResource("convolution", "CONV"), ":/icons/modern/waveform.svg", "convolution badge pictogram");
	expectEqual(FilterCardModel::badgeIconResource("convolution", "MCONV"), ":/icons/modern/multi-convolution.svg", "multiconvolution badge pictogram");
	expectEqual(FilterCardModel::badgeIconResource("device", "DEV"), ":/icons/modern/device-speaker.svg", "device badge pictogram");
	expectEqual(FilterCardModel::badgeIconResource("comment", "#"), ":/icons/modern/comment-bubble.svg", "comment badge pictogram");
	expectTrue(FilterCardModel::badgeIconResource("text", "TXT").isEmpty(), "raw text lines keep their monogram fallback");
	expectEqual(FilterCardModel::commandIconResource("MultiConvolution"), ":/icons/modern/multi-convolution.svg",
		"picker command vocabulary shares the multiconvolution pictogram");
	expectEqual(FilterCardModel::commandIconResource("Filter", "ON HP Fc 120 Hz"), ":/icons/modern/eq-highpass.svg",
		"picker command vocabulary shares the biquad curve split");

	// The programmatic vocabulary is modelled. The
	// If family shares one card type with per-branch badges, Eval gets its own
	// type, the condition/expression is the summary, and a parameterless line
	// does not echo itself twice ("ENDIF  EndIf:"). A bare note line keeps the
	// whole line as its summary.
	FilterCardDescriptor ifLine = FilterCardModel::describeLine("If: inputChannelCount == 2");
	expectEqual(ifLine.type, "if", "If line carries the if card type");
	expectEqual(ifLine.badge, "IF", "If line badge");
	expectEqual(ifLine.title, "If", "If line title");
	expectEqual(ifLine.summary, "inputChannelCount == 2", "If line summary carries the condition");

	FilterCardDescriptor elseIfLine = FilterCardModel::describeLine("ElseIf: sampleRate > 96000");
	expectEqual(elseIfLine.type, "if", "ElseIf shares the if card type");
	expectEqual(elseIfLine.badge, "ELIF", "ElseIf badge tells the branch kind");
	expectEqual(elseIfLine.summary, "sampleRate > 96000", "ElseIf summary carries the condition");

	FilterCardDescriptor elseLine = FilterCardModel::describeLine("Else:");
	expectEqual(elseLine.type, "if", "Else shares the if card type");
	expectEqual(elseLine.badge, "ELSE", "Else badge");
	expectTrue(elseLine.summary.isEmpty(),
		QStringLiteral("parameterless Else must not echo the raw line as summary: ") + elseLine.summary);

	FilterCardDescriptor endIfLine = FilterCardModel::describeLine("EndIf:");
	expectEqual(endIfLine.type, "if", "EndIf shares the if card type");
	expectEqual(endIfLine.badge, "ENDIF", "EndIf badge");
	expectEqual(endIfLine.title, "End if", "EndIf line title");
	expectTrue(endIfLine.summary.isEmpty(),
		QStringLiteral("parameterless EndIf must not echo the raw line as summary: ") + endIfLine.summary);

	FilterCardDescriptor evalLine = FilterCardModel::describeLine("Eval: gain = -3 + 1.5");
	expectEqual(evalLine.type, "eval", "Eval line carries the eval card type");
	expectEqual(evalLine.badge, "EVAL", "Eval badge");
	expectEqual(evalLine.summary, "gain = -3 + 1.5", "Eval summary carries the expression");

	// Dynamic-commands finishing pass: If/Eval wear real pictograms (the
	// decision diamond and the fx formula mark) instead of the monogram.
	expectEqual(FilterCardModel::badgeIconResource("if", "IF"), ":/icons/modern/logic-if.svg", "if badge pictogram");
	expectEqual(FilterCardModel::badgeIconResource("eval", "EVAL"), ":/icons/modern/logic-eval.svg", "eval badge pictogram");

	FilterCardDescriptor bareText = FilterCardModel::describeLine("plain note line without a command");
	expectEqual(bareText.title, "Text", "bare text line title");
	expectEqual(bareText.summary, "plain note line without a command", "bare text line keeps its content as summary");

	// Classification follows the engine, casing and all. A lower-case key is
	// prose in Equalizer APO 1.4.2 and stays prose here, because a card would
	// let one knob turn rewrite an inert note into a line that processes audio.
	FilterCardDescriptor lowerCasePreamp = FilterCardModel::describeLine("preamp: -6 dB");
	expectEqual(lowerCasePreamp.type, "text", "a lower-case \"preamp:\" key is prose to the engine, so it gets no card");
	FilterCardDescriptor lowerCaseCopyNote = FilterCardModel::describeLine("copy: remember to re-measure the room");
	expectEqual(lowerCaseCopyNote.type, "text", "the 1.4.2 \"copy: a note to self\" line must not become a routing card");

	// A numbered key is the Filter family's own grammar (REW and Dirac write
	// it), so those keep their card. No other factory accepts a trailing token:
	// "Channel 2:" is a line the engine recognizes but never runs, and its card
	// would rewrite the key to "Channel" on the first edit.
	FilterCardDescriptor numberedFilter = FilterCardModel::describeLine("Filter 1: ON PK Fc 1000 Hz Gain -3 dB Q 0.71");
	expectEqual(numberedFilter.type, "biquad", "a numbered Filter line keeps the biquad card");
	expectEqual(numberedFilter.badge, "PK", "a numbered Filter line keeps its parsed badge");
	FilterCardDescriptor numberedChannel = FilterCardModel::describeLine("Channel 2: L R");
	expectEqual(numberedChannel.type, "text", "only the Filter family takes a trailing token, so \"Channel 2:\" stays raw text");
	FilterCardDescriptor numberedCopy = FilterCardModel::describeLine("Copy 2: L=R");
	expectEqual(numberedCopy.type, "text", "\"Copy 2:\" must not open the routing view that would rewrite it as \"Copy:\"");

	// Dynamic-value contract: hasInlineExpressions must follow the engine's
	// InlineExpression lexer exactly, because it decides which lines may not
	// open a parsing editor (a knob turn would serialize the expression
	// away). Escaped backticks are literal, an empty `` still counts as an
	// expression (the engine reports its evaluation error), and the content
	// of an unterminated trailing expression is dropped like the engine
	// drops it.
	expectTrue(FilterCardModel::hasInlineExpressions("`bass + 3` dB"), "backtick expression detected");
	expectTrue(FilterCardModel::hasInlineExpressions("prefix `x` suffix"), "embedded expression detected");
	expectFalse(FilterCardModel::hasInlineExpressions("-3 dB"), "plain number is not dynamic");
	expectFalse(FilterCardModel::hasInlineExpressions("\\` literal backtick"), "escaped backtick is literal");
	expectTrue(FilterCardModel::hasInlineExpressions("``"), "empty expression still counts");
	expectFalse(FilterCardModel::hasInlineExpressions("`unterminated"), "unterminated trailing expression is dropped");
}

void testFilterCardDepths()
{
	QVector<int> depths = FilterCardModel::calculateDepths(QList<QString>({
		"Channel: L R",
		"Preamp: -6 dB",
		"Include: nested.txt",
		"Delay: 10 ms",
		"# Channel: R",
		"Preamp: -4 dB",
		"Channel: ALL",
		"Filter: ON PK Fc 1000 Hz Gain -3 dB Q 0.71"
	}));
	requireEqual(depths.size(), 8, "channel depth count");
	expectEqual(depths[0], 0, "channel command depth");
	expectEqual(depths[1], 1, "scoped preamp depth");
	expectEqual(depths[2], 1, "include depth");
	expectEqual(depths[3], 1, "include should preserve channel depth");
	expectEqual(depths[4], 1, "commented channel must not reset depth");
	expectEqual(depths[5], 1, "post-comment channel depth");
	expectEqual(depths[6], 0, "channel all depth");
	expectEqual(depths[7], 0, "post channel-all depth");

	// If opens a nestable scope that EndIf closes.
	// The indent axis puts members one level in while ElseIf/Else/EndIf sit at
	// their block head's level; the logic axis counts the scope a row lives in,
	// where branch/tail rows count their own scope so a painted rail can pass
	// through them and terminate on EndIf. Commented-out If lines are comments
	// to the engine and must not move either axis; a stray EndIf clamps at 0.
	QVector<FilterCardRowScope> scopes = FilterCardModel::calculateScopes(QList<QString>({
		"If: inputChannelCount == 2",             // 0: head, indent 0, logic 0
		"Preamp: -6 dB",                          // 1: member, indent 1, logic 1
		"If: sampleRate > 96000",                 // 2: nested head, indent 1, logic 1
		"Delay: 10 ms",                           // 3: nested member, indent 2, logic 2
		"ElseIf: sampleRate > 48000",             // 4: branch at head level, indent 1, logic 2
		"Else:",                                  // 5: branch at head level, indent 1, logic 2
		"# If: never == 1",                       // 6: commented If moves nothing, indent 2, logic 2
		"EndIf:",                                 // 7: closes nested scope, indent 1, logic 2
		"Eval: gain = -3",                        // 8: back in outer scope, indent 1, logic 1
		"EndIf:",                                 // 9: closes outer scope, indent 0, logic 1
		"Preamp: 0 dB",                           // 10: outside, indent 0, logic 0
		"EndIf:"                                  // 11: stray EndIf clamps, indent 0, logic 0
	}));
	requireEqual(scopes.size(), 12, "if scope count");
	expectEqual(scopes[0].indent, 0, "if head indent");
	expectEqual(scopes[0].logic, 0, "if head logic depth counts only outer scopes");
	expectEqual(scopes[1].indent, 1, "member indent");
	expectEqual(scopes[1].logic, 1, "member logic depth");
	expectEqual(scopes[2].indent, 1, "nested head indent");
	expectEqual(scopes[2].logic, 1, "nested head logic depth");
	expectEqual(scopes[3].indent, 2, "nested member indent");
	expectEqual(scopes[3].logic, 2, "nested member logic depth");
	expectEqual(scopes[4].indent, 1, "elseif sits at its head's indent");
	expectEqual(scopes[4].logic, 2, "elseif counts its own scope");
	expectEqual(scopes[5].indent, 1, "else sits at its head's indent");
	expectEqual(scopes[5].logic, 2, "else counts its own scope");
	expectEqual(scopes[6].indent, 2, "commented if is an ordinary member");
	expectEqual(scopes[6].logic, 2, "commented if moves no scope");
	expectEqual(scopes[7].indent, 1, "endif sits at its head's indent");
	expectEqual(scopes[7].logic, 2, "endif counts the scope it closes");
	expectEqual(scopes[8].indent, 1, "outer member indent after nested block");
	expectEqual(scopes[8].logic, 1, "outer member logic depth after nested block");
	expectEqual(scopes[9].indent, 0, "outer endif indent");
	expectEqual(scopes[9].logic, 1, "outer endif counts the scope it closes");
	expectEqual(scopes[10].indent, 0, "post-block indent");
	expectEqual(scopes[10].logic, 0, "post-block logic depth");
	expectEqual(scopes[11].indent, 0, "stray endif clamps indent at zero");
	expectEqual(scopes[11].logic, 0, "stray endif clamps logic depth at zero");

	// Channel grouping and If nesting are independent axes that add up on the
	// indent; a Channel row inside an If block indents with the block and
	// resets only the channel axis.
	QVector<FilterCardRowScope> mixed = FilterCardModel::calculateScopes(QList<QString>({
		"Channel: L R",                           // 0: indent 0
		"If: sampleRate == 48000",                // 1: indent 1 (channel scope), logic 0
		"Preamp: -2 dB",                          // 2: indent 2, logic 1
		"Channel: ALL",                           // 3: channel reset inside block, indent 1, logic 1
		"Preamp: -1 dB",                          // 4: indent 1, logic 1
		"EndIf:"                                  // 5: indent 0? (channel now 0) logic 1
	}));
	requireEqual(mixed.size(), 6, "mixed scope count");
	expectEqual(mixed[1].indent, 1, "if head inherits channel indent");
	expectEqual(mixed[2].indent, 2, "member stacks channel and if indent");
	expectEqual(mixed[3].indent, 1, "channel row indents with the enclosing block");
	expectEqual(mixed[3].logic, 1, "channel row logic depth inside block");
	expectEqual(mixed[4].indent, 1, "post channel-all member keeps if indent");
	expectEqual(mixed[5].logic, 1, "endif closes the scope in mixed nesting");
	// The scope also names the active selection: rows under "Channel: L R"
	// carry {L, R} until Channel: ALL clears it. The selection survives If
	// nesting, and a commented-out Channel line changes nothing.
	expectEqual(mixed[1].channels, QStringList({ "L", "R" }), "if head carries the channel selection");
	expectEqual(mixed[2].channels, QStringList({ "L", "R" }), "member carries the channel selection");
	expectEqual(mixed[4].channels, QStringList(), "channel-all clears the selection");
	expectEqual(mixed[5].channels, QStringList(), "endif after channel-all carries no selection");

	QVector<FilterCardRowScope> channelScopes = FilterCardModel::calculateScopes(QList<QString>({
		"Channel: SL SR",                         // 0: selects SL SR
		"Preamp: -3 dB",                          // 1: under SL SR
		"# Channel: L",                           // 2: comment, selection unchanged
		"Delay: 1 ms",                            // 3: still under SL SR
		"Channel: ALL",                           // 4: back to all channels
		"Preamp: 0 dB"                            // 5: no selection
	}));
	requireEqual(channelScopes.size(), 6, "channel selection scope count");
	expectEqual(channelScopes[1].channels, QStringList({ "SL", "SR" }), "member under channel selection");
	expectEqual(channelScopes[3].channels, QStringList({ "SL", "SR" }), "commented channel keeps the selection");
	expectEqual(channelScopes[5].channels, QStringList(), "channel-all resets the selection");
}

void testFilterCardBuildPlans()
{
	const QList<QString> lines({
		"Channel: L R",
		"If: sampleRate == 48000",
		"Copy: L=R*`gain`",
		"EndIf:",
		"Channel: ALL"
	});
	const QVector<FilterCardBuildPlan> plans = FilterCardModel::prepareRows(lines);
	const QVector<FilterCardRowScope> scopes = FilterCardModel::calculateScopes(lines);
	requireEqual(plans.size(), lines.size(), "card build-plan count");
	requireEqual(scopes.size(), lines.size(), "card build-plan scope count");

	for (int i = 0; i < plans.size(); ++i)
	{
		expectEqual(plans[i].descriptor.depth, scopes[i].indent,
			QString("build plan %1 descriptor indent").arg(i));
		expectEqual(plans[i].descriptor.logicDepth, scopes[i].logic,
			QString("build plan %1 descriptor logic depth").arg(i));
		expectEqual(plans[i].scope.indent, scopes[i].indent,
			QString("build plan %1 scope indent").arg(i));
		expectEqual(plans[i].scope.logic, scopes[i].logic,
			QString("build plan %1 scope logic depth").arg(i));
	}

	expectEqual(plans[0].descriptor.parameters, "L R", "build plan preserves parsed parameters");
	expectTrue(plans[2].descriptor.dynamicLine, "build plan carries inline-expression state");
	expectEqual(plans[2].descriptor.type, "copy", "build plan carries the prepared card type");
	expectEqual(plans[2].descriptor.scopeChannels, QStringList({ "L", "R" }),
		"build plan carries the enclosing channel selection");
	expectEqual(plans[1].descriptor.scopeChannels, QStringList({ "L", "R" }),
		"if head carries the enclosing channel selection");
}
