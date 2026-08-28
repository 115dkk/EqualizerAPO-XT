/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "MatrixSkin.h"

#include "Editor/skins/Skins.h"
#include "Editor/skins/matrix/cards/MatrixReferenceCardView.h"
#include "Editor/skins/matrix/cards/MatrixSubwooferRoutingCardView.h"
#include "Editor/skins/matrix/picker/MatrixFilterPicker.h"
#include "Editor/skins/matrix/routing/CrosspointMatrixRoutingRenderer.h"

QString MatrixSkin::id() const
{
	return QStringLiteral("matrix");
}
IRoutingRenderer* MatrixSkin::routingRenderer() const
{
	static CrosspointMatrixRoutingRenderer renderer;
	return &renderer;
}

FilterPickerView* MatrixSkin::createFilterPicker(QWidget* parent, const SkinTokens& tokens) const
{
	return new MatrixFilterPickerView(tokens, parent);
}

ReferenceCardView* MatrixSkin::createReferenceCardView(const QString& kind, QWidget* parent,
	const SkinTokens& tokens) const
{
	Q_UNUSED(tokens);
	return new MatrixReferenceCardView(kind, parent);
}

SubwooferRoutingCardView* MatrixSkin::createSubwooferRoutingCardView(QWidget* parent,
	const SkinTokens& tokens) const
{
	return new MatrixSubwooferRoutingCardView(tokens, parent);
}

ISkin* matrixSkin()
{
	static MatrixSkin instance;
	return &instance;
}
