#pragma once

#include <QString>
#include <QList>
#include <QCryptographicHash>
#include <QtXml/QDomDocument>
#include <QFile>

class UserManager
{
public:
	struct User {
		QString username;
		QString role;  // "admin" or "operator"
		QString passwordHash;
	};

private:
	UserManager();
	~UserManager();

	// 禁止拷贝
	UserManager(const UserManager&) = delete;
	UserManager& operator=(const UserManager&) = delete;

	QList<User> m_users;

public:
	// 获取单例实例
	static UserManager& instance();

	// 从config.xml加载用户配置
	bool loadUsers(const QString& configPath);

	// 保存用户配置到config.xml
	bool saveUsers(const QString& configPath);

	// 验证密码
	bool verifyPassword(const QString& username, const QString& password);

	// 修改密码
	bool changePassword(const QString& username, const QString& oldPassword,
		const QString& newPassword);

	// 管理员强制修改密码（不需要验证旧密码）
	bool forceChangePassword(const QString& username, const QString& newPassword);

	// 检查权限
	bool hasPermission(const QString& role, const QString& action) const;

	// 获取所有用户
	QList<User> getUsers() const;

	// 密码哈希工具函数（静态方法）
	static QString hashPassword(const QString& password);

	// 创建默认用户（如果config.xml中没有Users段）
	void createDefaultUsers(const QString& configPath);

	// 获取指定用户
	User getUser(const QString& username) const;
};
