/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The control contract check (GalleryOutlineCheck.h). The measurement is a
	port of the outline audit the Rack outline round used on its captures:
	per side, the pixel line where the outline sits against the line 2 px
	outside the part, each reduced to its median colour over the middle 60 %
	of the side, compared as CIE76 delta-E in CIELAB.
*/

#include "Editor/gallery/GalleryOutlineCheck.h"
#include "Editor/gallery/GallerySupport.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <vector>

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFrame>
#include <QImage>
#include <QLineEdit>
#include <QMargins>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSet>
#include <QStringList>
#include <QStyle>
#include <QStyleOption>
#include <QTabBar>
#include <QToolButton>
#include <QVariant>

#include "Editor/SkinManager.h"
#include "Editor/skins/ISkin.h"
#include "Editor/widgets/FilterCardRow.h"
#include "Editor/widgets/routing/IRoutingRenderer.h"

namespace SkinGalleryDetail
{
namespace
{
// A side below this does not show against what lies outside it: the
// capture fails. The reading moves by a few units with the brushed plate's
// row phase, so the failure level sits well under the review level.
constexpr double kFailLevel = 8.0;
// A side below this shows, but not by much: listed for review, not failed.
constexpr double kReviewLevel = 15.0;
// A lit outline (checked, hover, focus, pressed) is one colour: no two of
// its sides may differ by more than this.
constexpr double kLitColourSpread = 12.0;

enum class Side
{
	Top,
	Bottom,
	Left,
	Right
};

const char* sideName(Side side)
{
	switch (side)
	{
	case Side::Top:
		return "top";
	case Side::Bottom:
		return "bottom";
	case Side::Left:
		return "left";
	case Side::Right:
		return "right";
	}
	return "?";
}

struct Lab
{
	double l;
	double a;
	double b;
};

double linearChannel(int value)
{
	const double c = value / 255.0;
	return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double labF(double t)
{
	return t > 0.008856 ? std::cbrt(t) : 7.787 * t + 16.0 / 116.0;
}

Lab toLab(QRgb rgb)
{
	const double r = linearChannel(qRed(rgb));
	const double g = linearChannel(qGreen(rgb));
	const double b = linearChannel(qBlue(rgb));
	const double x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047;
	const double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
	const double z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883;
	const double fx = labF(x);
	const double fy = labF(y);
	const double fz = labF(z);
	return { 116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz) };
}

double deltaE(QRgb first, QRgb second)
{
	const Lab a = toLab(first);
	const Lab b = toLab(second);
	return std::sqrt((a.l - b.l) * (a.l - b.l) + (a.a - b.a) * (a.a - b.a) + (a.b - b.b) * (a.b - b.b));
}

// The median of integer samples as the audit took it: the middle value, or
// the truncated mean of the two middle values.
int medianOf(std::vector<int>& values)
{
	std::sort(values.begin(), values.end());
	const size_t n = values.size();
	if (n % 2 == 1)
		return values[n / 2];
	return (values[n / 2 - 1] + values[n / 2]) / 2;
}

QRgb medianColour(const QImage& image, const std::vector<QPoint>& points)
{
	std::vector<int> r, g, b;
	r.reserve(points.size());
	g.reserve(points.size());
	b.reserve(points.size());
	for (const QPoint& point : points)
	{
		const QRgb pixel = image.pixel(point);
		r.push_back(qRed(pixel));
		g.push_back(qGreen(pixel));
		b.push_back(qBlue(pixel));
	}
	return qRgb(medianOf(r), medianOf(g), medianOf(b));
}

// The pixels of the line offset px inside the given side (negative =
// outside), over the middle 60 % of the side; empty when the line falls off
// the image.
std::vector<QPoint> sideLine(const QRect& rect, Side side, int offset, const QSize& size)
{
	std::vector<QPoint> points;
	const int x = rect.x();
	const int y = rect.y();
	const int w = rect.width();
	const int h = rect.height();
	if (side == Side::Top || side == Side::Bottom)
	{
		const int from = x + static_cast<int>(w * 0.2);
		const int to = x + static_cast<int>(w * 0.8);
		const int yy = side == Side::Top ? y + offset : y + h - 1 - offset;
		if (yy < 0 || yy >= size.height())
			return points;
		for (int xx = std::max(from, 0); xx < std::min(to, size.width()); xx++)
			points.emplace_back(xx, yy);
	}
	else
	{
		const int from = y + static_cast<int>(h * 0.2);
		const int to = y + static_cast<int>(h * 0.8);
		const int xx = side == Side::Left ? x + offset : x + w - 1 - offset;
		if (xx < 0 || xx >= size.width())
			return points;
		for (int yy = std::max(from, 0); yy < std::min(to, size.height()); yy++)
			points.emplace_back(xx, yy);
	}
	return points;
}

struct SideReading
{
	bool measured = false;
	double value = -1.0;
	int offset = 0;
	QRgb outline = 0;
	QRgb outside = 0;
	// The outline's own line: the outermost offset that stands off the
	// outside by the failure level (the best one when none does). The value
	// above takes the best offset, which may be the face just inside a dark
	// border; the colour of a lit outline is read on the border itself.
	int edgeOffset = 0;
	QRgb edge = 0;
};

// Where a part's outline can sit, and where the outside reference line is.
// A sheet border sits on the rectangle's first pixel, or one in where a 1 px
// margin is set. A latched tool button's lamp row or top bar is 2 px, so its
// outline line may sit one pixel further in. A painted cap's antialiased
// 1 px pen lies on or just outside its integer rectangle, so its reference
// is one pixel further out.
void offsetsFor(const OutlinedPart& part, Side side, std::vector<int>& offsets, int& reference)
{
	if (part.cls == QLatin1String("PaintedCap"))
	{
		offsets = { -1, 0 };
		reference = -3;
		return;
	}
	const bool bar = part.cls == QLatin1String("QToolButton") && part.state.split(QLatin1Char(' ')).contains(QStringLiteral("checked"))
		&& (side == Side::Top || side == Side::Bottom);
	offsets = bar ? std::vector<int>{ 0, 1, 2 } : std::vector<int>{ 0, 1 };
	reference = -2;
}

SideReading measureSide(const QImage& image, const QRect& rect, Side side, const OutlinedPart& part)
{
	SideReading reading;
	std::vector<int> offsets;
	int reference = -2;
	offsetsFor(part, side, offsets, reference);
	const std::vector<QPoint> outer = sideLine(rect, side, reference, image.size());
	if (outer.empty())
		return reading;
	reading.outside = medianColour(image, outer);
	bool edgeFound = false;
	for (int offset : offsets)
	{
		const std::vector<QPoint> inner = sideLine(rect, side, offset, image.size());
		if (inner.empty())
			continue;
		const QRgb colour = medianColour(image, inner);
		const double value = deltaE(colour, reading.outside);
		if (!reading.measured || value > reading.value)
		{
			reading.measured = true;
			reading.value = value;
			reading.offset = offset;
			reading.outline = colour;
		}
		if (!edgeFound && value >= kFailLevel)
		{
			edgeFound = true;
			reading.edgeOffset = offset;
			reading.edge = colour;
		}
	}
	// A painted cap's antialiased pen spreads over its two candidate lines;
	// the stronger one is its edge.
	if (!edgeFound || part.cls == QLatin1String("PaintedCap"))
	{
		reading.edgeOffset = reading.offset;
		reading.edge = reading.outline;
	}
	return reading;
}

bool hasState(const OutlinedPart& part, const char* word)
{
	return part.state.split(QLatin1Char(' ')).contains(QLatin1String(word));
}

bool hasProp(const OutlinedPart& part, const char* nameValue)
{
	return part.props.split(QLatin1Char(',')).contains(QLatin1String(nameValue));
}

bool underAncestorNamed(const QWidget* widget, const char* name)
{
	for (const QWidget* p = widget != nullptr ? widget->parentWidget() : nullptr; p != nullptr; p = p->parentWidget())
	{
		if (p->objectName() == QLatin1String(name))
			return true;
	}
	return false;
}

// A part that deliberately draws no outline. Each row says why; atRestOnly
// rows are checked in the states where the part does draw one (lit).
struct Exemption
{
	std::function<bool(const OutlinedPart&)> matches;
	bool atRestOnly;
	const char* reason;
};

const std::vector<Exemption>& exemptions()
{
	static const std::vector<Exemption> table = {
		// The plain QToolButton rule has a transparent border at rest: a bare
		// glyph button (dialog and toolbar extras) is printed on the plate
		// and grows a cap only under the pointer or latched. The main
		// toolbar's and the file dialog's transport keys and a tab bar's
		// scroll keys are caps at rest and are not exempt.
		{ [](const OutlinedPart& part) {
			 return part.cls == QLatin1String("QToolButton") && part.name.isEmpty()
				 && !hasProp(part, "rackTransport=true") && !underAncestorNamed(part.widget, "MainToolBar")
				 && qobject_cast<const QTabBar*>(part.widget->parentWidget()) == nullptr;
		 },
			true, "plain tool button: bare glyph at rest" },
		// #456: a card header's actions (fold, add, remove, raw edit) rest as
		// bare glyphs and wear a cap only when lit.
		{ [](const OutlinedPart& part) { return hasProp(part, "headerAction=true"); }, true,
			"card header action: bare glyph at rest" },
		// Parameter selectors are engraved legends with an underline, not caps
		// (rack.md, "parameter area"): their only edge is the bottom one.
		{ [](const OutlinedPart& part) { return hasProp(part, "paramSelector=true"); }, false,
			"parameter selector: underline only" },
		{ [](const OutlinedPart& part) {
			 return part.cls == QLatin1String("QComboBox::drop-down") && part.widget != nullptr
				 && part.widget->property("paramSelector").toBool();
		 },
			false, "parameter selector: underline only" },
		// Value scrub boxes and the LCD readouts are display wells (rule 7):
		// a recess with a lit lower lip, not a cap or a field frame.
		{ [](const OutlinedPart& part) {
			 return hasProp(part, "valueScrub=true") || part.name == QLatin1String("ValueScrubBox");
		 },
			false, "display well (value scrub)" },
		// The comment card's text is the note tape: printed on the plate,
		// framed only while it is being edited.
		{ [](const OutlinedPart& part) { return part.name == QLatin1String("CommentCardText"); }, true,
			"comment tape: framed only while edited" },
		// Seats that are not channels of the device are printed as dashed
		// wire outlines (rack.md, virtual seats): the dashes leave gaps by
		// design, which a median over the side reads as no edge.
		{ [](const OutlinedPart& part) {
			 return part.name == QLatin1String("NewChannelChip") || part.name == QLatin1String("StageChipUnknown")
				 || hasProp(part, "customChannel=true");
		 },
			false, "dashed printed seat" },
		// The toast's close key is a glyph on the notice at rest, like the
		// plain tool buttons; it grows a cap when lit.
		{ [](const OutlinedPart& part) { return part.name == QLatin1String("UpdateToastClose"); }, true,
			"toast close: bare glyph at rest" },
		// The tab close button is a glyph on the tab, not a cap.
		{ [](const OutlinedPart& part) { return part.cls == QLatin1String("CloseButton"); }, false,
			"tab close glyph" },
	};
	return table;
}

const Exemption* exemptionFor(const OutlinedPart& part)
{
	for (const Exemption& row : exemptions())
	{
		if (row.matches(part) && (!row.atRestOnly || !part.lit))
			return &row;
	}
	return nullptr;
}

// Where a sheet's margin seats a cap well inside its widget, the cap's
// outline is that far in: the title bar's window keys carry a 5 px 3 px
// margin in both Rack sheets. A 1 px margin needs no row (the offsets reach
// it).
QMargins insetFor(const QWidget* widget)
{
	const QString name = widget->objectName();
	if (name == QLatin1String("TitleBarMin") || name == QLatin1String("TitleBarMax")
		|| name == QLatin1String("TitleBarClose"))
		return QMargins(3, 5, 3, 5);
	return QMargins();
}

// The one side where a lit part may carry a second colour: the engaged
// Channel key's lamp row and the engaged Stage key's top bar (rule 12).
bool isLampSide(const OutlinedPart& part, Side side)
{
	if (!hasState(part, "checked"))
		return false;
	if (part.name == QLatin1String("ChannelChip"))
		return side == Side::Bottom;
	if (part.name == QLatin1String("StageChip"))
		return side == Side::Top;
	return false;
}

QString widgetProps(const QWidget* widget)
{
	QStringList props;
	for (const QByteArray& name : widget->dynamicPropertyNames())
	{
		const QVariant value = widget->property(name.constData());
		if (value.typeId() == QMetaType::Bool || value.typeId() == QMetaType::QString)
			props.append(QString::fromLatin1(name) + QLatin1Char('=') + value.toString());
	}
	QString text = props.join(QLatin1Char(','));
	text.replace(QLatin1Char('\t'), QLatin1Char(' '));
	text.replace(QLatin1Char('\n'), QLatin1Char(' '));
	return text;
}

struct WidgetState
{
	QString words;
	bool lit = false;
};

WidgetState widgetState(const QWidget* widget)
{
	QStringList words;
	bool lit = false;
	words.append(widget->isEnabled() ? QStringLiteral("enabled") : QStringLiteral("disabled"));
	if (const QAbstractButton* button = qobject_cast<const QAbstractButton*>(widget))
	{
		if (button->isChecked())
		{
			words.append(QStringLiteral("checked"));
			lit = true;
		}
		if (button->isDown())
		{
			words.append(QStringLiteral("down"));
			lit = true;
		}
	}
	if (widget->testAttribute(Qt::WA_UnderMouse) && widget->isEnabled())
	{
		words.append(QStringLiteral("hover"));
		lit = true;
	}
	if (widget->hasFocus())
	{
		words.append(QStringLiteral("focus"));
		lit = true;
	}
	return { words.join(QLatin1Char(' ')), lit };
}

// The rectangle and sides a part is measured on. A combo's drop-down edge
// is the combo's own top, bottom and right edge along the arrow's span.
QRect measuredRect(const OutlinedPart& part, std::vector<Side>& sides)
{
	if (part.cls == QLatin1String("QComboBox::drop-down"))
		sides = { Side::Top, Side::Bottom, Side::Right };
	else
		sides = { Side::Top, Side::Bottom, Side::Left, Side::Right };
	return part.rect;
}

struct PartVerdict
{
	std::array<SideReading, 4> readings;
	std::vector<Side> sides;
	bool sideFailure = false;
	int reviewSides = 0;
	bool colourFailure = false;
	double colourSpread = 0.0;
};

PartVerdict judgePart(const QImage& image, const OutlinedPart& part)
{
	PartVerdict verdict;
	const QRect rect = measuredRect(part, verdict.sides);
	for (Side side : verdict.sides)
	{
		SideReading& reading = verdict.readings[static_cast<int>(side)];
		reading = measureSide(image, rect, side, part);
		if (!reading.measured)
			continue;
		if (reading.value < kFailLevel)
			verdict.sideFailure = true;
		else if (reading.value < kReviewLevel)
			verdict.reviewSides++;
	}
	if (part.lit)
	{
		std::vector<QRgb> colours;
		for (Side side : verdict.sides)
		{
			const SideReading& reading = verdict.readings[static_cast<int>(side)];
			if (reading.measured && !isLampSide(part, side))
				colours.push_back(reading.edge);
		}
		for (size_t i = 0; i < colours.size(); i++)
			for (size_t j = i + 1; j < colours.size(); j++)
				verdict.colourSpread = std::max(verdict.colourSpread, deltaE(colours[i], colours[j]));
		verdict.colourFailure = verdict.colourSpread > kLitColourSpread;
	}
	return verdict;
}

QString hexColour(QRgb rgb)
{
	return QStringLiteral("#%1").arg(rgb & 0xFFFFFF, 6, 16, QLatin1Char('0')).toUpper();
}

QString readingsText(const PartVerdict& verdict)
{
	QStringList text;
	for (Side side : verdict.sides)
	{
		const SideReading& reading = verdict.readings[static_cast<int>(side)];
		text.append(QStringLiteral("%1=%2").arg(QLatin1String(sideName(side)),
			reading.measured ? QString::number(reading.value, 'f', 1) : QStringLiteral("-")));
	}
	return text.join(QLatin1Char(' '));
}

QString partText(const OutlinedPart& part)
{
	return QStringLiteral("%1#%2 [%3] {%4} at %5,%6 %7x%8")
		.arg(part.cls, part.name, part.state, part.props)
		.arg(part.rect.x())
		.arg(part.rect.y())
		.arg(part.rect.width())
		.arg(part.rect.height());
}

// Appends every measured side to EAPO_GALLERY_OUTLINE_REPORT, one TSV line
// each: skin, mode, scene, state, class, name, properties, x, y, w, h, side,
// value, verdict, then the part's own state and the two colours compared.
class OutlineReport
{
public:
	OutlineReport()
	{
		const QString path = qEnvironmentVariable("EAPO_GALLERY_OUTLINE_REPORT");
		if (path.isEmpty())
			return;
		file.setFileName(path);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
			qWarning("SkinGallery: cannot append to the outline report %s", qPrintable(path));
	}

