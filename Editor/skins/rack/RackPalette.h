/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	The rack skin's painted colours that are not skin tokens: machined metal,
	display glass, LCD segments, lamp bezels, the brass VST nameplate, the
	patchbay caps and the file-dialog glyphs. Each value is the literal the
	painters used before they were named here (audit #348), so moving a
	painter onto this palette changes no pixel. Token-derived colours stay in
	SkinTokens; these are the hardware finishes the tokens do not describe.

	A Tone is a dark/light pair picked by the finish (tone(dark)). The light
	and shadow passes over any finish are plain white and black at an alpha,
	written light(a) and shadow(a): the constitution's "alpha passes are
	light and shadow, not palette" (docs/skins/rack.md).
*/

#pragma once

#include <QColor>
#include <QString>

namespace RackPalette
{
struct Tone
{
	QColor dark;
	QColor light;

	constexpr QColor operator()(bool isDark) const
	{
		return isDark ? dark : light;
	}

	// "#RRGGBB" in upper case, the spelling the stylesheet strings use.
	QString hex(bool isDark) const
	{
		return operator()(isDark).name().toUpper();
	}
};

// The work light and its shadow, at an alpha.
constexpr QColor light(int alpha)
{
	return QColor(255, 255, 255, alpha);
}

constexpr QColor shadow(int alpha)
{
	return QColor(0, 0, 0, alpha);
}

// Engraved lettering: the recess edge pass drawn one pixel under the ink.
inline constexpr Tone EngraveRelief{ shadow(170), light(200) };

// The dark seam of the rack opening every unit sits in.
inline constexpr Tone Seam{ QColor(0x06, 0x08, 0x09), QColor(0x8F, 0x82, 0x68) };

// Brushed-metal grain lines (their alpha is set per line).
inline constexpr Tone BrushingGrain{ QColor(255, 255, 255), QColor(96, 84, 64) };

// Slotted machine screw.
inline constexpr Tone ScrewHighlight{ QColor(0x9A, 0xA4, 0xAC), QColor(0xFF, 0xFF, 0xFC) };
inline constexpr Tone ScrewBody{ QColor(0x4E, 0x57, 0x5E), QColor(0xC4, 0xBD, 0xAE) };
inline constexpr Tone ScrewEdge{ QColor(0x23, 0x28, 0x2C), QColor(0x8E, 0x86, 0x76) };
inline constexpr Tone ScrewRim{ shadow(200), QColor(0x6B, 0x62, 0x52) };
inline constexpr Tone ScrewSlot{ QColor(10, 12, 14, 230), QColor(60, 54, 44, 220) };

// Panel LED bezel ring.
inline constexpr Tone LedBezel{ shadow(190), QColor(70, 62, 50, 190) };

// Patch jack: flange ring and bore.
inline constexpr Tone JackFlangeHighlight{ QColor(0xA8, 0xB1, 0xB8), QColor(0xFF, 0xFF, 0xFC) };
inline constexpr Tone JackFlangeMid{ QColor(0x55, 0x5E, 0x64), QColor(0xC0, 0xB9, 0xAA) };
inline constexpr Tone JackFlangeEdge{ QColor(0x26, 0x2B, 0x2F), QColor(0x86, 0x7E, 0x6E) };
inline constexpr Tone JackFlangeRim{ shadow(210), QColor(0x60, 0x58, 0x48) };
inline constexpr QColor JackBore(8, 9, 10);

// Display glass: dark in both finishes (scope, GEQ, LCD wells).
inline constexpr Tone GlassTop{ QColor(0x04, 0x06, 0x05), QColor(0x0A, 0x0E, 0x0B) };
inline constexpr Tone GlassBottom{ QColor(0x0A, 0x0F, 0x0C), QColor(0x11, 0x16, 0x10) };
inline constexpr Tone GlassBezel{ QColor(0x05, 0x08, 0x07), QColor(0x4A, 0x44, 0x38) };
inline constexpr Tone GlassBezelLip{ QColor(0x39, 0x42, 0x4A), QColor(0x6B, 0x63, 0x54) };
// The scope graticule on the cream finish (the dark finish uses its token).
inline constexpr QColor ScopeGridMinorLight(0x25, 0x43, 0x37);
// The raw-text LCD window of a card and the small LCD value well.
inline constexpr Tone LcdWindowGlass{ QColor(0x0B, 0x0F, 0x0C), QColor(0x11, 0x15, 0x0F) };
inline constexpr QColor LcdWellGlass(10, 14, 11);
// LED-segment inks: lit, dimmed, powered down.
inline constexpr Tone SegmentBright{ QColor(0x86, 0xF2, 0xBA), QColor(0x3E, 0xD6, 0x8E) };
inline constexpr Tone SegmentDim{ QColor(0x4C, 0x9E, 0x74), QColor(0x2F, 0x8A, 0x61) };
inline constexpr Tone SegmentOff{ QColor(0x3A, 0x6B, 0x51), QColor(0x2F, 0x6B, 0x4D) };
// The bass-management meter glass (one finish).
inline constexpr QColor MeterGlassTop(0x15, 0x1A, 0x17);
inline constexpr QColor MeterGlassBottom(0x08, 0x0B, 0x09);
// The module picker's search LCD: its lower bezel lip.
inline constexpr Tone SearchLcdLowerLip{ QColor(0x39, 0x42, 0x4A), QColor(0xFF, 0xFF, 0xFF) };

// Unit finishes and films.
inline constexpr Tone VstUnitFinish{ QColor(34, 20, 6, 50), QColor(74, 50, 14, 18) };
inline constexpr Tone PoweredDownFilm{ shadow(80), QColor(255, 252, 244, 120) };
inline constexpr Tone KnobPoweredDownFilm{ shadow(90), QColor(255, 252, 244, 130) };

// The brass VST nameplate (engraved and riveted).
inline constexpr Tone BrassHighlight{ QColor(0xD6, 0xB2, 0x6A), QColor(0xE8, 0xC8, 0x86) };
inline constexpr Tone BrassBody{ QColor(0xA8, 0x85, 0x46), QColor(0xC4, 0xA0, 0x5C) };
inline constexpr Tone BrassEdge{ QColor(0x86, 0x67, 0x30), QColor(0x9A, 0x7A, 0x3C) };
inline constexpr QColor BrassRim(0x5A, 0x44, 0x16);
inline constexpr QColor BrassEngraveRelief(255, 240, 200, 160);
inline constexpr QColor BrassEngraveInk(0x3A, 0x2A, 0x0C);
inline constexpr QColor BrassRivetRim(0x55, 0x40, 0x14);
inline constexpr QColor BrassRivet(0xE9, 0xD3, 0x9A);

// Knob: the machined aluminium body of the cream finish, rim and pointer.
inline constexpr QColor KnobAluminiumHighlight(0xFF, 0xFF, 0xFF);
inline constexpr QColor KnobAluminiumBody(0xDE, 0xD7, 0xC6);
inline constexpr QColor KnobAluminiumEdge(0xA8, 0x9F, 0x8C);
inline constexpr Tone KnobRim{ shadow(200), QColor(0x7E, 0x75, 0x62) };
inline constexpr Tone KnobPointer{ QColor(0xF2, 0xEC, 0xDC), QColor(0x2E, 0x29, 0x22) };

// Module picker: section-plate rivets and the panel's machined edge.
inline constexpr Tone PlateRivetRim{ shadow(180), QColor(0x6B, 0x62, 0x52) };
inline constexpr Tone PlateRivet{ QColor(0x6A, 0x74, 0x7C), QColor(0xD8, 0xCF, 0xBC) };
inline constexpr Tone PickerPlateEdge{ shadow(210), QColor(0x8A, 0x80, 0x6C) };

// The empty bay: the rack's interior (dark in both finishes) and its stencil.
inline constexpr Tone RackInteriorTop{ QColor(0x03, 0x04, 0x05), QColor(0x2E, 0x2A, 0x23) };
inline constexpr Tone RackInteriorMiddle{ QColor(0x0A, 0x0C, 0x0E), QColor(0x42, 0x3D, 0x33) };
inline constexpr Tone RackInteriorBottom{ QColor(0x12, 0x15, 0x18), QColor(0x52, 0x4B, 0x3F) };
inline constexpr Tone BayStencil{ QColor(0x8A, 0x84, 0x78, 170), QColor(0xB8, 0xAF, 0x9E, 190) };

// Patchbay: the crosspoint field and the button caps (raised, latched,
// routed, negative). Mirrors the Device switch bank in the rack sheets.
inline constexpr Tone PatchFieldShadowEdge{ QColor(0x0C, 0x10, 0x13), QColor(0xB8, 0xAC, 0x92) };
inline constexpr Tone PatchFieldLitEdge{ QColor(0x3E, 0x47, 0x4F), QColor(0xFF, 0xFF, 0xFF) };
inline constexpr Tone CapFaceTop{ QColor(0x2C, 0x33, 0x3A), QColor(0xFB, 0xF7, 0xEC) };
inline constexpr Tone CapFacePrelitTop{ QColor(0x34, 0x3C, 0x44), QColor(0xFF, 0xFF, 0xFF) };
inline constexpr Tone BlankCapFaceTop{ QColor(0x2C, 0x33, 0x3A), QColor(0xFF, 0xFF, 0xFF) };
inline constexpr Tone CapFaceBottom{ QColor(0x1B, 0x21, 0x26), QColor(0xE6, 0xDE, 0xCC) };
inline constexpr Tone CapFaceLatchedTop{ QColor(0x16, 0x1B, 0x20), QColor(0xD9, 0xD0, 0xBA) };
inline constexpr Tone CapFaceLatchedBottom{ QColor(0x21, 0x27, 0x2D), QColor(0xEE, 0xE7, 0xD4) };
inline constexpr Tone CapOutline{ QColor(0x11, 0x16, 0x1A), QColor(0xAF, 0xA2, 0x88) };
inline constexpr Tone CapDimple{ QColor(0x11, 0x16, 0x1A), QColor(0xB8, 0xAC, 0x92) };
inline constexpr Tone CapLegend{ QColor(0xB8, 0xC2, 0xCC), QColor(0x5A, 0x50, 0x38) };
inline constexpr Tone CapLegendLit{ QColor(0xE6, 0xEC, 0xF2), QColor(0x2A, 0x24, 0x14) };
inline constexpr Tone RoutedCapFaceTop{ QColor(0x24, 0x1B, 0x0C), QColor(0xE8, 0xC8, 0x87) };
inline constexpr Tone RoutedCapFaceBottom{ QColor(0x4A, 0x3A, 0x1C), QColor(0xFB, 0xE9, 0xC2) };
inline constexpr Tone RoutedCapBevelTop{ QColor(0x2A, 0x20, 0x08), QColor(0xB9, 0x8F, 0x3E) };
inline constexpr Tone RoutedCapBevelBottom{ QColor(0x6E, 0x52, 0x1E), QColor(0xFF, 0xF3, 0xD8) };
inline constexpr Tone RoutedCapLegend{ QColor(0xFF, 0xE9, 0xC8), QColor(0x4A, 0x2E, 0x00) };
inline constexpr Tone NegativeCapFaceTop{ QColor(0x2A, 0x0E, 0x0C), QColor(0xE8, 0xA6, 0x9E) };
inline constexpr Tone NegativeCapFaceBottom{ QColor(0x4A, 0x1D, 0x1C), QColor(0xF8, 0xD7, 0xD0) };
inline constexpr Tone NegativeCapBevelTop{ QColor(0x26, 0x08, 0x08), QColor(0xA3, 0x40, 0x38) };
inline constexpr Tone NegativeCapBevelBottom{ QColor(0x7A, 0x2E, 0x2A), QColor(0xFF, 0xE4, 0xDE) };
inline constexpr Tone NegativeCapLegend{ QColor(0xFF, 0xD2, 0xCC), QColor(0x5C, 0x12, 0x0C) };

// File-dialog glyphs: one finish for both modes (paper is paper).
inline constexpr QColor PaperTop(0xF7, 0xF4, 0xEA);
inline constexpr QColor PaperBottom(0xE3, 0xDF, 0xD1);
inline constexpr QColor PaperOutline(0x8A, 0x86, 0x78);
inline constexpr QColor PaperFold(0xCD, 0xC8, 0xB6);
inline constexpr QColor PaperRule(0x9A, 0x94, 0x82);
inline constexpr QColor ManilaTop(0xE8, 0xC8, 0x7E);
inline constexpr QColor ManilaBottom(0xC7, 0xA1, 0x52);
inline constexpr QColor ManilaOutline(0x8F, 0x6F, 0x2E);
inline constexpr QColor ManilaCatchLight(0xFF, 0xEC, 0xBC);
inline constexpr QColor ChipOutline(0x2A, 0x2E, 0x33);
inline constexpr QColor ChipBody(0x3A, 0x40, 0x47);
inline constexpr QColor DriveMetalTop(0x3C, 0x44, 0x4C);
inline constexpr QColor DriveMetalBottom(0x20, 0x26, 0x2B);
inline constexpr QColor HardwareOutline(0x0E, 0x12, 0x15);
inline constexpr QColor MonitorBezel(0x2C, 0x32, 0x38);
inline constexpr QColor MonitorScreen(0x14, 0x1A, 0x14);
inline constexpr QColor PowerLedGreen(0x7C, 0xE8, 0xA8);
}
