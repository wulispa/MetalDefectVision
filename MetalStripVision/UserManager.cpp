#include "UserManager.h"
#include <QMessageBox>
#include <QTextStream>
#include "Logger.h"

UserManager::UserManager()
{
}

UserManager::~UserManager()
{
}

UserManager& UserManager::instance()
{
	static UserManager instance;
	return instance;
}

QString UserManager::hashPassword(const QString& password)
{
	QByteArray bytes = password.toUtf8();
	QByteArray hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
	return hash.toHex();
}

bool UserManager::loadUsers(const QString& configPath)
{
	QFile file(configPath);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		Logger::instance().error(QString("Failed to open config file: %1").arg(configPath).toStdString());
		return false;
	}

	QDomDocument doc;
	if (!doc.setContent(&file)) {
		file.close();
		Logger::instance().error("Failed to parse config.xml");
		return false;
	}
	file.close();

	// 清空现有用户列表
	m_users.clear();

	// 解析Users节点
	QDomNodeList userNodes = doc.elementsByTagName("User");
	for (int i = 0; i < userNodes.size(); ++i) {
		QDomElement userElem = userNodes.at(i).toElement();
		User user;
		user.username = userElem.attribute("username");
		user.role = userElem.attribute("role");
		user.passwordHash = userElem.firstChildElement("PasswordHash").text();
		m_users.append(user);

		Logger::instance().info(QString("Loaded user: %1 (role: %2)")
			.arg(user.username).arg(user.role).toStdString());
	}

	// 如果没有找到任何用户，创建默认用户
	if (m_users.isEmpty()) {
		Logger::instance().info("No users found in config.xml, creating default users");
		createDefaultUsers(configPath);
		return loadUsers(configPath);  // 重新加载
	}

	return true;
}

bool UserManager::saveUsers(const QString& configPath)
{
	QFile file(configPath);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		Logger::instance().error(QString("Failed to open config file for reading: %1").arg(configPath).toStdString());
		return false;
	}

	QDomDocument doc;
	if (!doc.setContent(&file)) {
		file.close();
		return false;
	}
	file.close();

	// 查找或创建Users节点
	QDomNodeList configNodes = doc.elementsByTagName("Configuration");
	if (configNodes.isEmpty()) {
		Logger::instance().error("No Configuration node in config.xml");
		return false;
	}

	QDomElement configElem = configNodes.at(0).toElement();

	// 移除现有的Users节点
	QDomNodeList oldUsersNodes = configElem.elementsByTagName("Users");
	for (int i = 0; i < oldUsersNodes.size(); ++i) {
		configElem.removeChild(oldUsersNodes.at(i));
	}

	// 创建新的Users节点
	QDomElement usersElem = doc.createElement("Users");

	// 添加每个用户
	for (const User& user : m_users) {
		QDomElement userElem = doc.createElement("User");
		userElem.setAttribute("username", user.username);
		userElem.setAttribute("role", user.role);

		QDomElement passwordHashElem = doc.createElement("PasswordHash");
		passwordHashElem.appendChild(doc.createTextNode(user.passwordHash));
		userElem.appendChild(passwordHashElem);

		usersElem.appendChild(userElem);
	}

	configElem.appendChild(usersElem);

	// 保存到文件
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
		Logger::instance().error(QString("Failed to open config file for writing: %1").arg(configPath).toStdString());
		return false;
	}

	QTextStream out(&file);
	out << doc.toString();
	file.close();

	Logger::instance().info("Users saved to config.xml");
	return true;
}

bool UserManager::verifyPassword(const QString& username, const QString& password)
{
	User user = getUser(username);
	if (user.username.isEmpty()) {
		return false;  // 用户不存在
	}

	QString inputHash = hashPassword(password);
	return (inputHash == user.passwordHash);
}

bool UserManager::changePassword(const QString& username, const QString& oldPassword,
	const QString& newPassword)
{
	// 验证旧密码
	if (!verifyPassword(username, oldPassword)) {
		return false;
	}

	// 查找用户并更新密码
	for (User& user : m_users) {
		if (user.username == username) {
			user.passwordHash = hashPassword(newPassword);
			Logger::instance().info(QString("Password changed for user: %1").arg(username).toStdString());
			return true;
		}
	}

	return false;  // 用户不存在
}

bool UserManager::forceChangePassword(const QString& username, const QString& newPassword)
{
	// 查找用户并直接更新密码（不验证旧密码）
	for (User& user : m_users) {
		if (user.username == username) {
			user.passwordHash = hashPassword(newPassword);
			Logger::instance().info(QString("Password force-changed for user: %1 (by admin)").arg(username).toStdString());
			return true;
		}
	}

	return false;  // 用户不存在
}

bool UserManager::hasPermission(const QString& role, const QString& action) const
{
	if (role == "admin") {
		// 管理员拥有所有权限
		return true;
	}
	else if (role == "operator") {
		// 操作员只能进行检测操作
		return (action == "detect");
	}

	// 未登录或其他角色
	return false;
}

QList<UserManager::User> UserManager::getUsers() const
{
	return m_users;
}

UserManager::User UserManager::getUser(const QString& username) const
{
	for (const User& user : m_users) {
		if (user.username == username) {
			return user;
		}
	}
	return User();  // 返回空用户
}

void UserManager::createDefaultUsers(const QString& configPath)
{
	m_users.clear();

	// 创建管理员账户
	User admin;
	admin.username = "admin";
	admin.role = "admin";
	admin.passwordHash = hashPassword("admin");
	m_users.append(admin);

	// 创建操作员账户
	User operatorUser;
	operatorUser.username = "operator";
	operatorUser.role = "operator";
	operatorUser.passwordHash = hashPassword("123");
	m_users.append(operatorUser);

	// 保存到配置文件
	saveUsers(configPath);

	Logger::instance().info("Default users created: admin/admin, operator/123");
}