	void write(const QString& skinId, const QString& mode, const QString& scene, const QString& state,
		const OutlinedPart& part, const QString& side, const QString& value, const QString& verdict,
		const QString& outline = QString(), const QString& outside = QString())
	{
		if (!file.isOpen())
			return;
		const QStringList fields = { skinId, mode, scene, state, part.cls, part.name, part.props,
			QString::number(part.rect.x()), QString::number(part.rect.y()), QString::number(part.rect.width()),
			QString::number(part.rect.height()), side, value, verdict, part.state, outline, outside };
		file.write(fields.join(QLatin1Char('\t')).toUtf8() + '\n');
	}

private:
	QFile file;
};

void reportPart(OutlineReport& report, const QString& skinId, const QString& mode, const QString& scene,
	const QString& state, const OutlinedPart& part, const PartVerdict& verdict)
{
	for (Side side : verdict.sides)
	{
		const SideReading& reading = verdict.readings[static_cast<int>(side)];
		QString word;
		if (!reading.measured)
			word = QStringLiteral("unmeasured");
		else if (reading.value < kFailLevel)
			word = QStringLiteral("fail");
		else if (reading.value < kReviewLevel)
			word = QStringLiteral("review");
		else
			word = QStringLiteral("ok");
		report.write(skinId, mode, scene, state, part, QLatin1String(sideName(side)),
			reading.measured ? QString::number(reading.value, 'f', 1) : QString(), word,
			reading.measured ? hexColour(reading.outline) : QString(),
			reading.measured ? hexColour(reading.outside) : QString());
	}
	if (part.lit)
	{
		report.write(skinId, mode, scene, state, part, QStringLiteral("colour"),
			QString::number(verdict.colourSpread, 'f', 1),
			verdict.colourFailure ? QStringLiteral("fail") : QStringLiteral("ok"));
	}
}
}

QList<OutlinedPart> collectOutlinedParts(QWidget* root)
{
	QList<OutlinedPart> parts;
	QList<QWidget*> widgets{ root };
	widgets.append(root->findChildren<QWidget*>());
	for (QWidget* w : widgets)
	{
		if (w != root && !w->isVisibleTo(root))
			continue;
		if (w->width() <= 0 || w->height() <= 0)
			continue;
		// A child that is its own window (a combo's popup, a menu) is not
		// part of this grab.
		bool ownWindow = false;
		for (QWidget* p = w; p != nullptr && p != root; p = p->parentWidget())
			ownWindow = ownWindow || p->isWindow();
		if (ownWindow)
			continue;

		const QPoint origin = w == root ? QPoint(0, 0) : w->mapTo(root, QPoint(0, 0));
		QRect visible(QPoint(0, 0), root->size());
		for (QWidget* p = w->parentWidget(); p != nullptr && w != root && p != root; p = p->parentWidget())
			visible &= QRect(p->mapTo(root, QPoint(0, 0)), p->size());
		bool inList = false;
		for (const QWidget* p = w; p != nullptr; p = p->parentWidget())
			inList = inList || (qobject_cast<const QFrame*>(p) != nullptr && p->objectName() == QLatin1String("FilterCardRow"));
		const WidgetState state = widgetState(w);
		const QString props = widgetProps(w);

		const auto add = [&](const QString& cls, const QRect& local, const QString& words, bool lit) {
			if (local.width() < 6 || local.height() < 6)
				return;
			OutlinedPart part;
			part.widget = w;
			part.cls = cls;
			part.name = w->objectName();
			part.props = props;
			part.state = words;
			part.lit = lit;
			part.rect = local.translated(origin);
			part.inList = inList;
			part.clipped = (part.rect & visible) != part.rect;
			parts.append(part);
		};

		if (QCheckBox* box = qobject_cast<QCheckBox*>(w))
		{
			QStyleOptionButton opt;
			opt.initFrom(box);
			opt.text = box->text();
			opt.icon = box->icon();
			opt.iconSize = box->iconSize();
			add(QStringLiteral("QCheckBox::indicator"), box->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &opt, box),
				state.words, state.lit);
		}
		else if (QRadioButton* radio = qobject_cast<QRadioButton*>(w))
		{
			QStyleOptionButton opt;
			opt.initFrom(radio);
			opt.text = radio->text();
			opt.icon = radio->icon();
			opt.iconSize = radio->iconSize();
			add(QStringLiteral("QRadioButton::indicator"),
				radio->style()->subElementRect(QStyle::SE_RadioButtonIndicator, &opt, radio), state.words, state.lit);
		}
		else if (QComboBox* combo = qobject_cast<QComboBox*>(w))
		{
			add(QStringLiteral("QComboBox"), w->rect(), state.words, state.lit);
			QStyleOptionComboBox opt;
			opt.initFrom(combo);
			opt.editable = combo->isEditable();
			opt.frame = combo->hasFrame();
			opt.currentText = combo->currentText();
			opt.subControls = QStyle::SC_All;
			const QRect arrow = combo->style()->subControlRect(QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxArrow, combo);
			// The arrow's rectangle sits inside the combo's border: the
			// drop-down edge is the combo's own top, bottom and right edge
			// along the arrow's span.
			if (arrow.isValid())
				add(QStringLiteral("QComboBox::drop-down"), QRect(arrow.left(), 0, w->width() - arrow.left(), w->height()),
					state.words, state.lit);
		}
		else if (qobject_cast<QPushButton*>(w) != nullptr || qobject_cast<QToolButton*>(w) != nullptr)
		{
			add(QString::fromLatin1(w->metaObject()->className()), w->rect().marginsRemoved(insetFor(w)), state.words,
				state.lit);
		}
		else if (qobject_cast<QAbstractSpinBox*>(w) != nullptr
			|| (qobject_cast<QLineEdit*>(w) != nullptr && qobject_cast<QAbstractSpinBox*>(w->parentWidget()) == nullptr
				&& qobject_cast<QComboBox*>(w->parentWidget()) == nullptr))
		{
			// A field lights with the keyboard focus; the pointer over it
			// changes nothing on its frame, so hover alone is not a lit state
			// here. The line edit inside a spin box or an editable combo is
			// that control's field, measured with its owner.
			add(QString::fromLatin1(qobject_cast<QAbstractSpinBox*>(w) != nullptr ? w->metaObject()->className() : "QLineEdit"),
				w->rect(), state.words, w->hasFocus());
		}
		else if (RoutingView* view = qobject_cast<RoutingView*>(w))
		{
			// A painted cap's state is its own (what the view drew), not the
			// view's: the view under the pointer does not light every cap.
			const QString enabled = w->isEnabled() ? QStringLiteral("enabled") : QStringLiteral("disabled");
			for (const QPair<QRect, QString>& painted : view->paintedCaps())
			{
				const QStringList words = painted.second.split(QLatin1Char(' '), Qt::SkipEmptyParts);
				const bool paintedLit = words.contains(QStringLiteral("checked")) || words.contains(QStringLiteral("hover"));
				add(QStringLiteral("PaintedCap"), painted.first,
					painted.second.isEmpty() ? enabled : enabled + QLatin1Char(' ') + painted.second, paintedLit);
			}
		}
	}
	return parts;
}

