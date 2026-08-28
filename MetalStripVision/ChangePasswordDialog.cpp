#include "ChangePasswordDialog.h"
#include "UserManager.h"
#include "Logger.h"
#include <QMessageBox>
#include <QComboBox>

ChangePasswordDialog::ChangePasswordDialog(const QString& currentRole, const QString& currentUser, QWidget* parent)
	: QDialog(parent), m_currentRole(currentRole), m_currentUser(currentUser)
{
	ui.setupUi(this);

	// 显示当前登录用户
	ui.currentUserEdit->setText(currentUser + " (" + (currentRole == "admin" ? "管理员" : "操作员") + ")");

	// 如果是管理员，显示用户选择下拉框
	if (currentRole == "admin") {
		// 创建用户选择下拉框
		QComboBox* userCombo = new QComboBox(this);
		userCombo->setObjectName("userCombo");
		userCombo->addItem("admin", "admin");
		userCombo->addItem("operator", "operator");

		// 设置下拉框样式
		userCombo->setStyleSheet(
			"QComboBox#userCombo {"
			"  background-color: #313244;"
			"  color: #cdd6f4;"
			"  border: 2px solid #45475a;"
			"  border-radius: 6px;"
			"  padding: 8px;"
			"  font-size: 14px;"
			"  font-weight: bold;"
			"}"
			"QComboBox#userCombo:hover {"
			"  border: 2px solid #89b4fa;"
			"}"
			"QComboBox#userCombo::drop-down {"
			"  background-color: #313244;"
			"  border: 1px solid #45475a;"
			"}"
			"QComboBox#userCombo QAbstractItemView {"
			"  background-color: #1e1e2e;"
			"  color: #cdd6f4;"
			"  selection-background-color: #455a64;"
			"  font-size: 14px;"
			"  padding: 6px;"
			"}"
		);

		// 在布局中插入用户选择下拉框
		QVBoxLayout* layout = ui.verticalLayout_userSelect;
		layout->insertWidget(1, userCombo);

		// 默认选择当前用户
		if (currentUser == "admin") {
			userCombo->setCurrentIndex(0);
		} else {
			userCombo->setCurrentIndex(1);
		}

		// 连接信号
		connect(userCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, userCombo](int index) {
			m_targetUser = userCombo->currentData().toString();
		});

		// 初始化目标用户
		m_targetUser = userCombo->currentData().toString();
	} else {
		// 操作员只能修改自己的密码
		m_targetUser = currentUser;
	}

	// 连接信号
	connect(ui.confirmBtn, &QPushButton::clicked, this, &ChangePasswordDialog::onConfirmClicked);
	connect(ui.cancelBtn, &QPushButton::clicked, this, &ChangePasswordDialog::onCancelClicked);

	// 设置焦点
	ui.oldPasswordEdit->setFocus();

	QString targetUserInfo = (currentRole == "admin") ?
		QString("Change password dialog opened (admin can modify any user)") :
		QString("Change password dialog opened for user: %1").arg(currentUser);
	Logger::instance().info(targetUserInfo.toStdString());
}

void ChangePasswordDialog::onConfirmClicked()
{
	QString oldPassword = ui.oldPasswordEdit->text();
	QString newPassword = ui.newPasswordEdit->text();
	QString confirmPassword = ui.confirmPasswordEdit->text();

	// 验证输入
	if (oldPassword.isEmpty()) {
		QMessageBox::warning(this, "错误", "请输入当前密码！");
		ui.oldPasswordEdit->setFocus();
		return;
	}

	if (newPassword.isEmpty()) {
		QMessageBox::warning(this, "错误", "请输入新密码！");
		ui.newPasswordEdit->setFocus();
		return;
	}

	if (newPassword.length() < 3) {
		QMessageBox::warning(this, "错误", "新密码长度不能少于3个字符！");
		ui.newPasswordEdit->setFocus();
		return;
	}

	if (confirmPassword.isEmpty()) {
		QMessageBox::warning(this, "错误", "请确认新密码！");
		ui.confirmPasswordEdit->setFocus();
		return;
	}

	if (newPassword != confirmPassword) {
		QMessageBox::warning(this, "错误", "两次输入的新密码不一致！");
		ui.confirmPasswordEdit->clear();
		ui.confirmPasswordEdit->setFocus();
		return;
	}

	// 管理员修改其他用户密码时，不需要验证旧密码
	bool needOldPassword = (m_currentRole != "admin" || m_targetUser == m_currentUser);

	if (needOldPassword) {
		// 验证旧密码
		if (!UserManager::instance().verifyPassword(m_targetUser, oldPassword)) {
			QMessageBox::critical(this, "错误", "当前密码不正确！");
			ui.oldPasswordEdit->clear();
			ui.oldPasswordEdit->setFocus();
			Logger::instance().warn(QString("Password change failed for user: %1 (wrong old password)")
				.arg(m_targetUser).toStdString());
			return;
		}
	}

	// 修改密码（管理员修改其他用户密码时，直接修改）
	if (m_currentRole == "admin" && m_targetUser != m_currentUser) {
		// 管理员直接修改其他用户密码，不需要验证旧密码
		auto users = UserManager::instance().getUsers();
		for (const auto& user : users) {
			if (user.username == m_targetUser) {
				// 临时验证一下管理员自己的密码
				if (!UserManager::instance().verifyPassword(m_currentUser, oldPassword)) {
					QMessageBox::critical(this, "错误", "管理员密码验证失败！");
					ui.oldPasswordEdit->clear();
					ui.oldPasswordEdit->setFocus();
					return;
				}

				// 修改目标用户密码（使用强制修改，不需要验证旧密码）
				if (!UserManager::instance().forceChangePassword(m_targetUser, newPassword)) {
					QMessageBox::critical(this, "错误", "密码修改失败！");
					return;
				}
				break;
			}
		}
	} else {
		// 修改自己的密码
		if (!UserManager::instance().changePassword(m_targetUser, oldPassword, newPassword)) {
			QMessageBox::critical(this, "错误", "密码修改失败！");
			ui.oldPasswordEdit->clear();
			ui.oldPasswordEdit->setFocus();
			Logger::instance().warn(QString("Password change failed for user: %1")
				.arg(m_targetUser).toStdString());
			return;
		}
	}

	// 保存到配置文件
	QString configPath = "config/config.xml";
	if (!UserManager::instance().saveUsers(configPath)) {
		QMessageBox::warning(this, "警告", "密码已修改，但保存到配置文件失败！");
		Logger::instance().warn("Failed to save users to config/config.xml after password change");
	}

	QString message = (m_currentRole == "admin" && m_targetUser != m_currentUser) ?
		QString("用户 %1 的密码修改成功！").arg(m_targetUser) :
		QString("密码修改成功！");

	QMessageBox::information(this, "成功", message);
	Logger::instance().info(QString("Password changed successfully for user: %1").arg(m_targetUser).toStdString());

	accept();
}

void ChangePasswordDialog::onCancelClicked()
{
	Logger::instance().info("Change password dialog cancelled");
	reject();
}
