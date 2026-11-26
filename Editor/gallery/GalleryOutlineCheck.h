/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The gallery's check of what a skin promises about its controls
	(ISkin::controlContract): every capture the gallery saves is measured on
	its pixels, so a promise holds in every registered scene without the
	scenes being listed here.
*/

#pragma once

#include <QList>
#include <QRect>
#include <QString>

class QImage;
class QWidget;

namespace SkinGalleryDetail
{
// One outlined part of a capture: a cap, chip, combo (and its drop-down
// edge), field, spin box, check-box or radio indicator, or a cap a routing
// view paints. rect is in the coordinates of the widget the part was
// collected under.
struct OutlinedPart
{
	QWidget* widget = nullptr;
	// Widget class name, or "QComboBox::drop-down", "QCheckBox::indicator",
	// "QRadioButton::indicator", "PaintedCap".
	QString cls;
	QString name;
	// The widget's dynamic bool and string properties, "name=value,...".
	QString props;
	// Space-separated: enabled or disabled, then checked, down, hover, focus.
	QString state;
	QRect rect;
	// Checked, pressed, hovered (enabled) or holding the keyboard focus.
	bool lit = false;
	// Inside a QFrame#FilterCardRow: the filter list.
	bool inList = false;
	// An ancestor between the part and the root cuts it.
	bool clipped = false;
};

// Every visible outlined part under root, root included.
QList<OutlinedPart> collectOutlinedParts(QWidget* root);

// Measures the active skin's control contract on one saved capture: image
// is grabbed's grab. Prints one "SkinGallery:" line per failing part and one
// summary line per capture, appends every measured side to the file named by
// EAPO_GALLERY_OUTLINE_REPORT, and returns the number of failing parts. A
// skin that promises nothing returns 0 without measuring.
int checkControlOutlines(QWidget* grabbed, const QImage& image, const QString& skinId, const QString& mode,
	const QString& scene, const QString& state);

// Keyboard focus lights a cap's outline: for one instance of each focusable
// cap class in the gallery rows, the outline with the focus differs from the
// resting outline by at least the review level on all four sides. Runs only
// for a skin that promises closed outlines; writes no capture. Returns the
// number of failing classes.
int checkFocusLightsOutlines(const QString& configPath, const QString& skinId, const QString& mode);
}