namespace
{
// The fields that share a line with caps in the card list and take the
// caps' box (the Add channel fields, the Send unit's LATENCY field, the
// patchbay's add-channel field), and the patchbay's factor field, which has
// its own row in the height exceptions.
bool isCapLineField(const QString& name)
{
	return name == QLatin1String("ChannelChipAdd") || name == QLatin1String("NewChannelAdd")
		|| name == QLatin1String("SendLatencyEdit") || name == QLatin1String("PatchbayChannelEditor")
		|| name == QLatin1String("PatchbayEditor");
}

// A part whose height the list cap contract covers: a key, a framed combo,
// a cap a routing view paints, or a field on a cap line. Parameter
// selectors are engraved legends, not caps, and stay outside it.
bool isListCap(const OutlinedPart& part)
{
	if (!part.inList)
		return false;
	if (part.cls == QLatin1String("QComboBox"))
		return !hasProp(part, "paramSelector=true");
	if (part.cls == QLatin1String("QLineEdit"))
		return isCapLineField(part.name);
	return part.cls == QLatin1String("QPushButton") || part.cls == QLatin1String("QToolButton")
		|| part.cls == QLatin1String("PaintedCap");
}

// A list cap that is not the contract's height on purpose. Each row says
// why; the gallery prints every row it uses.
struct HeightException
{
	std::function<bool(const OutlinedPart&)> matches;
	const char* reason;
};

const std::vector<HeightException>& heightExceptions()
{
	static const std::vector<HeightException> table = {
		// The factor field opens over the crosspoint cap it edits, inside
		// the cell; it is not on a line of caps.
		{ [](const OutlinedPart& part) { return part.name == QLatin1String("PatchbayEditor"); },
			"patchbay factor field: opens inside its crosspoint cell" },
	};
	return table;
}

// Rows of a cap's interior (inside its outline, 3 px in from its sides)
// that carry ink: some pixel differs from the row's median colour by more
// than 20. Returns false when the cap holds no ink.
bool inkRows(const QImage& image, const QRect& rect, int bottomOutline, int& clearAbove, int& clearBelow,
	int& inkHeight)
{
	std::vector<bool> rows;
	for (int y = rect.top() + 1; y <= rect.bottom() - bottomOutline; y++)
	{
		std::vector<QPoint> line;
		for (int x = rect.left() + 3; x < rect.left() + rect.width() - 3; x++)
			line.emplace_back(x, y);
		if (line.empty())
			return false;
		const QRgb median = medianColour(image, line);
		bool ink = false;
		for (const QPoint& point : line)
			ink = ink || deltaE(image.pixel(point), median) > 20.0;
		rows.push_back(ink);
	}
	int first = -1;
	int last = -1;
	for (int i = 0; i < static_cast<int>(rows.size()); i++)
	{
		if (!rows[i])
			continue;
		if (first < 0)
			first = i;
		last = i;
	}
	if (first < 0)
		return false;
	clearAbove = first;
	clearBelow = static_cast<int>(rows.size()) - 1 - last;
	inkHeight = last - first + 1;
	return true;
}

// The list cap contract on one capture: every cap in the filter list is
// contract.listCapHeight tall in every state, and its legend's ink keeps
// at least 2 px clear of the outline above and below. The fill block's
// painted caps are custom widgets outside the outline check; they are
// listed here by class so the inventory covers them.
int checkListCaps(QWidget* grabbed, const QImage& pixels, const QList<OutlinedPart>& parts, int capHeight,
	OutlineReport& report, const QString& skinId, const QString& mode, const QString& scene, const QString& state,
	int& checkedCaps)
{
	int failing = 0;
	for (const OutlinedPart& part : parts)
	{
		if (!isListCap(part))
			continue;
		const HeightException* exception = nullptr;
		for (const HeightException& row : heightExceptions())
			if (row.matches(part))
				exception = &row;
		if (exception != nullptr)
		{
			report.write(skinId, mode, scene, state, part, QStringLiteral("height"), QString::number(part.rect.height()),
				QStringLiteral("height exception: ") + QLatin1String(exception->reason));
			continue;
		}
		checkedCaps++;
		const bool tall = part.rect.height() == capHeight;
		report.write(skinId, mode, scene, state, part, QStringLiteral("height"), QString::number(part.rect.height()),
			tall ? QStringLiteral("ok") : QStringLiteral("fail"));
		if (!tall)
		{
			qWarning("SkinGallery: OUTLINE FAIL height %s %s %s %s: %s: %d px, the list cap height is %d",
				qPrintable(skinId), qPrintable(mode), qPrintable(scene), qPrintable(state), qPrintable(partText(part)),
				part.rect.height(), capHeight);
			failing++;
			continue;
		}
		if (part.clipped || !QRect(QPoint(0, 0), pixels.size()).contains(part.rect))
			continue;
		// A focused field's caret runs the height of its text box; it is
		// not the legend, so a focused field's ink is not measured.
		if (part.cls == QLatin1String("QLineEdit") && hasState(part, "focus"))
			continue;
		// Qt draws a 1 px dashed bottom border two pixels thick, so a
		// dashed seat's outline takes the bottom two rows.
		const bool dashed = part.name == QLatin1String("NewChannelChip")
			|| part.name == QLatin1String("StageChipUnknown") || hasProp(part, "customChannel=true");
		int above = 0;
		int below = 0;
		int ink = 0;
		if (!inkRows(pixels, part.rect, dashed ? 2 : 1, above, below, ink))
			continue;
		const bool clear = above >= 2 && below >= 2;
		report.write(skinId, mode, scene, state, part, QStringLiteral("ink"),
			QStringLiteral("%1/%2/%3").arg(ink).arg(above).arg(below),
			clear ? QStringLiteral("ok") : QStringLiteral("fail"));
		if (!clear)
		{
			qWarning("SkinGallery: OUTLINE FAIL ink %s %s %s %s: %s: ink %d px, %d px clear above, %d below (2 needed)",
				qPrintable(skinId), qPrintable(mode), qPrintable(scene), qPrintable(state), qPrintable(partText(part)), ink,
				above, below);
			failing++;
		}
	}
	for (QWidget* w : grabbed->findChildren<QWidget*>())
	{
		const bool fillCap = qstrcmp(w->metaObject()->className(), "VSTSlotFillCell") == 0
			|| w->objectName() == QLatin1String("VSTSlotFillLatch");
		if (!fillCap || !w->isVisibleTo(grabbed))
			continue;
		OutlinedPart part;
		part.widget = w;
		part.cls = QString::fromLatin1(w->metaObject()->className());
		part.name = w->objectName();
		part.state = widgetState(w).words;
		part.rect = QRect(w->mapTo(grabbed, QPoint(0, 0)), w->size());
		// The fill block's value caps and FILL cap are painted 18 px tall
		// by RackSkin::vstSlotFillBlockMetrics, settled with the fill block
		// itself; they are not sheet caps.
		report.write(skinId, mode, scene, state, part, QStringLiteral("height"), QString::number(part.rect.height()),
			QStringLiteral("height exception: VST fill block cap, painted by the fill block's own metrics"));
	}
	return failing;
}
}

