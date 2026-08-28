#include "LoginDialog.h"
#include "UserManager.h"
#include "Logger.h"
#include <QMessageBox>

LoginDialog::LoginDialog(QWidget* parent)
	: QDialog(parent)
{
	ui.setupUi(this);

	// 连接信号
	connect(ui.loginBtn, &QPushButton::clicked, this, &LoginDialog::onLoginClicked);
	connect(ui.cancelBtn, &QPushButton::clicked, this, &LoginDialog::onCancelClicked);

	// 设置回车键触发登录
	ui.passwordEdit->setFocus();

	Logger::instance().info("Login dialog opened");
}

QString LoginDialog::getLoggedInRole() const
{
	return m_loggedInRole;
}

void LoginDialog::onLoginClicked()
{
	QString password = ui.passwordEdit->text();

	if (password.isEmpty()) {
		QMessageBox::warning(this, "错误", "请输入密码！");
		ui.passwordEdit->setFocus();
		return;
	}

	// 确定角色
	QString role;
	QString username;

	if (ui.roleAdminBtn->isChecked()) {
		role = "admin";
		username = "admin";
	}
	else {
		role = "operator";
		username = "operator";
	}

	// 验证密码
	if (!UserManager::instance().verifyPassword(username, password)) {
		QMessageBox::critical(this, "登录失败", "密码错误，请重试！");
		ui.passwordEdit->clear();
		ui.passwordEdit->setFocus();
		Logger::instance().warn(QString("Login failed for user: %1 (wrong password)").arg(username).toStdString());
		return;
	}

	// 登录成功
	m_loggedInRole = role;
	Logger::instance().info(QString("User logged in successfully: %1 (role: %2)")
		.arg(username).arg(role).toStdString());

	accept();
}

void LoginDialog::onCancelClicked()
{
	Logger::instance().info("Login dialog cancelled");
	reject();
}
