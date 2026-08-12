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

FilterPickerView* MatrixSkin::createFilterPicker(QWidget* parent) const
{
	return new MatrixFilterPickerView(parent);
}

ReferenceCardView* MatrixSkin::createReferenceCardView(const QString& kind, QWidget* parent) const
{
	return new MatrixReferenceCardView(kind, parent);
}

SubwooferRoutingCardView* MatrixSkin::createSubwooferRoutingCardView(QWidget* parent) const
{
	return new MatrixSubwooferRoutingCardView(parent);
}

ISkin* matrixSkin()
{
	static MatrixSkin instance;
	return &instance;
}
