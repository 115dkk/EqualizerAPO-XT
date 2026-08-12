/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

// Minimal skin. Constitution: docs/skins/minimal.md. The file-scope
// instance is exposed through minimalSkin() so Skins::all() can assemble
// the roster without a central definition list.

#include "MinimalSkin.h"

#include <QComboBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QToolButton>

#include "Editor/SkinManager.h"
#include "Editor/skins/shared/SkinPaint.h"
#include "Editor/skins/shared/SkinSupport.h"
#include "Editor/widgets/cards/VSTBusLayoutControls.h"
#include "cards/MinimalReferenceCardView.h"
#include "cards/MinimalSubwooferRoutingCardView.h"
#include "picker/MinimalFilterPicker.h"
#include "routing/StepListRoutingRenderer.h"

namespace
{
class MinimalVSTBusLayoutControls final : public VSTBusLayoutControls
{
public:
	explicit MinimalVSTBusLayoutControls(QWidget* parent)
		: VSTBusLayoutControls(parent, false)
	{
		setObjectName(QStringLiteral("MinimalVSTBusLayoutControls"));
		configurePaintOnlyChrome(this);
		QFont mono(QStringLiteral("Consolas"));
		mono.setStyleHint(QFont::Monospace);
		mono.setPointSizeF(8.5);
		inputCaptionLabel()->setFont(mono);
		inputSelector()->setFont(mono);
		outputCaptionLabel()->setFont(mono);
		outputSelector()->setFont(mono);
		directionIndicator()->setFont(mono);
		statusTextLabel()->setFont(mono);
		removeLayoutsButton()->setFont(mono);
		statusTextLabel()->setMinimumWidth(80);
		statusTextLabel()->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		// QLabel clips long terminal status text at narrow widths. Keep the real
		// label for accessibility/layout and paint an explicit elided readout.
		statusTextLabel()->setStyleSheet(QStringLiteral("color: transparent; background: transparent;"));
		inputCaptionLabel()->setText(QStringLiteral("IN"));
		outputCaptionLabel()->setText(QStringLiteral("OUT"));
		directionIndicator()->setText(QStringLiteral(">"));
		statusTextLabel()->setWordWrap(false);

		QHBoxLayout* row = new QHBoxLayout(this);
		row->setContentsMargins(10, 5, 10, 5);
		row->setSpacing(7);
		row->addWidget(inputCaptionLabel());
		row->addWidget(inputSelector(), 1);
		row->addWidget(directionIndicator());
		row->addWidget(outputCaptionLabel());
		row->addWidget(outputSelector(), 1);
		QLabel* separator = new QLabel(QStringLiteral("::"), this);
		separator->setFont(mono);
		separator->setProperty("minimalBusPunctuation", true);
		row->addWidget(separator);
		row->addWidget(statusTextLabel(), 2);
		row->addWidget(removeLayoutsButton());
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		const SkinTokens& tokens = SkinManager::instance()->tokens();
		QPainter painter(this);
		painter.fillRect(rect(), QColor(tokens.surfaceSunken));
		painter.setPen(QPen(QColor(tokens.border), 1.0));
		painter.drawLine(rect().topLeft(), rect().topRight());
		painter.drawLine(rect().bottomLeft(), rect().bottomRight());
		painter.setPen(QPen(QColor(tokens.accent), 2.0));
		painter.drawLine(rect().topLeft(), rect().bottomLeft());
		if (statusTextLabel()->isVisible())
		{
			painter.setFont(statusTextLabel()->font());
			painter.setPen(statusColor());
			const QRect statusRect = statusTextLabel()->geometry();
			const QString elided = painter.fontMetrics().elidedText(
				statusTextLabel()->text(), Qt::ElideRight, statusRect.width());
			painter.drawText(statusRect, Qt::AlignLeft | Qt::AlignVCenter, elided);
		}
	}
};
}

QString MinimalSkin::id() const { return QStringLiteral("minimal"); }
IRoutingRenderer* MinimalSkin::routingRenderer() const
{
	static StepListRoutingRenderer renderer;
	return &renderer;
}
FilterPickerView* MinimalSkin::createFilterPicker(QWidget* parent) const
{
	// The add-filter dropdown as a numbered terminal index; see
	// MinimalFilterPicker.h for the design.
	return new MinimalFilterPickerView(parent);
}
// The reference bodies as one line of type; see
// MinimalReferenceCardView.h.
ReferenceCardView* MinimalSkin::createReferenceCardView(const QString& kind, QWidget* parent) const
{
	return new MinimalReferenceCardView(kind, parent);
}

VSTBusLayoutControls* MinimalSkin::createVSTBusLayoutControls(QWidget* parent) const
{
	return new MinimalVSTBusLayoutControls(parent);
}

SubwooferRoutingCardView* MinimalSkin::createSubwooferRoutingCardView(QWidget* parent) const
{
	return new MinimalSubwooferRoutingCardView(parent);
}

ISkin* minimalSkin()
{
	static MinimalSkin instance;
	return &instance;
}
