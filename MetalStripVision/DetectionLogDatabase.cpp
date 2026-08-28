#pragma execution_character_set("utf-8")

#include "DetectionLogDatabase.h"
#include "Logger.h"
#include <QSqlRecord>
#include <QDebug>
#include <QThread>
#include <filesystem>

DetectionLogDatabase& DetectionLogDatabase::instance() {
	static DetectionLogDatabase instance;
	return instance;
}

DetectionLogDatabase::DetectionLogDatabase()
	: m_initialized(false)
	, m_dbPath("Log/detection.db") {
}

DetectionLogDatabase::~DetectionLogDatabase() {
	// 关闭所有线程的数据库连接
	QStringList connections = QSqlDatabase::connectionNames();
	for (const QString& name : connections) {
		QSqlDatabase::removeDatabase(name);
	}
}

bool DetectionLogDatabase::init() {
	QMutexLocker locker(&m_mutex);

	if (m_initialized) {
		return true;
	}

	// 检查SQLite驱动是否可用
	if (!QSqlDatabase::isDriverAvailable("QSQLITE")) {
		Logger::instance().error("QSQLITE driver is NOT available!");
		Logger::instance().error(QString("Available drivers: %1").arg(QSqlDatabase::drivers().join(", ")).toStdString());
		return false;
	}

	// 确保Log目录存在
	if (!ensureLogDirectory()) {
		return false;
	}

	// 在主线程创建一个连接来创建表
	QSqlDatabase mainDb = QSqlDatabase::addDatabase("QSQLITE", "main_init_connection");
	mainDb.setDatabaseName(m_dbPath);

	if (!mainDb.open()) {
		Logger::instance().error(QString("Failed to open database: %1").arg(mainDb.lastError().text()).toStdString());
		return false;
	}

	if (!createTable(mainDb)) {
		Logger::instance().error("Failed to create table");
		mainDb.close();
		return false;
	}

	mainDb.close();
	// 移除初始化连接
	QSqlDatabase::removeDatabase("main_init_connection");

	m_initialized = true;
	Logger::instance().info("DetectionLogDatabase initialized successfully");
	return true;
}

bool DetectionLogDatabase::ensureLogDirectory() {
	try {
		if (!std::filesystem::exists("Log")) {
			if (!std::filesystem::create_directory("Log")) {
				Logger::instance().error("Failed to create Log directory");
				return false;
			}
		}
	}
	catch (const std::exception& e) {
		Logger::instance().error(QString("Failed to create Log directory: %1").arg(e.what()).toStdString());
		return false;
	}
	return true;
}

QString DetectionLogDatabase::getThreadConnectionName() {
	// 使用线程ID作为连接名称，确保每个线程有独立连接
	qint64 threadId = reinterpret_cast<qint64>(QThread::currentThreadId());
	return QString("db_conn_%1").arg(threadId);
}

QSqlDatabase DetectionLogDatabase::getThreadDatabase() {
	QString connName = getThreadConnectionName();

	// 检查当前线程是否已有连接
	if (QSqlDatabase::contains(connName)) {
		QSqlDatabase db = QSqlDatabase::database(connName);
		if (db.isOpen()) {
			return db;
		}
		// 连接存在但未打开，移除重建
		QSqlDatabase::removeDatabase(connName);
	}

	// 为当前线程创建新的数据库连接
	QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
	db.setDatabaseName(m_dbPath);

	if (!db.open()) {
		Logger::instance().error(QString("Failed to open thread database: %1").arg(db.lastError().text()).toStdString());
	}

	return db;
}

bool DetectionLogDatabase::createTable(QSqlDatabase& db) {
	QSqlQuery query(db);
	bool success = query.exec(
		"CREATE TABLE IF NOT EXISTS detection_log ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT, "
		"time TEXT NOT NULL, "
		"camera TEXT NOT NULL, "
		"defect_num INTEGER DEFAULT 0, "
		"is_defect INTEGER DEFAULT 0, "
		"image_path TEXT, "
		"algorithm TEXT, "
		"process_time REAL)"
	);

	if (!success) {
		Logger::instance().error(QString("Failed to create table: %1").arg(query.lastError().text()).toStdString());
		return false;
	}

	// 创建索引加速查询
	query.exec("CREATE INDEX IF NOT EXISTS idx_time ON detection_log(time)");
	query.exec("CREATE INDEX IF NOT EXISTS idx_camera ON detection_log(camera)");
	query.exec("CREATE INDEX IF NOT EXISTS idx_is_defect ON detection_log(is_defect)");

	return true;
}

