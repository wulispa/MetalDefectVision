#pragma once

#include <QDialog>
#include "ui_LoginDialog.h"

class LoginDialog : public QDialog
{
	Q_OBJECT

public:
	explicit LoginDialog(QWidget* parent = nullptr);
	QString getLoggedInRole() const;  // 获取登录的角色

private slots:
	void onLoginClicked();
	void onCancelClicked();

private:
	Ui::LoginDialog ui;
	QString m_loggedInRole;
};
