/* Part of the Aitum Vertical Canvas port in Meketreve OBS Essentials.
 * Original: https://github.com/Aitum/obs-vertical-canvas (GPL-2.0), (C) Aitum.
 * Changes for the toolkit are marked "Meketreve:". */
#pragma once

#include <QDockWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QWidget>

class CanvasDock;

class CanvasTransitionsDock : public QFrame {
	Q_OBJECT
	friend class CanvasDock;

private:
	CanvasDock *canvasDock;
	QComboBox *transition;
	QSpinBox *duration;
	QPushButton *removeButton;
	QPushButton *propsButton;

public:
	CanvasTransitionsDock(CanvasDock *canvas_dock, QWidget *parent = nullptr);
	~CanvasTransitionsDock();
};
