/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "RackSkin.h"

#include <QComboBox>
#include <QFont>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QToolButton>

#include "Editor/SkinManager.h"
#include "Editor/skins/shared/SkinPaint.h"
#include "Editor/skins/Skins.h"
#include "Editor/skins/rack/cards/RackReferenceCardView.h"
#include "Editor/skins/rack/cards/RackSubwooferRoutingCardView.h"
#include "Editor/skins/rack/picker/RackFilterPicker.h"
#include "Editor/skins/rack/routing/HardwarePatchbayRoutingRenderer.h"
#include "Editor/widgets/cards/VSTBusLayoutControls.h"

namespace
{
class RackVSTBusLayoutControls final : public VSTBusLayoutControls
{
public:
	explicit RackVSTBusLayoutControls(QWidget* parent)
		: VSTBusLayoutControls(parent, false)
	{
		setObjectName(QStringLiteral("RackVSTBusLayoutControls"));
		configurePaintOnlyChrome(this);
		setProperty("rackBusDisplay", true);
		inputCaptionLabel()->setText(QStringLiteral("MAIN IN"));
		outputCaptionLabel()->setText(QStringLiteral("MAIN OUT"));
		for (QLabel* label : { inputCaptionLabel(), outputCaptionLabel() })
		{
			QFont font(label->font());
			font.setCapitalization(QFont::AllUppercase);
			font.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
			font.setPointSizeF(7.5);
			label->setFont(font);
		}
		inputSelector()->setProperty("rackBusDisplay", true);
		outputSelector()->setProperty("rackBusDisplay", true);
		statusTextLabel()->setProperty("rackBusReadout", true);

		QGridLayout* panel = new QGridLayout(this);
		panel->setContentsMargins(15, 10, 15, 9);
		panel->setHorizontalSpacing(9);
		panel->setVerticalSpacing(4);
		panel->addWidget(inputCaptionLabel(), 0, 0);
		panel->addWidget(outputCaptionLabel(), 0, 2);
		panel->addWidget(inputSelector(), 1, 0);
		panel->addWidget(directionIndicator(), 1, 1);
		panel->addWidget(outputSelector(), 1, 2);
		panel->addWidget(statusTextLabel(), 2, 0, 1, 3);
		panel->addWidget(removeLayoutsButton(), 3, 0, 1, 3, Qt::AlignLeft);
		panel->setColumnStretch(0, 1);
		panel->setColumnStretch(2, 1);
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		const SkinTokens& tokens = SkinManager::instance()->tokens();
		QPainter painter(this);
		const QRectF panel = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
		painter.setPen(QPen(QColor(tokens.border), 1.0));
		painter.setBrush(QColor(tokens.surfaceSunken));
		painter.drawRect(panel);
		painter.setPen(QPen(withAlpha(QColor(tokens.text), 28), 1.0));
		painter.drawLine(panel.topLeft() + QPointF(1, 1), panel.topRight() + QPointF(-1, 1));
		if (statusTextLabel()->isVisible())
		{
			const QPointF led(panel.right() - 9.0, statusTextLabel()->geometry().center().y());
			painter.setPen(QPen(withAlpha(statusColor(), 95), 1.0));
			painter.setBrush(statusColor());
			painter.drawEllipse(led, 3.0, 3.0);
		}
	}
};
}

QString RackSkin::id() const
{
	return QStringLiteral("rack");
}

IRoutingRenderer* RackSkin::routingRenderer() const
{
	static HardwarePatchbayRoutingRenderer renderer;
	return &renderer;
}

FilterPickerView* RackSkin::createFilterPicker(QWidget* parent) const
{
	return new RackFilterPickerView(parent);
}

ReferenceCardView* RackSkin::createReferenceCardView(const QString& kind, QWidget* parent) const
{
	return new RackReferenceCardView(kind, parent);
}

VSTBusLayoutControls* RackSkin::createVSTBusLayoutControls(QWidget* parent) const
{
	return new RackVSTBusLayoutControls(parent);
}

SubwooferRoutingCardView* RackSkin::createSubwooferRoutingCardView(QWidget* parent) const
{
	return new RackSubwooferRoutingCardView(parent);
}

ISkin* rackSkin()
{
	static RackSkin instance;
	return &instance;
}
