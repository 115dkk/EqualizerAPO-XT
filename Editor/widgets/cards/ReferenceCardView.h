/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	A reference card is the body of a command row whose subject is an external
	file the configuration points at: Include (a config file), Convolution /
	MultiConvolution (an impulse response) and VSTPlugin (a plugin library).
	The reference is presented as a *named entity* - a primary name, the
	location as secondary metadata, the broken state as a visual transition of
	the item itself, and a recovery affordance (Locate) right at the error -
	not as a path-input form. See docs/skin-hooks.md ("Reference card hook").

	ReferenceCardView is the skin seam for that presentation: the host editor
	owns all behavior (path resolution, file dialogs, plugin lifecycle,
	import) and describes itself through ReferenceCardState; each skin
	supplies its own view subclass via ISkin::createReferenceCardView so the
	skins can answer with genuinely different structures, not palette swaps.
	The base class owns the one interaction every skin shares: the inline
	path editor (interaction is shared across skins; skins only restyle it).
*/

#pragma once

#include <QString>
#include <QStringList>
#include <QHash>
#include <QWidget>

class QAbstractButton;
class QLineEdit;
class QStackedLayout;

// One render state of a reference card. The host editor computes it (resolving
// the reference, probing the target) and hands it to setState whenever it
// changes; views render it and never mutate it.
struct ReferenceCardState
{
	// "include", "convolution", "multiconvolution" or "vst".
	QString kind;
	// Primary label: the plugin display name (VST, once the library loaded) or
	// the target file name.
	QString name;
	// Secondary metadata: where the reference points, in native separators.
	// For relative references this is the as-written parent (empty when the
	// file sits next to the config); for absolute ones the absolute directory.
	// Empty hides the location line.
	QString directory;
	// Fully resolved absolute path, for tooltips. May be empty while missing.
	QString fullPath;
	// The reference exactly as written in the config line; what the inline
	// path editor edits (so relative references stay relative).
	QString editText;
	// Short format token ("VST2", "VST3"); empty hides the badge.
	QString formatBadge;
	// The target could not be resolved (file not found / library not loaded).
	// The skins' required broken-state transition keys off this.
	bool missing = false;
	// The reference is an absolute path - a config portability hazard the
	// skins may badge.
	bool absolutePath = false;
	// The primary name is a click affordance (open panel for VST, jump to the
	// included config). Views must ignore it while missing.
	bool nameClickable = false;
	// Measured facts about a resolved target, in display order (e.g. "100.0 ms",
	// "48000 Hz", "2 ch" for an impulse response). Views render these in their
	// own readout idiom; empty hides the readout.
	QStringList readout;
	// One-line status. Empty means the reference is healthy and quiet.
	QString statusText;
	enum class Severity
	{
		None,
		Warning,
		Critical
	};
	Severity statusSeverity = Severity::None;

	// The location as the containing prefix: the directory closed by its
	// trailing separator ("Surround\"). Views print this instead of the bare
	// folder name so the depiction keeps the real containment direction - the
	// folder holds the file; a bare name hanging off the file read as if the
	// folder were the file's child. Empty while directory is empty.
	QString locationPrefix() const;
};

bool referenceCardNeedsLocate(const ReferenceCardState& state);
QString referenceCardSeverityName(ReferenceCardState::Severity severity);

class ReferenceCardView : public QWidget
{
	Q_OBJECT

public:
	// Semantic roles for host-owned action buttons. The host creates the
	// buttons and owns their behavior (connections, enabled state); the view
	// owns their placement and may restyle them in its own language.
	enum class ActionRole
	{
		// Open the file dialog. Doubles as the Locate recovery entry while the
		// reference is missing; views surface it accordingly.
		Browse,
		// Jump to the target (open the included config in the editor).
		OpenTarget,
		// Copy the target into the config directory. The host toggles
		// visibility; views only place it.
		Import,
		// Open the plugin's own panel (VST).
		OpenPanel,
		// Auxiliary menu (VST embed toggle).
		Options,
		// Switch the card into inline path editing.
		EditPath
	};

	explicit ReferenceCardView(QWidget* parent = nullptr);

	// Host wiring, called once after construction and before the first
	// setState, in display order.
	void addActionButton(ActionRole role, QAbstractButton* button);
	// An extra interactive control that is part of the reference grammar
	// itself (MultiConvolution's output-channel selector). Placement is the
	// view's decision.
	virtual void addLeadingWidget(QWidget* widget) = 0;

	// Stores the state, lets the subclass apply it, and re-evaluates QSS
	// against the refreshed dynamic properties (refKind/refMissing).
	void setState(const ReferenceCardState& state);
	const ReferenceCardState& state() const;

	// Swap the card content for the shared inline path editor, seeded with the
	// as-written reference. Committing (or focus loss) leaves edit mode and
	// emits pathCommitted; Escape leaves without committing.
	void enterEditMode();

signals:
	// The primary name was activated (clicked) while nameClickable.
	void nameActivated();
	// The inline editor committed a new as-written reference.
	void pathCommitted(const QString& text);

protected:
	// Subclasses place the already-registered host button in their visual
	// structure; role ownership and shared state properties stay in the base.
	virtual void placeActionButton(ActionRole role, QAbstractButton* button) = 0;
	QAbstractButton* actionButton(ActionRole role) const;
	bool locateMode() const;

	// Apply the already-stored state to the subclass's widgets/painting.
	virtual void applyState(const ReferenceCardState& state) = 0;

	// The page the subclass builds its presentation into.
	QWidget* contentWidget() const;

	// Make widget act as the primary-name click affordance (the identity
	// opens the panel / jumps to the target). Emits nameActivated on
	// left-click while the state allows it and keeps the cursor honest;
	// every view's name label shares this plumbing.
	void installNameActivation(QWidget* widget);

	bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
	void editCommitted();

private:
	void updateSharedProperties();
	void leaveEditMode();

	ReferenceCardState currentState;
	QStackedLayout* stack = nullptr;
	QWidget* content = nullptr;
	QLineEdit* pathEdit = nullptr;
	QWidget* nameActivationWidget = nullptr;
	QHash<int, QAbstractButton*> actionRegistry;
	bool committing = false;
};
