#pragma once

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QMutex>
#include <QString>
#include <QDateTime>
#include <QDate>
#include <vector>

// 检测记录结构体
struct DetectionRecord {
	qint64 id;
	QString time;
	QString camera;
	int defectNum;
	bool isDefect;
	QString imagePath;
	QString algorithm;
	QString processTime;
};

// 检测日志数据库管理类（单例模式，支持多线程）
class DetectionLogDatabase {
public:
	static DetectionLogDatabase& instance();

	// 初始化数据库（主线程调用）
	bool init();

	// 插入检测记录（线程安全，每个线程使用独立连接）
	bool insertRecord(int cameraId, int defectNum, bool isDefect,
		const QString& imagePath, const QString& algorithm,
		double processTime);

	// 批量插入检测记录（使用事务，更高效）
	bool insertRecordsBatch(const std::vector<std::tuple<int, int, bool, QString, QString, double>>& records);

	// 数据库事务支持
	void beginTransaction();
	void commitTransaction();
	void rollbackTransaction();

	// 查询记录（分页）
	std::vector<DetectionRecord> queryRecords(const QDate& date, int limit, int offset);
	std::vector<DetectionRecord> queryRecordsWeek(const QDate& startDate, int limit, int offset);

	// 获取统计数据
	int getTotalCount(const QDate& date);
	int getDefectCount(const QDate& date);
	int getTotalCountWeek(const QDate& startDate);
	int getDefectCountWeek(const QDate& startDate);
	int getRecordCount(const QDate& date);
	int getRecordCountWeek(const QDate& startDate);

private:
	DetectionLogDatabase();
	~DetectionLogDatabase();
	DetectionLogDatabase(const DetectionLogDatabase&) = delete;
	DetectionLogDatabase& operator=(const DetectionLogDatabase&) = delete;

	// 获取当前线程的数据库连接
	QSqlDatabase getThreadDatabase();
	QString getThreadConnectionName();
	bool createTable(QSqlDatabase& db);
	bool ensureLogDirectory();

	QMutex m_mutex;
	bool m_initialized;
	QString m_dbPath;
};
