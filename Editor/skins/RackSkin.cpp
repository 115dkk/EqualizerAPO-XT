/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

// Rack skin, split out of Skins.cpp (audit #109 F005). This is a verbatim
// move of the helpers and the class; behaviour is unchanged. The file-scope
// instance is exposed through rackSkin() so Skins::all() can assemble the
// roster without a central definition list.

#include "Skins.h"

#include <QAction>
#include <QComboBox>
#include <QDial>
#include <QEvent>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStyle>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>
#include <QtMath>

// Studio's S3 band-colour law maps BiQuad filter types onto hue families.
#include "filters/BiQuad.h"

#include "Editor/SkinManager.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/skins/RackChrome.h"
#include "Editor/skins/cards/RackReferenceCardView.h"
#include "Editor/skins/pickers/StudioFilterPicker.h"
#include "Editor/skins/pickers/MinimalFilterPicker.h"
#include "Editor/skins/pickers/SoftFilterPicker.h"
#include "Editor/skins/pickers/RackFilterPicker.h"
#include "Editor/skins/pickers/MatrixFilterPicker.h"
#include "Editor/widgets/routing/CrosspointMatrixRoutingRenderer.h"
#include "Editor/widgets/routing/StepListRoutingRenderer.h"
#include "Editor/widgets/routing/BlockChipRoutingRenderer.h"
#include "Editor/widgets/routing/HardwarePatchbayRoutingRenderer.h"
#include "SkinSupport.h"
#include "SkinThemeData.h"

namespace
{
class RackSkin : public ISkin
{
public:
	QString id() const override { return QStringLiteral("rack"); }
	QString qssResource(bool dark) const override
	{
		return SkinThemeData::qssResource(id(), dark);
	}
	IRoutingRenderer* routingRenderer() const override
	{
		static HardwarePatchbayRoutingRenderer renderer;
		return &renderer;
	}

	void paintKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens) const override
	{
		RackChrome::paintKnob(painter, rect, state, tokens);
	}

