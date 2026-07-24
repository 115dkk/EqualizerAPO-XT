#pragma once

#include <QEvent>
#include <QPainter>
#include <QString>
#include <QToolBar>
#include <QWidget>

#include "Editor/SkinManager.h"

// Shared lifecycle for paint-only toolbar chrome.  The overlay contract is
// deliberately centralized here: it never accepts input, never receives a
// QSS background, tracks the toolbar geometry, suspends outside its owning
// skin, and reasserts its z-order after every style change/reuse.
class SkinChromeOverlay : public QWidget
{
public:
	enum class ZPolicy
	{
		BelowControls,
		AboveControls
	};

	SkinChromeOverlay(QToolBar* toolBar, const QString& objectName,
		const QString& ownerSkinId, ZPolicy zPolicy)
		: QWidget(toolBar),
		toolBar(toolBar),
		ownerSkinId(ownerSkinId),
		zPolicy(zPolicy)
	{
		setObjectName(objectName);
		setAttribute(Qt::WA_TransparentForMouseEvents);
		setAttribute(Qt::WA_NoSystemBackground, true);
		toolBar->installEventFilter(this);
		setGeometry(toolBar->rect());
		// Derived overlays finish wiring their active-state dependants before
		// refreshOverlay() invokes the virtual callback.
		setVisible(isOwnerActive());
		reassertZOrder();
	}

	void refreshOverlay()
	{
		setGeometry(toolBar->rect());
		syncActiveState();
		reassertZOrder();
		update();
	}

protected:
	bool eventFilter(QObject* watched, QEvent* event) override
	{
		if (watched == toolBar)
		{
			if (event->type() == QEvent::Resize)
				setGeometry(toolBar->rect());
			if (event->type() == QEvent::StyleChange)
			{
				syncActiveState();
				reassertZOrder();
			}
		}
		return QWidget::eventFilter(watched, event);
	}

	void paintEvent(QPaintEvent*) final
	{
		if (!isOwnerActive())
			return;
		QPainter painter(this);
		paintChrome(painter);
	}

	virtual void paintChrome(QPainter& painter) = 0;
	virtual void ownerActiveChanged(bool) {}

	QToolBar* parentToolBar() const { return toolBar; }
	bool isOwnerActive() const
	{
		return SkinManager::instance()->currentSkinId() == ownerSkinId;
	}

private:
	void syncActiveState()
	{
		const bool active = isOwnerActive();
		setVisible(active);
		ownerActiveChanged(active);
	}

	void reassertZOrder()
	{
		if (zPolicy == ZPolicy::BelowControls)
			lower();
		else
			raise();
	}

	QToolBar* toolBar;
	QString ownerSkinId;
	ZPolicy zPolicy;
};
