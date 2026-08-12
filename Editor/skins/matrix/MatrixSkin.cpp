/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "MatrixSkin.h"

#include <QComboBox>
#include <QFont>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QToolButton>

#include "Editor/SkinManager.h"
#include "Editor/skins/shared/SkinPaint.h"
#include "Editor/skins/Skins.h"
#include "Editor/skins/matrix/cards/MatrixReferenceCardView.h"
#include "Editor/skins/matrix/cards/MatrixSubwooferRoutingCardView.h"
#include "Editor/skins/matrix/picker/MatrixFilterPicker.h"
#include "Editor/skins/matrix/routing/CrosspointMatrixRoutingRenderer.h"
#include "Editor/widgets/cards/VSTBusLayoutControls.h"

namespace
{
class MatrixVSTBusLayoutControls final : public VSTBusLayoutControls
{
public:
	explicit MatrixVSTBusLayoutControls(QWidget* parent)
		: VSTBusLayoutControls(parent, false)
	{
		setObjectName(QStringLiteral("MatrixVSTBusLayoutControls"));
		configurePaintOnlyChrome(this);
		QFont cellFont(inputCaptionLabel()->font());
		cellFont.setPointSizeF(7.5);
		cellFont.setWeight(QFont::DemiBold);
		cellFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.4);
		inputCaptionLabel()->setText(QStringLiteral("IN"));
		outputCaptionLabel()->setText(QStringLiteral("OUT"));
		for (QLabel* label : { inputCaptionLabel(), outputCaptionLabel(), directionIndicator() })
			label->setFont(cellFont);
		directionIndicator()->setText(QStringLiteral(">"));
		statusTextLabel()->setProperty("matrixBusRemark", true);

		QGridLayout* cells = new QGridLayout(this);
		cells->setContentsMargins(8, 7, 8, 7);
		cells->setHorizontalSpacing(0);
		cells->setVerticalSpacing(4);
		cells->addWidget(inputCaptionLabel(), 0, 0);
		cells->addWidget(inputSelector(), 0, 1);
		cells->addWidget(directionIndicator(), 0, 2);
		cells->addWidget(outputCaptionLabel(), 0, 3);
		cells->addWidget(outputSelector(), 0, 4);
		cells->addWidget(statusTextLabel(), 1, 0, 1, 5);
		cells->addWidget(removeLayoutsButton(), 2, 0, 1, 5, Qt::AlignLeft);
		cells->setColumnStretch(1, 1);
		cells->setColumnStretch(4, 1);
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		const SkinTokens& tokens = SkinManager::instance()->tokens();
		QPainter painter(this);
		painter.fillRect(rect(), QColor(tokens.surfaceSunken));
		painter.setPen(QPen(withAlpha(tokens.border, 160), 1.0));
		painter.drawRect(rect().adjusted(0, 0, -1, -1));
		const int y = inputSelector()->geometry().bottom() + 3;
		painter.drawLine(0, y, width(), y);
		for (int x : { inputCaptionLabel()->geometry().right(), inputSelector()->geometry().right(),
			directionIndicator()->geometry().right(), outputCaptionLabel()->geometry().right() })
			painter.drawLine(x, inputSelector()->geometry().top() - 2, x, inputSelector()->geometry().bottom() + 2);
		painter.fillRect(QRect(0, 0, 3, height()), QColor(tokens.accent));
	}
};
}

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

VSTBusLayoutControls* MatrixSkin::createVSTBusLayoutControls(QWidget* parent) const
{
	return new MatrixVSTBusLayoutControls(parent);
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