	void paintAddRow(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const override
	{
		// The empty rack bay: the opening's dark interior, the mounting
		// rails' empty bolt holes and a stencilled EMPTY BAY marking; hover
		// pre-heats the bezel amber (RackChrome). state.label is a UI string,
		// not hardware printing, so the stencil ignores it - the widget's
		// tooltip keeps the translated caption reachable.
		RackChrome::paintAddRow(painter, rect, state, tokens);
	}

	void paintInsertSeam(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const override
	{
		// The service slot's amber heat line above the first unit - strokes
		// only, nothing at rest (RackChrome).
		RackChrome::paintInsertSeam(painter, rect, state, tokens);
	}

	QString cardFrameStyle(const CommandRowInfo& info, const SkinTokens& tokens) const override
	{
		// QSS only provides the machined base plate and the hover brightening;
		// the faceplate texture, ears, screws and LEDs are painted on top by
		// RackChrome::paintCardChrome (the sheen overlays are translucent, so
		// the hover state shines through them). The resting border is the dark
		// seam of the rack opening rather than the token border, so stacked
		// units separate physically (R3); focus and selection keep their
		// signal colours.
		const bool dark = QColor(tokens.background).lightness() < 128;
		const QString seam = dark ? QStringLiteral("#060809") : QStringLiteral("#8F8268");
		const QString borderColor = info.focused ? tokens.focusRing : (info.selected ? tokens.accent : seam);
		const QString background = info.selected ? tokens.cardSelected : tokens.card;
		return QStringLiteral(
			"QFrame#FilterCardRow { background: %1; border: 1px solid %2; border-radius: %3px; }"
			"QFrame#FilterCardRow:hover { background: %4; }")
			.arg(background, borderColor)
			.arg(tokens.borderRadius)
			.arg(info.selected ? tokens.cardSelected : tokens.cardHover);
	}

	QString cardHeaderStyle(const CommandRowInfo&, const SkinTokens&) const override
	{
		// The header strip is part of the painted faceplate; a transparent
		// background lets the brushed metal, ears and LEDs show through.
		return QStringLiteral("QWidget#FilterCardHeader { background: transparent; }");
	}

	void prepareCommandRow(const CommandRowInfo& info, QWidget* card, QWidget* header, QWidget* body) const override
	{
		// Reserve the rack-ear zones along the faceplate edges so the painted
		// chrome (screws, LEDs, patchbay jacks, the VST nameplate) never
		// collides with row content. Rows are rebuilt on every skin switch, so
		// this only ever runs while the rack skin is active.
		if (header != nullptr && header->layout() != nullptr)
		{
			const int right = RackChrome::earWidth() + 6
				+ (info.type == QStringLiteral("vst") ? RackChrome::nameplateReserve() : 0);
			header->layout()->setContentsMargins(RackChrome::earWidth() + 6, 4, right, 4);
		}
		// Only the modern card's body stack is inset; body-only consultations
		// (Include/VST editors, legacy rows) already sit inside that stack.
		if (card != nullptr && body != nullptr)
			body->setContentsMargins(RackChrome::earWidth() + 4, 0, RackChrome::earWidth() + 4, 6);

		// Unparsed lines (bare text, programmatic commands like If) are the
		// AUX unit's programming LCD: the as-written line burns in green
		// segments in a dark recessed well, in both finishes - displays never
		// follow the panel finish. The row widget seeds this label with an
		// inline token style QSS cannot beat, so the display law is applied
		// here, and a powered-down unit dims its segments at the same time
		// (rows are rebuilt whenever the line's state changes).
		if (info.type == QStringLiteral("text") && body != nullptr)
		{
			if (QLabel* raw = body->findChild<QLabel*>(QStringLiteral("FilterCardRawText")))
			{
				const SkinTokens tk = SkinManager::instance()->tokens();
				const bool dark = QColor(tk.background).lightness() < 128;
				const QString glass = dark ? QStringLiteral("#0B0F0C") : QStringLiteral("#11150F");
				const QString segments = !info.enabled
					? (dark ? QStringLiteral("#3A6B51") : QStringLiteral("#2F6B4D"))
					: (dark ? QStringLiteral("#86F2BA") : QStringLiteral("#3ED68E"));
				const QString bezel = dark ? QStringLiteral("#050807") : QStringLiteral("#4A4438");
				const QString lowerLip = dark ? QStringLiteral("#39424A") : QStringLiteral("#6B6354");
				raw->setStyleSheet(QStringLiteral(
					"QLabel#FilterCardRawText { background:%1; color:%2;"
					" border:1px solid %3; border-bottom-color:%4; border-radius:2px;"
					" padding:6px 10px; font-family:\"%5\"; font-weight:700; }")
					.arg(glass, segments, bezel, lowerLip, tk.monoFontFamily));
			}
		}
	}

	void paintCardChrome(QPainter& painter, const QRect& rect, const CommandRowInfo& info, const SkinTokens& tokens) const override
	{
		RackChrome::paintCardChrome(painter, rect, info, tokens);
	}

	void paintTitleBarChrome(QPainter& painter, const QRect& rect, const SkinTokens& tokens) const override
	{
		// The caption strip is the unit's top panel: brushed sheen, machined
		// edges, the caption-ear groove and two rail screws (RackChrome). QSS
		// prints the model designation and dresses the caption buttons as
		// machined caps.
		RackChrome::paintTitleBarChrome(painter, rect, tokens);
	}

	FilterPickerView* createFilterPicker(QWidget* parent) const override
	{
		// The module library browser: a brushed 1U faceplate with engraved
		// section plates, LED-lit slots and an LCD search strip.
		return new RackFilterPickerView(parent);
	}

	ReferenceCardView* createReferenceCardView(const QString& kind, QWidget* parent) const override
	{
		// The reference row's service face: a bezel status lamp, engraved
		// identity printing with stamped tags and a dark LCD readout well,
		// painted in RackChrome's grammar (see RackReferenceCardView).
		return new RackReferenceCardView(kind, parent);
	}

	void styleMainToolbar(QToolBar* toolBar, const SkinTokens& tokens) const override
	{
		// The shared stroke icons first (tinted with the panel's warm ink),
		// then the master-rail chrome: RackChrome mounts a painted overlay
		// (brushed strip, machined edges, end screws, engraved series
		// marking, instant-mode power LED) under the toolbar's controls. The
		// QSS dresses the controls themselves as transport buttons and an
		// LCD save-state well.
		ISkin::styleMainToolbar(toolBar, tokens);
		RackChrome::styleMainToolbar(toolBar, tokens);
	}

	// The token table lives in SkinThemeData (shared with satellite tools);
	// this class keeps only behaviour.
	SkinTokens tokens(bool dark) const override
	{
		return SkinThemeData::tokens(id(), dark);
	}
};
}

ISkin* rackSkin()
{
	static RackSkin instance;
	return &instance;
}