bool DetectionLogDatabase::insertRecord(int cameraId, int defectNum, bool isDefect,
	const QString& imagePath, const QString& algorithm,
	double processTime) {

	QSqlDatabase db = getThreadDatabase();
	if (!db.isOpen()) {
		Logger::instance().error("Database not open for insert");
		return false;
	}

	QSqlQuery query(db);
	query.prepare(
		"INSERT INTO detection_log "
		"(time, camera, defect_num, is_defect, image_path, algorithm, process_time) "
		"VALUES (?, ?, ?, ?, ?, ?, ?)"
	);

	query.addBindValue(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));
	query.addBindValue(QString("Camera%1").arg(cameraId));
	query.addBindValue(defectNum);
	query.addBindValue(isDefect ? 1 : 0);
	query.addBindValue(imagePath.isEmpty() || imagePath == "N/A" ? "" : imagePath);
	query.addBindValue(algorithm);
	query.addBindValue(processTime);

	if (!query.exec()) {
		Logger::instance().error(QString("Failed to insert record: %1").arg(query.lastError().text()).toStdString());
		return false;
	}

	return true;
}

bool DetectionLogDatabase::insertRecordsBatch(
	const std::vector<std::tuple<int, int, bool, QString, QString, double>>& records) {

	if (records.empty()) return true;

	QSqlDatabase db = getThreadDatabase();
	if (!db.isOpen()) {
		Logger::instance().error("Database not open for batch insert");
		return false;
	}

	// 使用事务批量插入
	db.transaction();

	QSqlQuery query(db);
	query.prepare(
		"INSERT INTO detection_log "
		"(time, camera, defect_num, is_defect, image_path, algorithm, process_time) "
		"VALUES (?, ?, ?, ?, ?, ?, ?)"
	);

	for (const auto& record : records) {
		query.addBindValue(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));
		query.addBindValue(QString("Camera%1").arg(std::get<0>(record)));
		query.addBindValue(std::get<1>(record));
		query.addBindValue(std::get<2>(record) ? 1 : 0);
		query.addBindValue(std::get<3>(record).isEmpty() || std::get<3>(record) == "N/A" ? "" : std::get<3>(record));
		query.addBindValue(std::get<4>(record));
		query.addBindValue(std::get<5>(record));

		if (!query.exec()) {
			Logger::instance().error(QString("Batch insert failed: %1").arg(query.lastError().text()).toStdString());
			db.rollback();
			return false;
		}
	}

	db.commit();
	return true;
}

void DetectionLogDatabase::beginTransaction() {
	QSqlDatabase db = getThreadDatabase();
	if (db.isOpen()) {
		db.transaction();
	}
}

void DetectionLogDatabase::commitTransaction() {
	QSqlDatabase db = getThreadDatabase();
	if (db.isOpen()) {
		db.commit();
	}
}

void DetectionLogDatabase::rollbackTransaction() {
	QSqlDatabase db = getThreadDatabase();
	if (db.isOpen()) {
		db.rollback();
	}
}

int DetectionLogDatabase::getTotalCount(const QDate& date) {
	QString dateStr = date.toString("yyyy-MM-dd");

	QSqlDatabase db = getThreadDatabase();
	if (!db.isOpen()) return 0;

	QSqlQuery query(db);
	query.prepare("SELECT COUNT(*) FROM detection_log WHERE time LIKE ?");
	query.addBindValue(dateStr + "%");
	if (query.exec() && query.next()) {
		return query.value(0).toInt();
	}
	return 0;
}

