/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2015  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <algorithm>
#include <QKeyEvent>
#include <QGraphicsProxyWidget>
#include <QLineEdit>
#include <QToolButton>

#include "Editor/helpers/GUIHelper.h"
#include "Editor/widgets/ResizingLineEdit.h"
#include "CopyFilterGUIChannelItem.h"
#include "CopyFilterGUIScene.h"

using std::list;
using std::vector;
using std::wstring;

void CopyFilterGUIScene::load(const vector<wstring>& channelNames, vector<Assignment> assignments)
{
	clear();

	QHash<QString, CopyFilterGUIChannelItem*> inputChannelMap;
	QHash<QString, CopyFilterGUIChannelItem*> outputChannelMap;

	QGraphicsItem* lastInputItem = nullptr;
	lastOutputItem = nullptr;

	for (unsigned i = 0; i < channelNames.size(); i++)
	{
		QString c = QString::fromStdWString(channelNames[i]);

		CopyFilterGUIChannelItem* inputItem = new CopyFilterGUIChannelItem(c, false);
		inputItem->setPos(getNextChannelPoint(lastInputItem, false));
		addItem(inputItem);

		inputChannelMap.insert(c, inputItem);
		if (c == "LFE")
			inputChannelMap.insert("SUB", inputItem);
		inputChannelMap.insert(QString().setNum(i + 1), inputItem);
		lastInputItem = inputItem;

		CopyFilterGUIChannelItem* outputItem = new CopyFilterGUIChannelItem(c, true);
		outputItem->setPos(getNextChannelPoint(lastOutputItem, true));
		addItem(outputItem);

		outputChannelMap.insert(c, outputItem);
		if (c == "LFE")
			outputChannelMap.insert("SUB", outputItem);
		outputChannelMap.insert(QString().setNum(i + 1), outputItem);
		lastOutputItem = outputItem;
	}

	CopyFilterGUIChannelItem* noChannelItem = new CopyFilterGUIChannelItem("", false);
	noChannelItem->setPos(getNextChannelPoint(lastInputItem, false));
	addItem(noChannelItem);
	inputChannelMap.insert("", noChannelItem);

	for (Assignment assignment : assignments)
	{
		QString oc = QString::fromStdWString(assignment.targetChannel);
		if (oc == "")
			continue;
		CopyFilterGUIChannelItem* outputItem = outputChannelMap.value(oc.toUpper());
		if (outputItem == nullptr)
		{
			outputItem = new CopyFilterGUIChannelItem(oc, true);
			outputItem->setPos(getNextChannelPoint(lastOutputItem, true));
			addItem(outputItem);

			outputChannelMap.insert(oc, outputItem);
			lastOutputItem = outputItem;
		}
		std::vector<CopyFilterGUIConnectionItem*> targetConnections;
		for (Assignment::Summand summand : assignment.sourceSum)
		{
			QString ic = QString::fromStdWString(summand.channel);
			if (ic == " ")
				continue;
			CopyFilterGUIChannelItem* inputItem = inputChannelMap.value(ic.toUpper());
			if (inputItem == nullptr)
			{
				inputItem = new CopyFilterGUIChannelItem(ic, false);
				inputItem->setPos(getNextChannelPoint(lastInputItem, false));
				addItem(inputItem);

				inputChannelMap.insert(ic, inputItem);
				lastInputItem = inputItem;
			}

			CopyFilterGUIConnectionItem* connItem = new CopyFilterGUIConnectionItem(inputItem, outputItem, summand.factor, summand.isDecibel);
			addItem(connItem);
			targetConnections.push_back(connItem);
		}

		// Spread the coefficient labels of every arrow feeding this output along
		// their own lines. A single source keeps the midpoint; multiple sources
		// (a mix such as VC = 0.7071*L + 0.7071*R) fan the labels between 28% and
		// 72% of each line so the boxes no longer overlap at the shared midpoint.
		const int connectionCount = static_cast<int>(targetConnections.size());
		for (int k = 0; k < connectionCount; k++)
		{
			double t = (connectionCount <= 1) ? 0.5 : 0.28 + 0.44 * k / (connectionCount - 1);
			targetConnections[k]->setLabelPosition(t);
		}
	}

	QToolButton* addButton = new QToolButton;
	addButton->setIcon(QIcon(":/icons/list-add-green.ico"));
	connect(addButton, SIGNAL(clicked()), this, SLOT(addOutputChannel()));
	addProxyItem = addWidget(addButton);
	addProxyItem->setPos(getNextChannelPoint(lastOutputItem, true));

	int margin = GUIHelper::scale(4);
	setSceneRect(itemsBoundingRect().marginsAdded(QMarginsF(margin, margin, margin, margin)));
}

