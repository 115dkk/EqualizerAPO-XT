/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

// Soft skin. Constitution: docs/skins/soft.md. The file-scope instance is
// exposed through softSkin() so Skins::all() can assemble the roster
// without a central definition list.

#include "SoftSkin.h"

#include "Editor/skins/shared/SkinSupport.h"
#include "cards/SoftReferenceCardView.h"
#include "cards/SoftSubwooferRoutingCardView.h"
#include "picker/SoftFilterPicker.h"
#include "routing/BlockChipRoutingRenderer.h"

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
