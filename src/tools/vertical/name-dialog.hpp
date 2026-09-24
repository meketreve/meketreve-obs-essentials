/* Part of the Aitum Vertical Canvas port in Meketreve OBS Essentials.
 * Original: https://github.com/Aitum/obs-vertical-canvas (GPL-2.0), (C) Aitum.
 * Changes for the toolkit are marked "Meketreve:". */
#pragma once

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <string>

class NameDialog : public QDialog {
	Q_OBJECT
public:
	static bool AskForName(QWidget *parent, const QString &title, std::string &name);

private:
	NameDialog(QWidget *parent, const QString &title);
	QLineEdit *userText;
};