int DetectionLogDatabase::getDefectCount(const QDate& date) {
	QString dateStr = date.toString("yyyy-MM-dd");

	QSqlDatabase db = getThreadDatabase();
	if (!db.isOpen()) return 0;

	QSqlQuery query(db);
	query.prepare("SELECT COUNT(*) FROM detection_log WHERE time LIKE ? AND is_defect = 1");
	query.addBindValue(dateStr + "%");
	if (query.exec() && query.next()) {
		return query.value(0).toInt();
	}
	return 0;
}

int DetectionLogDatabase::getTotalCountWeek(const QDate& startDate) {
	int total = 0;
	for (int i = 0; i < 7; ++i) {
		total += getTotalCount(startDate.addDays(i));
	}
	return total;
}

int DetectionLogDatabase::getDefectCountWeek(const QDate& startDate) {
	int total = 0;
	for (int i = 0; i < 7; ++i) {
		total += getDefectCount(startDate.addDays(i));
	}
	return total;
}

int DetectionLogDatabase::getRecordCount(const QDate& date) {
	return getTotalCount(date);
}

int DetectionLogDatabase::getRecordCountWeek(const QDate& startDate) {
	return getTotalCountWeek(startDate);
}

std::vector<DetectionRecord> DetectionLogDatabase::queryRecords(
	const QDate& date, int limit, int offset) {

	std::vector<DetectionRecord> records;
	QString dateStr = date.toString("yyyy-MM-dd");

	QSqlDatabase db = getThreadDatabase();
	if (!db.isOpen()) return records;

	QSqlQuery query(db);
	query.prepare(
		"SELECT id, time, camera, defect_num, is_defect, image_path, algorithm, process_time "
		"FROM detection_log WHERE time LIKE ? "
		"ORDER BY id DESC LIMIT ? OFFSET ?"
	);
	query.addBindValue(dateStr + "%");
	query.addBindValue(limit);
	query.addBindValue(offset);

	if (query.exec()) {
		while (query.next()) {
			DetectionRecord record;
			record.id = query.value(0).toLongLong();
			record.time = query.value(1).toString();
			record.camera = query.value(2).toString();
			record.defectNum = query.value(3).toInt();
			record.isDefect = query.value(4).toInt() == 1;
			record.imagePath = query.value(5).toString();
			if (record.imagePath.isEmpty()) record.imagePath = "N/A";
			record.algorithm = query.value(6).toString();
			record.processTime = QString::number(query.value(7).toDouble(), 'f', 2);
			records.push_back(record);
		}
	}
	else {
		Logger::instance().error(QString("Query failed: %1").arg(query.lastError().text()).toStdString());
	}

	return records;
}

std::vector<DetectionRecord> DetectionLogDatabase::queryRecordsWeek(
	const QDate& startDate, int limit, int offset) {

	std::vector<DetectionRecord> records;
	QString startStr = startDate.toString("yyyy-MM-dd");
	QString endStr = startDate.addDays(7).toString("yyyy-MM-dd");

	QSqlDatabase db = getThreadDatabase();
	if (!db.isOpen()) return records;

	QSqlQuery query(db);
	query.prepare(
		"SELECT id, time, camera, defect_num, is_defect, image_path, algorithm, process_time "
		"FROM detection_log WHERE time >= ? AND time < ? "
		"ORDER BY id DESC LIMIT ? OFFSET ?"
	);
	query.addBindValue(startStr + " 00:00:00");
	query.addBindValue(endStr + " 00:00:00");
	query.addBindValue(limit);
	query.addBindValue(offset);

	if (query.exec()) {
		while (query.next()) {
			DetectionRecord record;
			record.id = query.value(0).toLongLong();
			record.time = query.value(1).toString();
			record.camera = query.value(2).toString();
			record.defectNum = query.value(3).toInt();
			record.isDefect = query.value(4).toInt() == 1;
			record.imagePath = query.value(5).toString();
			if (record.imagePath.isEmpty()) record.imagePath = "N/A";
			record.algorithm = query.value(6).toString();
			record.processTime = QString::number(query.value(7).toDouble(), 'f', 2);
			records.push_back(record);
		}
	}
	else {
		Logger::instance().error(QString("Query week failed: %1").arg(query.lastError().text()).toStdString());
	}

	return records;
}
