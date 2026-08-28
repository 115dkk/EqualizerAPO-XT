/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	The neutral reference-card presentation: the ISkin::createReferenceCardView
	default for skins that have not answered with their own view. It renders
	the reference-card information hierarchy plainly - icon, name-first label with format /
	ABS / MISSING badges, middle-elided location line, readout line, status
	line - styled from SkinTokens only, so it stays legible under any palette.
	The five shipped skins override it; keep this class free of any one skin's
	vocabulary.
*/

#pragma once

#include "ReferenceCardView.h"

class ElidedLabel;
class QHBoxLayout;
class QLabel;

class DefaultReferenceCardView : public ReferenceCardView
{
	Q_OBJECT

public:
	explicit DefaultReferenceCardView(QWidget* parent = nullptr);

	void addLeadingWidget(QWidget* widget) override;

protected:
	void placeActionButton(ActionRole role, QAbstractButton* button) override;
	void applyState(const ReferenceCardState& state) override;

private:
	QHBoxLayout* rootLayout = nullptr;
	QHBoxLayout* actionLayout = nullptr;
	QLabel* iconLabel = nullptr;
	QLabel* nameLabel = nullptr;
	QLabel* formatBadge = nullptr;
	QLabel* absBadge = nullptr;
	QLabel* missingBadge = nullptr;
	ElidedLabel* dirLabel = nullptr;
	QLabel* readoutLabel = nullptr;
	QLabel* statusLabel = nullptr;
};
