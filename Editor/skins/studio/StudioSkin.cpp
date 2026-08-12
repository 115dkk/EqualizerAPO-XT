/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

// Constitution: docs/skins/studio.md

#include "StudioSkin.h"

#include <QComboBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

#include "Editor/SkinManager.h"
#include "Editor/skins/shared/SkinSupport.h"
#include "Editor/skins/shared/SkinPaint.h"
#include "Editor/skins/studio/cards/StudioReferenceCardView.h"
#include "Editor/skins/studio/cards/StudioSubwooferRoutingCardView.h"
#include "Editor/skins/studio/picker/StudioFilterPicker.h"
#include "Editor/skins/studio/routing/LightTraceRoutingRenderer.h"
#include "Editor/widgets/cards/VSTBusLayoutControls.h"

namespace
{
class StudioVSTBusLayoutControls final : public VSTBusLayoutControls
{
public:
	explicit StudioVSTBusLayoutControls(QWidget* parent)
		: VSTBusLayoutControls(parent, false)
	{
		setObjectName(QStringLiteral("StudioVSTBusLayoutControls"));
		for (QLabel* label : { inputCaptionLabel(), outputCaptionLabel() })
		{
			QFont font(label->font());
			font.setPointSizeF(8.0);
			font.setWeight(QFont::DemiBold);
			font.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
			label->setFont(font);
		}
		statusTextLabel()->setWordWrap(false);

		QVBoxLayout* root = new QVBoxLayout(this);
		root->setContentsMargins(12, 8, 12, 7);
		root->setSpacing(5);
		QHBoxLayout* signal = new QHBoxLayout();
		signal->setContentsMargins(0, 0, 0, 0);
		signal->setSpacing(7);
		signal->addWidget(inputCaptionLabel());
		signal->addWidget(inputSelector(), 1);
		directionIndicator()->setFixedWidth(22);
		signal->addWidget(directionIndicator());
		signal->addWidget(outputCaptionLabel());
		signal->addWidget(outputSelector(), 1);
		root->addLayout(signal);

		QHBoxLayout* status = new QHBoxLayout();
		status->setContentsMargins(18, 0, 0, 0);
		status->setSpacing(8);
		status->addWidget(statusTextLabel(), 1);
		status->addWidget(removeLayoutsButton(), 0, Qt::AlignRight);
		root->addLayout(status);
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		const SkinTokens& tokens = SkinManager::instance()->tokens();
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing, true);
		const QRectF pane = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
		painter.setPen(QPen(withAlpha(tokens.border, isEnabled() ? 205 : 120), 1.0));
		painter.setBrush(withAlpha(tokens.surfaceSunken, tokens.dark ? 218 : 238));
		painter.drawRoundedRect(pane, 8.0, 8.0);

		QLinearGradient reflection(pane.left(), pane.top(), pane.right(), pane.top());
		reflection.setColorAt(0.0, QColor(255, 255, 255, 0));
		reflection.setColorAt(0.5, QColor(255, 255, 255, tokens.dark ? 46 : 150));
		reflection.setColorAt(1.0, QColor(255, 255, 255, 0));
		painter.setPen(QPen(QBrush(reflection), 1.0));
		painter.drawLine(QPointF(pane.left() + 8.0, pane.top() + 1.0),
			QPointF(pane.right() - 8.0, pane.top() + 1.0));

		if (statusTextLabel()->isVisible())
		{
			const QPointF lamp(12.0, statusTextLabel()->geometry().center().y());
			QColor glow = statusColor();
			glow.setAlpha(54);
			painter.setPen(Qt::NoPen);
			painter.setBrush(glow);
			painter.drawEllipse(lamp, 5.0, 5.0);
			painter.setBrush(statusColor());
			painter.drawEllipse(lamp, 2.2, 2.2);
		}
	}
};
}

QString StudioSkin::id() const { return QStringLiteral("studio"); }

IRoutingRenderer* StudioSkin::routingRenderer() const
{
	static LightTraceRoutingRenderer renderer;
	return &renderer;
}

FilterPickerView* StudioSkin::createFilterPicker(QWidget* parent) const
{
	return new StudioFilterPickerView(parent);
}

ReferenceCardView* StudioSkin::createReferenceCardView(const QString& kind, QWidget* parent) const
{
	return new StudioReferenceCardView(kind, parent);
}

VSTBusLayoutControls* StudioSkin::createVSTBusLayoutControls(QWidget* parent) const
{
	return new StudioVSTBusLayoutControls(parent);
}

SubwooferRoutingCardView* StudioSkin::createSubwooferRoutingCardView(QWidget* parent) const
{
	return new StudioSubwooferRoutingCardView(parent);
}

ISkin* studioSkin()
{
	static StudioSkin instance;
	return &instance;
}
