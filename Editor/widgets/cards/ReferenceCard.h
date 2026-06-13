/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	A reference card is the shared body chrome for command rows whose subject is
	an external file the configuration points at: Include (a config file) and
	VSTPlugin (a plugin library). Both used to render as a path-input form - a
	bare QLineEdit with the path plus a "File not found" caption. The adversarial
	review (AR2 X-1/X-3/X-6/X-7/X-9/X-10) found that DAWs, IDEs and design tools
	all present such a reference as a named entity: an icon, a primary name, the
	directory as secondary metadata, status as a row-state transition (not a
	caption), and a recovery affordance when the reference breaks. This widget is
	the neutral base that carries that information hierarchy; per-skin identity is
	layered on through QSS targeting the object names and dynamic properties it
	sets (refCard, refMissing, ...).

	The card is purely presentational. The host editor owns all behavior (path
	resolution, plugin lifecycle, file dialogs) and drives the card through
	setState(); the card emits intent signals the host connects to.
*/

#pragma once

#include <QString>
#include <QWidget>

class QHBoxLayout;
class QLabel;
class QLineEdit;
class QToolButton;

// One render state of a reference card. The host computes it (resolving the
// path/library, probing existence) and hands it to ReferenceCard::setState.
struct ReferenceCardState
{
	// Primary label: the plugin display name (VST) or the file name (Include).
	QString name;
	// Secondary metadata: the directory the reference lives in. Middle-elided
	// for display; the full string is exposed as a tooltip.
	QString directory;
	// Full reference string (full path), shown as the name's tooltip and used as
	// the QLineEdit text in edit mode.
	QString fullPath;
	// Short format token shown as a badge ("VST2"); empty hides the badge.
	QString formatBadge;
	// True when the reference target was not found / could not be resolved.
	bool missing = false;
	// True when the reference is an absolute path (config portability hazard).
	bool absolutePath = false;
	// True when the primary name should act as a click affordance (open panel /
	// jump to file). Ignored while missing.
	bool nameClickable = false;
	// Optional tooltip for the clickable name (e.g. a file preview).
	QString nameTooltip;
	// Short status line under the name; empty hides it. Severity drives styling.
	QString statusText;
	enum class Severity { None, Warning, Critical };
	Severity statusSeverity = Severity::None;
};

class ReferenceCard : public QWidget
{
	Q_OBJECT

public:
	// kind is "include" or "vst"; it namespaces the object names so a skin's QSS
	// can target each reference type separately (IncludeRefName vs VstRefName).
	explicit ReferenceCard(const QString& kind, QWidget* parent = nullptr);

	void setState(const ReferenceCardState& state);

	// Hand the host an action button to host its own action (Locate when
	// missing, otherwise nothing extra). The button is created lazily and lives
	// in the action area; callers set its text/icon and connect its clicked().
	QToolButton* addActionButton(const QString& objectName);

	// Place a host-owned widget (e.g. the VST Open panel button) in the action
	// area, before the pencil edit button. The widget is reparented onto the
	// card.
	void addActionWidget(QWidget* widget);

	// Switch between the display card and the inline path editor. The editor is
	// pre-filled with the current full path and selected; on commit the card
	// emits pathEdited(newPath).
	void enterEditMode();
	void leaveEditMode();
	bool isEditing() const;

signals:
	// The clickable primary name was activated (open panel / jump to file).
	void nameActivated();
	// The edit-mode line edit committed a (possibly unchanged) path.
	void pathEdited(const QString& newPath);
	// The user asked to leave the reference card and edit the path.
	void editRequested();

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
	void onEditCommitted();

private:
	// Neutral token-driven base styling applied at construction. Rows are
	// rebuilt on every skin switch, so this always matches the active skin's
	// tokens. Skins layer their own identity through paintCardChrome and (in a
	// later skin round) QSS targeting the object names / refMissing property.
	void applyNeutralStyle();
	static QString elide(const QLabel* label, const QString& text);

	QString kind;
	ReferenceCardState lastState;

	QHBoxLayout* actionLayout = nullptr;
	QWidget* displayWidget = nullptr;
	QLabel* iconLabel = nullptr;
	QLabel* nameLabel = nullptr;
	QLabel* dirLabel = nullptr;
	QLabel* statusLabel = nullptr;
	QLabel* formatBadge = nullptr;
	QLabel* absBadge = nullptr;
	QLabel* missingBadge = nullptr;
	QToolButton* editButton = nullptr;

	QLineEdit* pathEdit = nullptr;
	bool editing = false;
	bool editCommitting = false;
};
