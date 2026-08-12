/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

// Soft skin. Constitution: docs/skins/soft.md. The file-scope instance is
// exposed through softSkin() so Skins::all() can assemble the roster
// without a central definition list.

#include "SoftSkin.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QToolButton>

#include "Editor/SkinManager.h"
#include "Editor/skins/shared/SkinPaint.h"
#include "Editor/skins/shared/SkinSupport.h"
#include "Editor/widgets/cards/VSTBusLayoutControls.h"
#include "cards/SoftReferenceCardView.h"
#include "cards/SoftSubwooferRoutingCardView.h"
#include "picker/SoftFilterPicker.h"
#include "routing/BlockChipRoutingRenderer.h"

namespace
{
class SoftVSTBusLayoutControls final : public VSTBusLayoutControls
{
public:
	explicit SoftVSTBusLayoutControls(QWidget* parent)
		: VSTBusLayoutControls(parent, false)
	{
		setObjectName(QStringLiteral("SoftVSTBusLayoutControls"));
		configurePaintOnlyChrome(this);
		inputCaptionLabel()->setText(QCoreApplication::translate("VSTBusLayoutControls", "From"));
		outputCaptionLabel()->setText(QCoreApplication::translate("VSTBusLayoutControls", "To"));
		inputCaptionLabel()->setProperty("softBusCaption", true);
		outputCaptionLabel()->setProperty("softBusCaption", true);
		directionIndicator()->setProperty("softBusArrow", true);
		statusTextLabel()->setProperty("softBusStatus", true);

		QGridLayout* root = new QGridLayout(this);
		root->setContentsMargins(12, 9, 12, 9);
		root->setHorizontalSpacing(10);
		root->setVerticalSpacing(7);
		root->addWidget(inputCaptionLabel(), 0, 0);
		root->addWidget(outputCaptionLabel(), 0, 2);
		root->addWidget(inputSelector(), 1, 0);
		directionIndicator()->setFixedWidth(28);
		root->addWidget(directionIndicator(), 1, 1);
		root->addWidget(outputSelector(), 1, 2);
		root->addWidget(statusTextLabel(), 2, 0, 1, 3);
		root->addWidget(removeLayoutsButton(), 3, 0, 1, 3, Qt::AlignLeft);
		root->setColumnStretch(0, 1);
		root->setColumnStretch(2, 1);
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		const SkinTokens& tokens = SkinManager::instance()->tokens();
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing, true);
		const QRectF body = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
		painter.setPen(QPen(withAlpha(tokens.border, 135), 1.0));
		painter.setBrush(withAlpha(QColor(tokens.surfaceRaised), 165));
		painter.drawRoundedRect(body, 15.0, 15.0);
		if (statusTextLabel()->isVisible())
		{
			QRectF pill(statusTextLabel()->geometry().adjusted(-7, -2, 7, 2));
			painter.setPen(Qt::NoPen);
			painter.setBrush(withAlpha(statusColor(), 22));
			painter.drawRoundedRect(pill, pill.height() / 2.0, pill.height() / 2.0);
		}
	}
};
}

QString SoftSkin::id() const { return QStringLiteral("soft"); }
IRoutingRenderer* SoftSkin::routingRenderer() const
{
	static BlockChipRoutingRenderer renderer;
	return &renderer;
}

// A rounded menu card picker (soft/picker/SoftFilterPicker.cpp).
FilterPickerView* SoftSkin::createFilterPicker(QWidget* parent) const
{
	return new SoftFilterPickerView(parent);
}

// The reference rows in the consumer-settings grammar
// (soft/cards/SoftReferenceCardView.cpp).
ReferenceCardView* SoftSkin::createReferenceCardView(const QString& kind, QWidget* parent) const
{
	return new SoftReferenceCardView(kind, parent);
}

VSTBusLayoutControls* SoftSkin::createVSTBusLayoutControls(QWidget* parent) const
{
	return new SoftVSTBusLayoutControls(parent);
}

SubwooferRoutingCardView* SoftSkin::createSubwooferRoutingCardView(QWidget* parent) const
{
	return new SoftSubwooferRoutingCardView(parent);
}

// Window chrome: deliberately NO paintTitleBarChrome override. The
// constitutional tiebreaker ("when in doubt, remove the element and add
// whitespace") answers painted caption decoration directly - the calm app
// header is already complete in the QSS sheets: the surface one value
// step off the window, a friendly-weight title in full ink, caption
// buttons resting as soft rounded squares whose hover lifts one value
// step on a stadium highlight, and a close button that warms with the
// dirty-badge amber instead of alarming red. Anything painted on top
// (screws, glows, grids) belongs to the neighbours' vocabularies and
// would only make the header more anxious.

// tokens()/qssResource() ride the ISkin defaults (SkinThemeData tables).

ISkin* softSkin()
{
	static SoftSkin instance;
	return &instance;
}
