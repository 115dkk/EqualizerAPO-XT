/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "CurvedNodeRoutingRenderer.h"

#include <QGraphicsView>
#include <QVBoxLayout>

#include "Editor/guis/CopyFilterGUIScene.h"

using std::vector;

CurvedNodeView::CurvedNodeView(const vector<Assignment>& assignments,
	const vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
	QWidget* parent)
	: RoutingView(parent)
{
	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	setMinimumSize(0, 0);

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	scene = new CopyFilterGUIScene;
	view = new QGraphicsView(scene, this);
	view->setObjectName(QStringLiteral("CopyRoutingGraphView"));
	view->setBackgroundRole(QPalette::Window);
	view->setRenderHint(QPainter::Antialiasing, true);
	view->setMinimumHeight(120);
	view->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	layout->addWidget(view);

	scene->load(channelNames, assignments, portModel.fixedSources, portModel.allowFactors);

	connect(scene, SIGNAL(updateModel()), this, SIGNAL(routingChanged()));
}

std::vector<Assignment> CurvedNodeView::assignments() const
{
	return scene->buildAssignments();
}

RoutingView* CurvedNodeRoutingRenderer::create(const vector<Assignment>& assignments,
	const vector<std::wstring>& channelNames, const RoutingPortModel& portModel, QWidget* parent)
{
	return new CurvedNodeView(assignments, channelNames, portModel, parent);
}
