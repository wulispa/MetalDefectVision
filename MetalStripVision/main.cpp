#include "MetalStripVision.h"
#include "ConfigManager.h"
#include <QtWidgets/QApplication>
#include <QSettings>
#include <QFileInfo>
#include <QDir>
#include <QMetaType>
#include <opencv2/opencv.hpp>
#include <iostream>

// 设置开机自启动
void setAutoStart(bool enabled) {
	QString appName = "MetalStripVision";
	QString appPath = QFileInfo(QCoreApplication::applicationFilePath()).filePath();

	// Windows路径需要反斜杠，且用引号包裹（防止空格问题）
	appPath = appPath.replace("/", "\\");
	QString regValue = "\"" + appPath + "\"";

	QSettings settings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
					  QSettings::NativeFormat);

	if (enabled) {
		settings.setValue(appName, regValue);
		std::cout << "Auto-start enabled: " << regValue.toStdString() << std::endl;
	} else {
		settings.remove(appName);
		std::cout << "Auto-start disabled" << std::endl;
	}
}

int main(int argc, char* argv[])
{
	QApplication app(argc, argv);

	// 注册 cv::Mat 元类型，修复跨线程信号传递时的断言崩溃
	qRegisterMetaType<cv::Mat>("cv::Mat");

	// 切换工作目录到程序所在目录（解决自启动时找不到配置文件的问题）
	QString appPath = QCoreApplication::applicationFilePath();
	QString appDir = QFileInfo(appPath).absolutePath();
	QDir::setCurrent(appDir);

	// 调试：写入日志文件
	QString debugLog = appDir + "/startup_debug.log";
	QFile debugFile(debugLog);
	if (debugFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
		QTextStream out(&debugFile);
		out << "App Path: " << appPath << "\n";
		out << "App Dir: " << appDir << "\n";
		out << "Current Dir: " << QDir::currentPath() << "\n";
		out << "Config exists: " << (QFile::exists("config/config.xml") ? "YES" : "NO") << "\n";
		debugFile.close();
	}


	// 检查并设置开机自启动
	AutoStartConfig autoStartConfig = ConfigManager::instance().getAutoStartConfig();
	setAutoStart(autoStartConfig.enabled);

	MetalStripVision window;
	window.resize(1600, 900);  // 设置窗口大小
	window.show();              // 窗口化显示
	return app.exec();
}