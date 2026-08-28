#pragma once

#include <QDialog>
#include "ui_ChangePasswordDialog.h"

class ChangePasswordDialog : public QDialog
{
	Q_OBJECT

public:
	explicit ChangePasswordDialog(const QString& currentRole, const QString& currentUser, QWidget* parent = nullptr);

private slots:
	void onConfirmClicked();
	void onCancelClicked();

private:
	Ui::ChangePasswordDialog ui;
	QString m_currentRole;  // 当前登录用户的角色
	QString m_currentUser;  // 当前登录用户名
	QString m_targetUser;   // 要修改密码的目标用户
};