int checkControlOutlines(QWidget* grabbed, const QImage& image, const QString& skinId, const QString& mode,
	const QString& scene, const QString& state)
{
	const SkinControlContract contract = SkinManager::instance()->controlContract();
	if (!contract.closedOutlines && contract.listCapHeight <= 0)
		return 0;
	const QImage pixels = image.convertToFormat(QImage::Format_RGB32);
	const QList<OutlinedPart> parts = collectOutlinedParts(grabbed);
	OutlineReport report;
	int listCaps = 0;
	const int capFailures = contract.listCapHeight > 0
		? checkListCaps(grabbed, pixels, parts, contract.listCapHeight, report, skinId, mode, scene, state, listCaps)
		: 0;
	if (listCaps > 0)
	{
		qWarning("SkinGallery: list caps %s %s %s %s: %d caps, %d failing", qPrintable(skinId), qPrintable(mode),
			qPrintable(scene), qPrintable(state), listCaps, capFailures);
	}
	if (!contract.closedOutlines)
		return capFailures;
	int failing = capFailures;
	int measuredParts = 0;
	int reviewSides = 0;
	for (const OutlinedPart& part : parts)
	{
		// A part an ancestor cuts, or one that runs off the capture, has
		// sides that are not in the picture.
		if (part.clipped || !QRect(QPoint(0, 0), pixels.size()).contains(part.rect))
			continue;
		if (const Exemption* exemption = exemptionFor(part))
		{
			report.write(skinId, mode, scene, state, part, QString(), QString(),
				QStringLiteral("exempt: ") + QLatin1String(exemption->reason));
			continue;
		}
		measuredParts++;
		const PartVerdict verdict = judgePart(pixels, part);
		reportPart(report, skinId, mode, scene, state, part, verdict);
		reviewSides += verdict.reviewSides;
		if (verdict.sideFailure)
		{
			qWarning("SkinGallery: OUTLINE FAIL side %s %s %s %s: %s: %s", qPrintable(skinId), qPrintable(mode),
				qPrintable(scene), qPrintable(state), qPrintable(partText(part)), qPrintable(readingsText(verdict)));
		}
		if (verdict.colourFailure)
		{
			QStringList colours;
			for (Side side : verdict.sides)
			{
				const SideReading& reading = verdict.readings[static_cast<int>(side)];
				if (reading.measured)
					colours.append(QStringLiteral("%1=%2%3").arg(QLatin1String(sideName(side)), hexColour(reading.edge),
						isLampSide(part, side) ? QStringLiteral("(lamp)") : QString()));
			}
			qWarning("SkinGallery: OUTLINE FAIL colour %s %s %s %s: %s: spread %.1f, %s", qPrintable(skinId),
				qPrintable(mode), qPrintable(scene), qPrintable(state), qPrintable(partText(part)), verdict.colourSpread,
				qPrintable(colours.join(QLatin1Char(' '))));
		}
		if (verdict.sideFailure || verdict.colourFailure)
			failing++;
	}
	if (measuredParts > 0)
	{
		qWarning("SkinGallery: outlines %s %s %s %s: %d parts, %d failing, %d sides under review", qPrintable(skinId),
			qPrintable(mode), qPrintable(scene), qPrintable(state), measuredParts, failing, reviewSides);
	}
	return failing;
}