std::vector<Assignment> CopyFilterGUIScene::buildAssignments()
{
	std::vector<CopyFilterGUIConnectionItem*> connItems;

	for (QGraphicsItem* item : items())
	{
		CopyFilterGUIConnectionItem* connItem = qgraphicsitem_cast<CopyFilterGUIConnectionItem*>(item);
		if (connItem != nullptr && connItem->getTarget() != nullptr)
		{
			connItems.push_back(connItem);
		}
	}

	std::sort(connItems.begin(), connItems.end(), [](CopyFilterGUIConnectionItem* a, CopyFilterGUIConnectionItem* b)
	{
		double diff = a->getTarget()->x() - b->getTarget()->x();
		if (diff == 0.0)
			diff = a->getSource()->x() - b->getSource()->x();

		return diff < 0;
	});

	std::vector<Assignment> assignments;
	QHash<QString, Assignment*> channelAssignmentMap;
	for (CopyFilterGUIConnectionItem* connItem : connItems)
	{
		QString oc = connItem->getTarget()->getName();
		Assignment* assignment = channelAssignmentMap.value(oc);
		if (assignment == nullptr)
		{
			Assignment newAssignment;
			newAssignment.targetChannel = oc.toStdWString();
			assignments.push_back(newAssignment);
			assignment = &assignments.back();
			channelAssignmentMap.insert(oc, assignment);
		}

		QString ic = connItem->getSource()->getName();
		Assignment::Summand summand;
		summand.channel = ic.toStdWString();
		summand.factor = connItem->getFactor();
		summand.isDecibel = connItem->getIsDecibel();
		assignment->sourceSum.push_back(summand);
	}

	return assignments;
}

void CopyFilterGUIScene::addOutputChannel()
{
	ResizingLineEdit* lineEdit = new ResizingLineEdit("", true);
	connect(lineEdit, SIGNAL(editingFinished()), this, SLOT(lineEditEditingFinished()));
	connect(lineEdit, SIGNAL(editingCanceled()), this, SLOT(lineEditEditingCanceled()));
	QGraphicsProxyWidget* proxyItem = addWidget(lineEdit);
	QPointF point = getNextChannelPoint(lastOutputItem, true);
	QRectF rect;
	rect.setSize(lineEdit->size());
	rect.moveTo(point);
	proxyItem->setPos(rect.topLeft());
	proxyItem->setZValue(10);
	lineEdit->setFocus();
	addProxyItem->setVisible(false);
}

void CopyFilterGUIScene::lineEditEditingFinished()
{
	QLineEdit* lineEdit = qobject_cast<QLineEdit*>(QObject::sender());
	// might be called twice
	if (!lineEdit->isVisible())
		return;

	QString text = lineEdit->text();
	if (text != "")
	{
		CopyFilterGUIChannelItem* item = new CopyFilterGUIChannelItem(text, true);
		item->setPos(getNextChannelPoint(lastOutputItem, true));
		addItem(item);
		lastOutputItem = item;

		addProxyItem->setPos(getNextChannelPoint(lastOutputItem, true));
	}
	addProxyItem->setVisible(true);

	lineEdit->setVisible(false);
	lineEdit->deleteLater();
}

void CopyFilterGUIScene::lineEditEditingCanceled()
{
	QLineEdit* lineEdit = qobject_cast<QLineEdit*>(QObject::sender());

	addProxyItem->setVisible(true);

	lineEdit->setVisible(false);
	lineEdit->deleteLater();
}

void CopyFilterGUIScene::keyPressEvent(QKeyEvent* event)
{
	// focusItem != nullptr means that the cursor is inside a line edit
	if (event->key() == Qt::Key_Delete && focusItem() == nullptr)
	{
		for (QGraphicsItem* item : selectedItems())
		{
			CopyFilterGUIConnectionItem* connectionItem = qgraphicsitem_cast<CopyFilterGUIConnectionItem*>(item);
			if (connectionItem != nullptr)
			{
				removeItem(connectionItem);
				delete connectionItem;
			}
		}

		emit updateModel();
		emit updateChannels();
	}
	else
	{
		QGraphicsScene::keyPressEvent(event);
	}
}