int checkFocusLightsOutlines(const QString& configPath, const QString& skinId, const QString& mode)
{
	const SkinControlContract contract = SkinManager::instance()->controlContract();
	if (!contract.closedOutlines)
		return 0;

	QList<QString> lines;
	for (const GalleryRow& row : galleryRows())
		lines.append(row.line);
	QScrollArea scrollArea;
	scrollArea.resize(960, 720);
	const QList<FilterCardRow*> rows = buildRows(scrollArea, configPath, lines);
	QWidget* table = scrollArea.widget();
	if (rows.isEmpty() || table == nullptr)
	{
		qWarning("SkinGallery: OUTLINE FAIL focus %s %s: the focus self test could not build the rows", qPrintable(skinId),
			qPrintable(mode));
		return 1;
	}
	// Offscreen, a widget takes keyboard focus only inside the active window.
	scrollArea.activateWindow();
	QApplication::processEvents();

	OutlineReport report;
	int failures = 0;
	int tested = 0;
	QSet<QString> seen;
	const QString scene = QStringLiteral("focus-selftest");
	for (const OutlinedPart& listed : collectOutlinedParts(table))
	{
		QWidget* w = listed.widget;
		const bool cap = qobject_cast<QAbstractButton*>(w) != nullptr || qobject_cast<QComboBox*>(w) != nullptr
			|| qobject_cast<QLineEdit*>(w) != nullptr || qobject_cast<QAbstractSpinBox*>(w) != nullptr;
		if (!cap || qobject_cast<QCheckBox*>(w) != nullptr || qobject_cast<QRadioButton*>(w) != nullptr)
			continue;
		if (listed.cls == QLatin1String("QComboBox::drop-down"))
			continue;
		if (!w->isEnabled() || (w->focusPolicy() & Qt::TabFocus) == 0)
			continue;
		// Exempt for good (not only at rest): the part draws no outline to
		// light.
		const Exemption* always = nullptr;
		for (const Exemption& row : exemptions())
			if (row.matches(listed) && !row.atRestOnly)
				always = &row;
		if (always != nullptr)
			continue;
		const QAbstractButton* button = qobject_cast<QAbstractButton*>(w);
		const QString key = QStringLiteral("%1#%2|%3|%4").arg(listed.cls, listed.name,
			button != nullptr && button->isChecked() ? QStringLiteral("checked") : QString(), listed.props);
		if (seen.contains(key))
			continue;
		seen.insert(key);

		QWidget* row = nullptr;
		for (QWidget* p = w; p != nullptr && row == nullptr; p = p->parentWidget())
			if (qobject_cast<FilterCardRow*>(p) != nullptr)
				row = p;
		if (row == nullptr)
			continue;
		tested++;

		if (QWidget* holder = QApplication::focusWidget())
			holder->clearFocus();
		QApplication::processEvents();
		const QImage resting = row->grab().toImage().convertToFormat(QImage::Format_RGB32);
		OutlinedPart part = listed;
		part.rect = QRect(w->mapTo(row, QPoint(0, 0)), w->size());

		w->setFocus(Qt::TabFocusReason);
		QApplication::processEvents();
		if (!w->hasFocus())
		{
			qWarning("SkinGallery: OUTLINE FAIL focus %s %s: %s cannot take the keyboard focus", qPrintable(skinId),
				qPrintable(mode), qPrintable(partText(part)));
			failures++;
			continue;
		}
		const QImage focused = row->grab().toImage().convertToFormat(QImage::Format_RGB32);
		const WidgetState focusedState = widgetState(w);
		part.state = focusedState.words;
		part.lit = true;
		const PartVerdict verdict = judgePart(focused, part);

		// The focused outline against the resting one, on the pixel line the
		// focused outline was found on.
		QStringList changes;
		bool unlit = false;
		for (Side side : verdict.sides)
		{
			const SideReading& reading = verdict.readings[static_cast<int>(side)];
			if (!reading.measured)
				continue;
			const std::vector<QPoint> line = sideLine(part.rect, side, reading.edgeOffset, focused.size());
			const double change = line.empty() ? 0.0 : deltaE(medianColour(resting, line), reading.edge);
			if (change < kReviewLevel)
				unlit = true;
			changes.append(QStringLiteral("%1=%2").arg(QLatin1String(sideName(side)), QString::number(change, 'f', 1)));
			report.write(skinId, mode, scene, QStringLiteral("focus"), part, QLatin1String(sideName(side)),
				QString::number(change, 'f', 1), change < kReviewLevel ? QStringLiteral("fail") : QStringLiteral("ok"),
				hexColour(reading.edge), hexColour(medianColour(resting, line)));
		}
		// A latched key's outline is lit already and stays as it is while
		// focused; it must still be one colour on four sides. The LOCATE
		// cap rests lit in the service signal (rack.md, reference card),
		// which in the light finish is the focus signal itself.
		const bool latched = (button != nullptr && button->isChecked()) || hasProp(listed, "rackLocate=true");
		const bool failed = (unlit && !latched) || verdict.colourFailure || verdict.sideFailure;
		qWarning("SkinGallery: focus %s %s %s#%s: change %s, colour spread %.1f%s", qPrintable(skinId), qPrintable(mode),
			qPrintable(part.cls), qPrintable(part.name), qPrintable(changes.join(QLatin1Char(' '))), verdict.colourSpread,
			failed ? " FAIL" : "");
		if (failed)
		{
			qWarning("SkinGallery: OUTLINE FAIL focus %s %s: %s: change %s, outline %s, spread %.1f", qPrintable(skinId),
				qPrintable(mode), qPrintable(partText(part)), qPrintable(changes.join(QLatin1Char(' '))),
				qPrintable(readingsText(verdict)), verdict.colourSpread);
			failures++;
		}
		w->clearFocus();
		QApplication::processEvents();
	}
	if (tested == 0)
	{
		qWarning("SkinGallery: OUTLINE FAIL focus %s %s: no focusable cap was found", qPrintable(skinId), qPrintable(mode));
		failures++;
	}
	return failures;
}
}
