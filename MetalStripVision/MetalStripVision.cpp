
#pragma execution_character_set("utf-8")

#include "MetalStripVision.h"
#include <opencv2/opencv.hpp>
#include <windows.h>
#include <QMessageBox>
#include <QApplication>
#include <QInputDialog>
#include <sstream>
#include <iomanip>
#include "Logger.h"
#include "DataStatsDialog.h"
#include "LoginDialog.h"
#include "UserManager.h"
#include "ChangePasswordDialog.h"
#include "AlgorithmROIManager.h"
#include "ParameterSettingsDialog.h"
#include "ConfigManagerDialog.h"
#include "EnlargedViewDialog.h"
#include "ManualCaptureDialog.h"
#include "AIParamConfigDialog.h"

MetalStripVision::MetalStripVision(QWidget* parent)
    : QWidget(parent)
{
    ui.setupUi(this);

    // 加载配置文件（必须先加载配置才能获取系统日志设置）
    loadConfiguration();

    // 初始化日志系统
    Logger::instance().init();

    // 根据配置设置系统日志开关
    auto sysLogConfig = ConfigManager::instance().getSystemLogConfig();
    Logger::instance().setEnabled(sysLogConfig.enabled);
    Logger::instance().setDebugEnabled(sysLogConfig.debugEnabled);

    // 初始化检测日志数据库
    if (!DetectionLogDatabase::instance().init()) {
        Logger::instance().error("Failed to initialize detection log database");
    }

    // 初始化PLC通信
    auto plcConfig = ConfigManager::instance().getPLCConfig();
    if (plcConfig.enabled) {
        m_plc = std::make_unique<PLCInterface>(plcConfig.ip, plcConfig.port, plcConfig.slaveId);
        if (m_plc->start()) {
            Logger::instance().info("[PLC] Interface started - " + plcConfig.ip + ":" + std::to_string(plcConfig.port));
        } else {
            Logger::instance().error("[PLC] Failed to start interface");
        }
    } else {
        Logger::instance().info("[PLC] Communication disabled in config");
    }

    if (sysLogConfig.enabled) {
        Logger::instance().info("Program Start");
    }

    // 确保图片保存目录存在
    auto imgConfig = ConfigManager::instance().getImageSaveConfig();
    if (!std::filesystem::exists(imgConfig.savePath)) {
        std::error_code ec;
        std::filesystem::create_directories(imgConfig.savePath, ec);
        if (ec) {
            if (sysLogConfig.enabled) {
                Logger::instance().warn("Failed to create save directory: " + imgConfig.savePath + ", error: " + ec.message());
            }
        } else {
            if (sysLogConfig.enabled) {
                Logger::instance().info("Created save directory: " + imgConfig.savePath);
            }
        }
    }

    // 检查是否启用测试模式
    auto testConfig = ConfigManager::instance().getTestModeConfig();
    m_testMode = testConfig.enabled;
    if (m_testMode) {
        m_testImagePath = testConfig.imageBasePath;
        loadTestImages();
        Logger::instance().info("Test mode enabled. Image path: " + m_testImagePath);
    }

    // ========== 重要：先初始化算法，再初始化相机（因为Camera_Init需要使用算法实例）==========
    // 初始化传统算法
    m_traditionalAlgo = std::make_shared<HalconAlgorithm>();
    m_traditionalAlgo->initialize(nullptr);
    // m_traditionalAlgo2 = std::make_shared<HalconAlgorithm>();  // 相机2传统算法（已禁用）
    // m_traditionalAlgo2->initialize(nullptr);

    // 初始化AI算法（上表面和下表面）
    m_aiAlgoUpper = std::make_shared<YOLOAlgorithm>();
    m_aiAlgoUpper->initialize(nullptr);
    m_aiAlgoLower = std::make_shared<YOLOAlgorithm>();
    m_aiAlgoLower->initialize(nullptr);

    // ========== 重要：先设置源图片尺寸，再创建模型 ==========
    // 设置AI算法源图片尺寸（相机图片尺寸）
    // TODO: 从配置文件或相机属性获取实际尺寸
    int srcWidth = 4096;   // 默认值，请根据实际相机尺寸修改
    int srcHeight = 3000;  // 默认值，请根据实际相机尺寸修改
    m_aiAlgoUpper->setSrcImageSize(srcWidth, srcHeight);
    m_aiAlgoLower->setSrcImageSize(srcWidth, srcHeight);

    // 加载YOLO模型
    QString modelPath = QApplication::applicationDirPath() + "/Model";
    m_aiAlgoUpper->loadModel(modelPath.toStdString());
    m_aiAlgoLower->loadModel(modelPath.toStdString());

    // 加载颜色配置
    QString colorConfigPath = QApplication::applicationDirPath() + "/config/defect_colors.xml";
    m_aiAlgoUpper->loadColorConfig(colorConfigPath);
    m_aiAlgoLower->loadColorConfig(colorConfigPath);

    // 先加载算法ROI配置以获取默认料号
    AlgorithmROIManager::instance().loadConfig();
    std::string initPartNumber = AlgorithmROIManager::instance().getDefaultProfile(1);
    if (initPartNumber.empty()) {
        initPartNumber = "默认料号";
    }
    m_currentPartNumber = initPartNumber;

    // 设置AI算法的料号（用于构建模型路径）
    QString qPartNumber = QString::fromStdString(initPartNumber);
    m_aiAlgoUpper->setPartNumber(qPartNumber);
    m_aiAlgoLower->setPartNumber(qPartNumber);
    Logger::instance().info("初始化料号: " + initPartNumber);

    // 加载上表面模型（Up_metal）
    if (m_aiAlgoUpper->loadModelByName("Up_metal")) {
        Logger::instance().info("上表面AI模型加载成功 (料号: " + initPartNumber + ")");
    } else {
        Logger::instance().warn("上表面AI模型加载失败，请检查 Model/" + initPartNumber + "/Trt/Up_metal.trt 和 Model/" + initPartNumber + "/Tag/Up_metal.txt");
    }

    // 加载下表面模型（Down_metal）
    if (m_aiAlgoLower->loadModelByName("Down_metal")) {
        Logger::instance().info("下表面AI模型加载成功 (料号: " + initPartNumber + ")");
    } else {
        Logger::instance().warn("下表面AI模型加载失败，请检查 Model/" + initPartNumber + "/Trt/Down_metal.trt 和 Model/" + initPartNumber + "/Tag/Down_metal.txt");
    }

    // 初始化相机（必须在算法初始化之后，因为Camera_Init内部会创建AlgorithmThread并传入算法实例）
    Logger::instance().info("Camera Initialization Start");
    Camera_Init();
    Logger::instance().info("Camera Initialization End");

    frameId = 0;

    // 主定时器（图像显示）
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MetalStripVision::processFrame);

    // ===== NG记录框字体设置（可以调整这里） =====
    // 使用样式表设置字体（24px 是字体大小，可以根据需要调整）
    ui.NG_textedit->setStyleSheet("QTextEdit { font-family: 'Microsoft YaHei UI'; font-size: 24px; font-weight: bold; color: rgb(205, 92, 92); }");
    // ===== NG记录框字体设置结束 =====

    // 系统时间更新定时器
    m_timeTimer = new QTimer(this);
    connect(m_timeTimer, &QTimer::timeout, this, &MetalStripVision::updateSystemTime);
    m_timeTimer->start(1000);  // 每秒更新

    // 连接信号
    connect(ui.DetectBtn, &QPushButton::clicked, this, &MetalStripVision::Detect_Btn_click);
    connect(ui.ExitBtn, &QPushButton::clicked, this, &MetalStripVision::exit_Btn_click);
    connect(ui.DataBtn, &QPushButton::clicked, this, &MetalStripVision::onDataStatsClicked);
    connect(ui.UnlockBtn, &QPushButton::clicked, this, &MetalStripVision::onUnlockBtnClicked);
    connect(ui.PressBtn, &QPushButton::clicked, this, &MetalStripVision::onChangePasswordClicked);
    connect(ui.ParamBtn, &QPushButton::clicked, this, &MetalStripVision::onParameterSettingsClicked);  // 参数设置按钮
    connect(ui.AIParamBtn, &QPushButton::clicked, this, &MetalStripVision::onAIParamSettingsClicked);  // AI参数设置按钮
    connect(ui.ConfigManagerBtn, &QPushButton::clicked, this, &MetalStripVision::onConfigManagementClicked);  // 配置管理按钮
    connect(ui.ManualCaptureBtn, &QPushButton::clicked, this, &MetalStripVision::onManualCaptureClicked);  // 手动取图按钮

    // 连接跨线程更新NG记录的信号槽
    connect(this, &MetalStripVision::newNGRecord, this, &MetalStripVision::updateNGRecordDisplay);

    // 初始化为未登录状态
    m_isLoggedIn = false;
    m_currentRole.clear();

    // 根据登录状态更新UI
    updateUIByPermission();

    // 初始化算法ROI配置（加载默认用户集）
    initializeAlgorithmROI();

    // 加载AI参数配置（阈值）
    loadAIParamConfig();

    // 安装事件过滤器 - 用于捕获相机窗口的点击事件
    ui.Camera1->installEventFilter(this);
    ui.Camera2->installEventFilter(this);
    ui.Camera3->installEventFilter(this);

    // 自动启动检测（初始化完成后延迟500ms自动点击检测按钮）
    QTimer::singleShot(500, this, [this]() {
        Logger::instance().info("Auto-starting detection after initialization");
        Detect_Btn_click();
    });
}

MetalStripVision::~MetalStripVision()
{
    Logger::instance().info("Destructor called, cleaning up resources");

    // 停止PLC通信
    if (m_plc) {
        m_plc->stop();
        Logger::instance().info("[PLC] Interface stopped");
    }

    // 设置停止标志
    m_running = false;
    m_stop = true;

    // 停止新架构
    if (m_useNewArchitecture) {
        stopNewArchitecture();
    }

    Logger::instance().info("All threads stopped in destructor");
}

void MetalStripVision::Camera_Init()
{
	// 从配置文件加载相机配置
	auto configs = ConfigManager::instance().getCameraConfigs();

	// 用于记录哪些相机成功初始化
	bool camera1Ready = false;
	bool camera2Ready = false;
	bool camera3Ready = false;

	// 重置初始化状态 
	m_camera1Initialized = false;
	m_camera2Initialized = false;
	m_camera3Initialized = false;

	for (const auto& config : configs) {
		if (!config.enabled) continue;

		CameraHK1* camera = nullptr;
		if (config.id == 1) {
			camera = &m_camera1;
		} else if (config.id == 2) {
			camera = &m_camera2;
		} else if (config.id == 3) {
			camera = &m_camera3;
		}

		if (camera && !camera->OpenCamera(config.serialNumber)) {
			// 相机打开失败，只记录警告，不中断初始化流程
			Logger::instance().warn(QString("Camera %1 open failed (Serial: %2), will skip")
				.arg(config.id)
				.arg(QString::fromStdString(config.serialNumber)).toStdString());

			// 继续初始化其他相机
			continue;
		}

		if (camera) {
			camera->SetTriggerModel(config.triggerMode);
			// 注意：OpenCamera() 内部已调用 MV_CC_StartGrabbing()，无需再次启动
			// GraphImage() 会阻塞，不能在UI线程调用，由相机线程中的 GetImage() 处理

			Logger::instance().info(QString("Camera %1 initialized successfully (Serial: %2, Algorithm: %3)")
				.arg(config.id)
				.arg(QString::fromStdString(config.serialNumber))
				.arg(QString::fromStdString(config.getAlgorithmTypeDisplay())).toStdString());

			// 标记相机初始化成功
			if (config.id == 1) {
				camera1Ready = true;
				m_camera1Initialized = true;
			}
			else if (config.id == 2) {
				camera2Ready = true;
				m_camera2Initialized = true;
			}
			else if (config.id == 3) {
				camera3Ready = true;
				m_camera3Initialized = true;
			}
		}
	}

	TriggerModel = 1;

	// ========== 使用新架构替代旧线程 ==========
	// 初始化并启动新架构
	if (camera1Ready || camera2Ready || camera3Ready || m_testMode) {
		initializeNewArchitecture();
		startNewArchitecture();
		m_useNewArchitecture = true;
		Logger::instance().info("New architecture initialized and started");
	} else {
		Logger::instance().error("No camera initialized successfully, system cannot run");
		QMessageBox::critical(this, "严重错误",
			"所有相机初始化失败，系统无法运行！\n请检查：\n"
			"1. 相机是否正确连接\n"
			"2. 序列号是否正确\n"
			"3. 驱动程序是否安装");
		return;
	}

	Logger::instance().info("Camera initialization completed");
}



// ===== 加载配置文件 =====
void MetalStripVision::loadConfiguration()
{
    QString configPath = "config/config.xml";

    if (!ConfigManager::instance().loadConfig(configPath.toStdString()))
    {
        QMessageBox::warning(this, "配置错误",
            "无法加载配置文件 config/config.xml，将使用默认配置。");

        Logger::instance().error("Failed to load config/config.xml");
    }
    else
    {
        Logger::instance().info("Configuration loaded successfully");

        auto configs = ConfigManager::instance().getCameraConfigs();

        for (const auto& config : configs)
        {
            Logger::instance().info(
                QString("Camera %1: SN=%2, Type=%3, Enabled=%4")
                .arg(config.id)
                .arg(QString::fromStdString(config.serialNumber))
                .arg(QString::fromStdString(config.getAlgorithmTypeDisplay()))
                .arg(config.enabled ? "true" : "false")
                .toStdString()
            );
        }
    }

    // 加载用户配置
    if (!UserManager::instance().loadUsers(configPath))
    {
        Logger::instance().warn("Failed to load users from config.xml, will create default users");
        // 创建默认用户
        UserManager::instance().createDefaultUsers(configPath);
    }
    else
    {
        Logger::instance().info("User configuration loaded successfully");
    }
}

// ===== 更新系统时间 =====
void MetalStripVision::updateSystemTime()
{
	QDateTime current = QDateTime::currentDateTime();
	ui.System_Label_2->setText(current.toString("yyyy-MM-dd hh:mm:ss"));

	// 计算运行时间
	static QDateTime startTime = QDateTime::currentDateTime();
	qint64 seconds = startTime.secsTo(QDateTime::currentDateTime());

	int hours = seconds / 3600;
	int minutes = (seconds % 3600) / 60;
	int secs = seconds % 60;

	ui.Runtime_Label->setText(QString("%1:%2:%3")
		.arg(hours)
		.arg(minutes, 2, 10, QChar('0'))
		.arg(secs, 2, 10, QChar('0')));
}

// ===== 数据统计按钮点击 =====
void MetalStripVision::onDataStatsClicked()
{
	DataStatsDialog* dialog = new DataStatsDialog(this);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->show();
}

// ===== 获取当前相机图像（用于参数设置对话框的实时预览）=====
cv::Mat MetalStripVision::getCurrentCameraImage(int cameraId) const
{
	cv::Mat image;
	std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_cvMtx));

	switch (cameraId) {
	case 1:
		image = m_camera1Image.clone();
		break;
	case 2:
		image = m_camera2Image.clone();
		break;
	case 3:
		image = m_camera3Image.clone();
		break;
	default:
		break;
	}

	return image;
}

void MetalStripVision::updateNGRecordDisplay(const QString& record)
{
	// 在NG_textedit中追加记录（append会自动滚动到底部）
	ui.NG_textedit->append(record);

	// 限制显示的记录数量（最多显示最近100条）
	QString text = ui.NG_textedit->toPlainText();
	QStringList lines = text.split('\n');
	if (lines.size() > 100) {
		// 保留最近100条
		lines = lines.mid(lines.size() - 100);
		ui.NG_textedit->setPlainText(lines.join('\n'));
	}
}
void MetalStripVision::Detect_Btn_click()
{
	if (!m_running) {
		// 开始运行
		m_running = true;
		timer->start(50);  // 固定50ms UI刷新间隔

		// 恢复所有采图线程
		if (m_captureThreadNew1) m_captureThreadNew1->resume();
		if (m_captureThreadNew2) m_captureThreadNew2->resume();
		if (m_captureThreadNew3) m_captureThreadNew3->resume();

		Logger::instance().info("Detection started");


			// 检测启动时一次性加载ROI参数到传统算法（不再每帧读取）
			{
				std::string defaultProfile = AlgorithmROIManager::instance().getDefaultProfile(1);
				if (!defaultProfile.empty()) {
					UserProfile profile = AlgorithmROIManager::instance().getUserProfile(1, defaultProfile);
					auto halconAlgo = std::dynamic_pointer_cast<HalconAlgorithm>(m_traditionalAlgo);
					if (halconAlgo && profile.roi.enabled) {
						halconAlgo->updateROI(profile.roi.row1, profile.roi.column1,
							profile.roi.row2, profile.roi.column2);
						halconAlgo->updateThreshold(profile.thresholdMin, profile.thresholdMax);
						Logger::instance().info("ROI loaded: " + defaultProfile +
							" row1=" + std::to_string((int)profile.roi.row1) + " row2=" + std::to_string((int)profile.roi.row2));
					}
				}
			}
		ui.DetectBtn->setText("暂停检测");
		ui.DetectBtn->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
		ui.ManualCaptureBtn->setEnabled(false);

	}
	else {
		// 暂停运行
		m_running = false;
		timer->stop();

		// 暂停所有采图线程
		if (m_captureThreadNew1) m_captureThreadNew1->pause();
		if (m_captureThreadNew2) m_captureThreadNew2->pause();
		if (m_captureThreadNew3) m_captureThreadNew3->pause();

		Logger::instance().info("Detection paused");
		ui.DetectBtn->setText("开始检测");
		ui.DetectBtn->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
		ui.ManualCaptureBtn->setEnabled(m_isLoggedIn);
}
}



void MetalStripVision::exit_Btn_click()
{
    Logger::instance().info("Program Exit initiated");

    // 标记为正常退出，防止closeEvent拦截
    m_isNormalExit = true;

    m_running = false;
    m_stop = true;

    // 停止定时器
    if (timer) timer->stop();
    if (m_timeTimer) m_timeTimer->stop();

    // 停止新架构
    if (m_useNewArchitecture) {
        stopNewArchitecture();
    }

    // 关闭相机
    m_camera1.StopGraphImage();
    m_camera1.CloseCamera();

    m_camera2.StopGraphImage();
    m_camera2.CloseCamera();

    m_camera3.StopGraphImage();
    m_camera3.CloseCamera();

    Logger::instance().info("All threads stopped");

    close();
}

void MetalStripVision::processFrame()
{
	// 新架构中，UI更新由信号槽机制处理（onImageUpdated, onDefectStatusUpdated）
	// 此函数保留用于定时器触发，但不执行任何操作
	// 如果需要兼容旧架构，可以在此添加条件判断

	// 快速检查：如果正在退出，立即返回
	if (!m_running || m_stop) {
		return;
	}

	// 新架构：所有UI更新已通过信号槽处理，此处无需操作
	// 如果未来需要添加定时触发的逻辑，可以在此处添加
}



QImage Mat2QImage(const cv::Mat& mat)
{
    if (mat.empty())
        return QImage();

    // 注意：返回的QImage与mat共享数据，mat必须在QImage使用期间保持有效
    // 由于QPixmap::fromImage会复制数据，因此在onImageUpdated中使用是安全的
    switch (mat.type()) {
    case CV_8UC3:
        return QImage(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_BGR888);
    case CV_8UC1:
        return QImage(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_Grayscale8);
    case CV_8UC4:
        return QImage(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_ARGB32);
    default:
        cv::Mat mat_bgr;
        cv::cvtColor(mat, mat_bgr, cv::COLOR_RGB2BGR);
        return QImage(mat_bgr.data, mat_bgr.cols, mat_bgr.rows, mat_bgr.step, QImage::Format_BGR888);
    }
}



cv::Mat MetalStripVision::draw_pic(cv::Mat img, DetectionState state)
{
    if (img.empty())
        return img;

    cv::Mat img_display;

    // 性能优化：只有NG/FAIL状态才clone，OK状态直接使用原图
    switch (state) {
        case DetectionState::OK:
            // OK状态直接使用原图（不clone）
            img_display = img;
            if (img_display.channels() == 1)
                cv::cvtColor(img_display, img_display, cv::COLOR_GRAY2BGR);
            break;

        case DetectionState::NG:
        case DetectionState::FAIL:
            // NG/FAIL状态需要clone（用于保存）
            img_display = img.clone();
            if (img_display.channels() == 1)
                cv::cvtColor(img_display, img_display, cv::COLOR_GRAY2BGR);
            break;
    }

    // 根据状态设置文本和颜色
    std::string text;
    cv::Scalar textColor;
    switch (state) {
        case DetectionState::OK:
            text = "OK";
            textColor = cv::Scalar(0, 255, 0);  // 绿色
            break;
        case DetectionState::NG:
            text = "NG";
            textColor = cv::Scalar(0, 0, 255);  // 红色
            break;
        case DetectionState::FAIL:
            text = "FAIL";
            textColor = cv::Scalar(0, 255, 255);  // 黄色（BGR格式）
            break;
    }

    // ===== 字体设置（适配缩放后的显示尺寸） =====
    double fontScale = std::max(0.8, img_display.cols / 800.0);
    int thickness = std::max(2, int(img_display.cols / 600.0));
    // ===== 字体设置结束 =====

    int baseline = 0;
    cv::Size textSize = cv::getTextSize(
        text,
        cv::FONT_HERSHEY_SIMPLEX,
        fontScale,
        thickness,
        &baseline
    );

    // 左上角位置
    cv::Point textOrg(20, textSize.height + 30);

    cv::putText(
        img_display,
        text,
        textOrg,
        cv::FONT_HERSHEY_SIMPLEX,
        fontScale,
        textColor,
        thickness,
        cv::LINE_AA
    );

    return img_display;
}

// ===== 权限控制方法 =====

void MetalStripVision::onUnlockBtnClicked()
{
	if (m_isLoggedIn) {
		// 已登录，执行锁定/退出登录
		logout();
		return;
	}

	// 未登录，显示登录对话框
	LoginDialog dialog(this);
	if (dialog.exec() == QDialog::Accepted) {
		m_currentRole = dialog.getLoggedInRole();
		m_isLoggedIn = true;
		updateUIByPermission();

		QString roleText = (m_currentRole == "admin") ? "管理员" : "操作员";
		Logger::instance().info(QString("User logged in as: %1").arg(roleText).toStdString());
	}
}

void MetalStripVision::logout()
{
	m_isLoggedIn = false;
	m_currentRole.clear();
	updateUIByPermission();

	Logger::instance().info("User logged out");

	// 注意：锁定时不停止检测，只禁用操作按钮
	// 检测程序继续运行，保证生产不受影响
}

void MetalStripVision::updateUIByPermission()
{
	if (!m_isLoggedIn) {
		// 未登录：禁用所有功能按钮

		ui.DetectBtn->setEnabled(false);
		ui.DataBtn->setEnabled(false);
		ui.PressBtn->setEnabled(false);  // 参数设置/修改密码
		ui.ParamBtn->setEnabled(false);  // 算法参数设置
		ui.AIParamBtn->setEnabled(false);  // AI参数设置
		ui.ConfigManagerBtn->setEnabled(false);  // 配置管理
		ui.ManualCaptureBtn->setEnabled(false);  // 手动取图
		ui.ExitBtn->setEnabled(false);   // 退出按钮也需要登录才能使用
		ui.UnlockBtn->setText("🔓 解锁");
		return;
	}

	// 已登录：根据角色启用按钮
	ui.DetectBtn->setEnabled(true);  // 所有角色都可以检测
	ui.ExitBtn->setEnabled(true);    //
	ui.ManualCaptureBtn->setEnabled(!m_running);  // 所有角色都可以退出程序

	// 管理员专属功能
	bool isAdmin = (m_currentRole == "admin");
	ui.DataBtn->setEnabled(true);         // 数据查询 - 所有角色可用
	ui.PressBtn->setEnabled(isAdmin);     // 修改密码 - 操作员禁用
	ui.AIParamBtn->setEnabled(isAdmin);   // AI参数设置 - 操作员禁用
	ui.ConfigManagerBtn->setEnabled(isAdmin);  // 配置管理 - 操作员禁用

	// 参数设置 - 操作员可以打开但有内部限制（只能切换料号和设为默认）
	ui.ParamBtn->setEnabled(true);        // 所有角色都可以打开

	// 更新解锁按钮文本
	ui.UnlockBtn->setText("🔒 锁定");
}

bool MetalStripVision::hasPermission(const QString& action) const
{
	return UserManager::instance().hasPermission(m_currentRole, action);
}

void MetalStripVision::onChangePasswordClicked()
{
	if (!m_isLoggedIn) {
		return;
	}

	// 确定当前用户
	QString currentUser;
	if (m_currentRole == "admin") {
		currentUser = "admin";
	}
	else {
		currentUser = "operator";
	}

	// 打开修改密码对话框，传递当前角色和当前用户
	ChangePasswordDialog dialog(m_currentRole, currentUser, this);
	dialog.exec();
}

// ===== 算法ROI参数设置 =====
void MetalStripVision::onParameterSettingsClicked()
{
	if (!m_isLoggedIn) {
		return;
	}

	// 所有角色都可以打开参数设置对话框
	// 但对话框内部会根据角色限制功能（操作员只能切换料号和设为默认）
	ParameterSettingsDialog dialog(m_currentRole, this);
	dialog.exec();

	// 对话框关闭后，重新加载算法配置
	reloadAlgorithmConfig();
}

// ===== AI参数设置 =====
void MetalStripVision::onAIParamSettingsClicked()
{
	if (!m_isLoggedIn) {
		return;
	}

	if (m_currentRole != "admin") {
		QMessageBox::warning(this, "权限不足", "只有管理员可以修改AI参数设置");
		return;
	}

	AIParamConfigDialog dialog(m_aiAlgoUpper.get(), m_aiAlgoLower.get(),
	                           m_bboxSizeConfig.pixelsPerMm, this);
	if (dialog.exec() == QDialog::Accepted) {
		// 从对话框获取最新阈值
		m_aiThresholds[2] = dialog.getThresholdsForCamera(2);
		m_aiThresholds[3] = dialog.getThresholdsForCamera(3);

		// 同步到分发线程
		updateAIThresholdsToDispatch();

		Logger::instance().info("AI parameter configuration updated and synced");
	}
}

void MetalStripVision::loadAIParamConfig()
{
	AlgorithmROIManager::instance().loadAIThresholds(m_aiThresholds);

	// 确保两个相机都有数据条目
	if (m_aiThresholds.find(2) == m_aiThresholds.end()) {
		m_aiThresholds[2] = std::vector<AIDefectThreshold>();
	}
	if (m_aiThresholds.find(3) == m_aiThresholds.end()) {
		m_aiThresholds[3] = std::vector<AIDefectThreshold>();
	}

	// 同步到分发线程（修复：初始化时也需要同步）
	updateAIThresholdsToDispatch();
}

void MetalStripVision::updateAIThresholdsToDispatch()
{
	// 同步到分发线程（绘制时筛选）
	if (m_dispatchThread2) {
		m_dispatchThread2->updateAIThresholds(m_aiThresholds[2]);
	}
	if (m_dispatchThread3) {
		m_dispatchThread3->updateAIThresholds(m_aiThresholds[3]);
	}
	// 同步到算法线程（NG判定时筛选）
	if (m_algoThread2) {
		m_algoThread2->updateAIThresholds(m_aiThresholds[2]);
	}
	if (m_algoThread3) {
		m_algoThread3->updateAIThresholds(m_aiThresholds[3]);
	}
}

// ===== 配置管理 =====
void MetalStripVision::onConfigManagementClicked()
{
	if (!m_isLoggedIn) {
		return;
	}

	// 只有管理员可以访问配置管理
	if (m_currentRole != "admin") {
		QMessageBox::warning(this, "权限不足", "只有管理员可以管理配置文件");
		return;
	}

	// 打开配置管理对话框
	ConfigManagerDialog dialog(this);
	dialog.exec();

	// 对话框关闭后，重新加载算法配置（如果配置被修改）
	reloadAlgorithmConfig();
}

void MetalStripVision::onManualCaptureClicked()
{
	if (m_running) {
		QMessageBox::warning(this, "提示", "请先停止检测再进行手动取图");
		return;
	}

	auto imgConfig = ConfigManager::instance().getImageSaveConfig();
	QString savePath = QString::fromStdString(imgConfig.savePath) + "/manual";

	if (!m_manualCaptureDialog) {
		m_manualCaptureDialog = new ManualCaptureDialog(
			&m_camera1, &m_camera2, &m_camera3, savePath, this);
	}
	m_manualCaptureDialog->exec();
}

void MetalStripVision::initializeAlgorithmROI()
{
	// 加载算法ROI配置文件
	AlgorithmROIManager::instance().loadConfig();

	// 相机1 - 传统算法初始化（根据 config.xml 配置）
	const CameraConfig& cam1Config = ConfigManager::instance().getCameraConfig(1);
	if (cam1Config.useTraditionalAlgo) {
		std::string defaultProfile = AlgorithmROIManager::instance().getDefaultProfile(1);
		if (!defaultProfile.empty()) {
			UserProfile profile = AlgorithmROIManager::instance().getUserProfile(1, defaultProfile);
			if (profile.roi.enabled && m_traditionalAlgo) {
				m_traditionalAlgo->updateROI(
					profile.roi.row1, profile.roi.column1,
					profile.roi.row2, profile.roi.column2);
				m_traditionalAlgo->updateThreshold(profile.thresholdMin, profile.thresholdMax);
				Logger::instance().info("Loaded default ROI profile for camera 1: " + defaultProfile);
			}
			ui.PartNumber_Label->setText(QString("料号：%1").arg(QString::fromStdString(defaultProfile)));
		}
	}

	// 相机2 - 传统算法初始化（根据 config.xml 配置）
	const CameraConfig& cam2Config = ConfigManager::instance().getCameraConfig(2);
	if (cam2Config.useTraditionalAlgo) {
		std::string defaultProfile = AlgorithmROIManager::instance().getDefaultProfile(2);
		if (!defaultProfile.empty()) {
			UserProfile profile = AlgorithmROIManager::instance().getUserProfile(2, defaultProfile);
			if (profile.roi.enabled && m_traditionalAlgo2) {
				m_traditionalAlgo2->updateROI(
						profile.roi.row1, profile.roi.column1,
						profile.roi.row2, profile.roi.column2);
					m_traditionalAlgo2->updateThreshold(profile.thresholdMin, profile.thresholdMax);
					Logger::instance().info("Loaded default ROI profile for camera 2: " + defaultProfile);
			}
		}
	}

	// 更新料号并同步到分发线程
	updatePartNumber();
}

void MetalStripVision::reloadAlgorithmConfig()
{
	// 重新加载配置文件
	AlgorithmROIManager::instance().loadConfig();

	// 相机1 - 重载配置（根据 config.xml 配置）
	const CameraConfig& cam1Config = ConfigManager::instance().getCameraConfig(1);
	if (cam1Config.useTraditionalAlgo) {
		std::string defaultProfile = AlgorithmROIManager::instance().getDefaultProfile(1);
		if (!defaultProfile.empty()) {
			UserProfile profile = AlgorithmROIManager::instance().getUserProfile(1, defaultProfile);
			if (profile.roi.enabled && m_traditionalAlgo) {
				m_traditionalAlgo->updateROI(
					profile.roi.row1, profile.roi.column1,
					profile.roi.row2, profile.roi.column2);
				m_traditionalAlgo->updateThreshold(profile.thresholdMin, profile.thresholdMax);
				Logger::instance().info("Reloaded ROI configuration for camera 1");
			}
			ui.PartNumber_Label->setText(QString("料号：%1").arg(QString::fromStdString(defaultProfile)));
		}
	}

	// 相机2 - 重载配置（根据 config.xml 配置）
	const CameraConfig& cam2Config = ConfigManager::instance().getCameraConfig(2);
	if (cam2Config.useTraditionalAlgo) {
		std::string defaultProfile = AlgorithmROIManager::instance().getDefaultProfile(2);
		if (!defaultProfile.empty()) {
			UserProfile profile = AlgorithmROIManager::instance().getUserProfile(2, defaultProfile);
			if (profile.roi.enabled && m_traditionalAlgo2) {
				m_traditionalAlgo2->updateROI(
						profile.roi.row1, profile.roi.column1,
						profile.roi.row2, profile.roi.column2);
					m_traditionalAlgo2->updateThreshold(profile.thresholdMin, profile.thresholdMax);
					Logger::instance().info("Reloaded ROI configuration for camera 2");
			}
		}
	}

	// 更新料号并同步到分发线程
	updatePartNumber();

	QMessageBox::information(this, "成功", "算法配置已重新加载");
}

// ===== 重新加载AI模型 =====
bool MetalStripVision::reloadAIModels()
{
	bool success = true;

	// 重新加载上表面模型（使用固定名称 "Up_metal"）
	// 注意：不再依赖 getCurrentModelName()，因为如果初始化时加载失败，
	// m_currentModelName 不会被设置，导致切换料号时无法重新加载
	if (m_aiAlgoUpper) {
		if (!m_aiAlgoUpper->loadModelByName("Up_metal")) {
			Logger::instance().error("Failed to reload upper surface AI model: Up_metal");
			success = false;
		} else {
			Logger::instance().info("Upper surface AI model reloaded: Up_metal");
		}
	}

	// 重新加载下表面模型（使用固定名称 "Down_metal"）
	if (m_aiAlgoLower) {
		if (!m_aiAlgoLower->loadModelByName("Down_metal")) {
			Logger::instance().error("Failed to reload lower surface AI model: Down_metal");
			success = false;
		} else {
			Logger::instance().info("Lower surface AI model reloaded: Down_metal");
		}
	}

	return success;
}

// ===== 更新料号并同步到分发线程 =====
void MetalStripVision::updatePartNumber()
{
	// 从用户集1获取默认料号
	std::string partNumber = AlgorithmROIManager::instance().getDefaultProfile(1);

	if (partNumber.empty()) {
		partNumber = "默认料号";
	}

	// 检查料号是否变化
	bool partNumberChanged = (m_currentPartNumber != partNumber);
	m_currentPartNumber = partNumber;

	// 更新UI显示
	ui.PartNumber_Label->setText(QString("料号：%1").arg(QString::fromStdString(partNumber)));

	// 设置AI算法的料号（用于构建模型路径）
	QString qPartNumber = QString::fromStdString(partNumber);
	m_aiAlgoUpper->setPartNumber(qPartNumber);
	m_aiAlgoLower->setPartNumber(qPartNumber);

	// 如果料号变化，重新加载AI模型
	if (partNumberChanged) {
		Logger::instance().info("料号变化，重新加载AI模型: " + partNumber);
		if (!reloadAIModels()) {
			// 模型加载失败，显示错误提示
			Logger::instance().error("料号 [" + partNumber + "] 的AI模型加载失败");
			QMessageBox::warning(this, "模型加载失败",
				QString("料号 [%1] 的AI模型加载失败！\n\n"
					   "请检查以下路径是否存在对应的模型文件：\n"
					   "• Model/%2/Trt/Up_metal.trt\n"
					   "• Model/%2/Tag/Up_metal.txt\n"
					   "• Model/%2/Trt/Down_metal.trt\n"
					   "• Model/%2/Tag/Down_metal.txt")
				.arg(QString::fromStdString(partNumber))
				.arg(QString::fromStdString(partNumber)));
		}
	}

	// 同步到所有分发线程
	if (m_dispatchThread1) {
		m_dispatchThread1->setPartNumber(partNumber);
	}
	if (m_dispatchThread2) {
		m_dispatchThread2->setPartNumber(partNumber);
	}
	if (m_dispatchThread3) {
		m_dispatchThread3->setPartNumber(partNumber);
	}

	Logger::instance().info("Part number updated: " + partNumber);
}

// ===== 禁用窗口关闭按钮 =====
void MetalStripVision::closeEvent(QCloseEvent* event)
{
	// 如果是正常退出（通过退出按钮），允许关闭
	if (m_isNormalExit) {
		event->accept();
		return;
	}

	// 禁用X按钮，只能通过退出按钮退出
	// 这样确保用户必须登录后才能退出程序
	event->ignore();

	// 如果未登录，提示需要登录才能退出
	if (!m_isLoggedIn) {
		QMessageBox::information(this, "提示",
			"请先登录后再退出程序！\n\n"
			"这是为了确保生产安全，防止误操作关闭程序。");
	} else {
		// 已登录，提示使用退出按钮
		QMessageBox::information(this, "提示",
			"请使用界面上的【退出】按钮退出程序！\n\n"
			"这样可以确保程序正常关闭，相机资源正确释放。");
	}
}

// ===== 测试模式实现 =====

// 辅助函数：从文件夹加载所有支持的图片格式
std::vector<std::string> MetalStripVision::loadImagesFromFolder(const std::string& folderPath, const std::string& cameraName)
{
	std::vector<std::string> imageFiles;
	std::vector<cv::String> files;

	// 支持的图片格式
	const std::vector<std::string> extensions = { "\\*.jpg", "\\*.png", "\\*.bmp", "\\*.tiff", "\\*.tif" };

	for (const auto& ext : extensions) {
		std::vector<cv::String> tmp;
		cv::glob(folderPath + ext, tmp);
		files.insert(files.end(), tmp.begin(), tmp.end());
	}

	// 转换为 std::string
	for (const auto& file : files) {
		imageFiles.emplace_back(file);
	}

	Logger::instance().info(cameraName + " test images: " + std::to_string(imageFiles.size()) + " found in " + folderPath);
	return imageFiles;
}

void MetalStripVision::loadTestImages()
{
	m_testImages1.clear();
	m_testImages2.clear();
	m_testImages3.clear();

	Logger::instance().info("Loading test images from: " + m_testImagePath);

	// 加载三个相机的测试图片
	m_testImages1 = loadImagesFromFolder(m_testImagePath + "\\camera1", "Camera1");
	m_testImages2 = loadImagesFromFolder(m_testImagePath + "\\camera2", "Camera2");
	m_testImages3 = loadImagesFromFolder(m_testImagePath + "\\camera3", "Camera3");

	Logger::instance().info("Test images loaded: Camera1=" + std::to_string(m_testImages1.size()) +
		", Camera2=" + std::to_string(m_testImages2.size()) +
		", Camera3=" + std::to_string(m_testImages3.size()));
}

cv::Mat MetalStripVision::getNextTestImage(int cameraId)
{
	std::vector<std::string>* images = nullptr;
	size_t* index = nullptr;

	switch (cameraId) {
	case 1:
		images = &m_testImages1;
		index = &m_testImageIndex1;
		break;
	case 2:
		images = &m_testImages2;
		index = &m_testImageIndex2;
		break;
	case 3:
		images = &m_testImages3;
		index = &m_testImageIndex3;
		break;
	default:
		return cv::Mat();
	}

	if (images->empty()) {
		return cv::Mat();
	}

	// 读取当前索引的图片（强制读取为彩色BGR）
	cv::Mat image = cv::imread(images->at(*index), cv::IMREAD_COLOR);
	if (image.empty()) {
		Logger::instance().warn("Failed to load test image: " + images->at(*index));
		return cv::Mat();
	}

	// 如果读取的是灰度图，强制转换为BGR（3通道）
	if (image.channels() == 1) {
		cv::cvtColor(image, image, cv::COLOR_GRAY2BGR);
		Logger::instance().debug("Converted grayscale test image to BGR: " + images->at(*index));
	} else if (image.channels() == 4) {
		// 如果是RGBA，转换为BGR
		cv::cvtColor(image, image, cv::COLOR_BGRA2BGR);
	}

	// 检查是否连续（Halcon需要连续数据）
	if (!image.isContinuous()) {
		Logger::instance().debug("Test image not continuous, cloning: " + images->at(*index));
		image = image.clone();  // clone()保证数据连续
	}

	// 更新索引（循环）
	auto testConfig = ConfigManager::instance().getTestModeConfig();
	if (testConfig.loopImages) {
		*index = (*index + 1) % images->size();
	} else {
		if (*index + 1 < images->size()) {
			(*index)++;
		}
	}

	return image;
}

// ===== 放大视图相关方法 =====

bool MetalStripVision::eventFilter(QObject* watched, QEvent* event)
{
	// 捕获相机窗口的鼠标点击事件
	if (event->type() == QEvent::MouseButtonPress) {
		int cameraId = 0;
		if (watched == ui.Camera1) {
			cameraId = 1;
		} else if (watched == ui.Camera2) {
			cameraId = 2;
		} else if (watched == ui.Camera3) {
			cameraId = 3;
		}

		if (cameraId > 0) {
			showEnlargedView(cameraId);
			return true;  // 事件已处理
		}
	}

	return QWidget::eventFilter(watched, event);
}

void MetalStripVision::showEnlargedView(int cameraId)
{
	// 如果窗口不存在，创建它
	if (!m_enlargedDialog) {
		m_enlargedDialog = new EnlargedViewDialog(this);
		connect(m_enlargedDialog, &EnlargedViewDialog::dialogClosed,
				this, &MetalStripVision::onEnlargedDialogClosed);
	}

	// 更新窗口标题并显示（非模态）
	// 优先使用带标注的图像（显示缺陷），没有则用原图
		{
			std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_cvMtx));
			cv::Mat enlargedImg;
			switch (cameraId) {
			case 1: enlargedImg = m_camera1Annotated.empty() ? m_camera1Image : m_camera1Annotated; break;
			case 2: enlargedImg = m_camera2Annotated.empty() ? m_camera2Image : m_camera2Annotated; break;
			case 3: enlargedImg = m_camera3Annotated.empty() ? m_camera3Image : m_camera3Annotated; break;
			}
			m_enlargedDialog->updateImage(cameraId, enlargedImg);
		}
	m_enlargedDialog->show();
	m_enlargedDialog->raise();        // 将窗口置顶
	m_enlargedDialog->activateWindow();  // 激活窗口
	m_enlargedDialogOpen = true;
}

void MetalStripVision::onEnlargedDialogClosed()
{
	m_enlargedDialogOpen = false;
}

// ========== 新架构实现 ==========

void MetalStripVision::initializeNewArchitecture()
{
	Logger::instance().info("Initializing new architecture...");

	// ========== 1. 创建图像队列（每个相机独立）==========
	m_imageQueue1 = std::make_unique<ThreadSafeQueue<ImageTask>>(10);
	m_imageQueue2 = std::make_unique<ThreadSafeQueue<ImageTask>>(10);
	m_imageQueue3 = std::make_unique<ThreadSafeQueue<ImageTask>>(10);

	// ========== 2. 创建分发队列（每个相机独立）==========
	m_dispatchQueue1 = std::make_unique<ThreadSafeQueue<DispatchTask>>(10);
	m_dispatchQueue2 = std::make_unique<ThreadSafeQueue<DispatchTask>>(10);
	m_dispatchQueue3 = std::make_unique<ThreadSafeQueue<DispatchTask>>(10);

	// ========== 3. 创建共享的UI和IO队列 ==========
	// UI队列：可丢帧，保持最新帧显示（队列大小3，每个相机一个槽位）
	m_uiQueue = std::make_shared<ThreadSafeQueue<UIDisplayTask>>(30);

	// IO队列：不能丢数据，保证完整保存（队列大小100）
	m_ioQueue = std::make_shared<ThreadSafeQueue<IOSaveTask>>(100);

	// ========== 4. 创建采图线程 ==========
	m_captureThreadNew1 = std::make_unique<CaptureThread>(1, &m_camera1, *m_imageQueue1);
	m_captureThreadNew2 = std::make_unique<CaptureThread>(2, &m_camera2, *m_imageQueue2);
	m_captureThreadNew3 = std::make_unique<CaptureThread>(3, &m_camera3, *m_imageQueue3);

	// 设置触发模式（根据相机配置）
	const auto& camConfig1 = ConfigManager::instance().getCameraConfig(1);
	const auto& camConfig2 = ConfigManager::instance().getCameraConfig(2);
	const auto& camConfig3 = ConfigManager::instance().getCameraConfig(3);

	m_captureThreadNew1->setTriggerMode(camConfig1.triggerMode);
	m_captureThreadNew2->setTriggerMode(camConfig2.triggerMode);
	m_captureThreadNew3->setTriggerMode(camConfig3.triggerMode);

	Logger::instance().info(QString("Trigger mode: Cam1=%1, Cam2=%2, Cam3=%3 (1=Soft, 2=Hard)")
		.arg(camConfig1.triggerMode).arg(camConfig2.triggerMode).arg(camConfig3.triggerMode).toStdString());

	// 设置测试模式（如果启用）
	if (m_testMode) {
		m_captureThreadNew1->setTestMode(true, m_testImages1, 100);  // 100ms 延迟
		m_captureThreadNew2->setTestMode(true, m_testImages2, 100);
		m_captureThreadNew3->setTestMode(true, m_testImages3, 100);
		Logger::instance().info("Test mode configured for CaptureThreads");
	}

	// ========== 5. 创建算法线程（传入分发队列而非UI/IO队列）==========
	m_algoThread1 = std::make_unique<AlgorithmThread>(1, *m_imageQueue1, *m_dispatchQueue1,
													  m_traditionalAlgo, nullptr, m_plc.get(), &m_camera1);
	m_algoThread2 = std::make_unique<AlgorithmThread>(2, *m_imageQueue2, *m_dispatchQueue2,
													  nullptr, m_aiAlgoUpper, m_plc.get(), &m_camera2);
	m_algoThread3 = std::make_unique<AlgorithmThread>(3, *m_imageQueue3, *m_dispatchQueue3,
													  nullptr, m_aiAlgoLower, m_plc.get(), &m_camera3);

	// ========== 6. 创建分发线程（每个相机独立）==========
	m_dispatchThread1 = std::make_unique<DispatchThread>(1, *m_dispatchQueue1, *m_uiQueue, *m_ioQueue, nullptr);
	m_dispatchThread2 = std::make_unique<DispatchThread>(2, *m_dispatchQueue2, *m_uiQueue, *m_ioQueue, m_aiAlgoUpper);
	m_dispatchThread3 = std::make_unique<DispatchThread>(3, *m_dispatchQueue3, *m_uiQueue, *m_ioQueue, m_aiAlgoLower);

	// 创建UI显示线程
	m_uiThread = std::make_unique<UIThread>(*m_uiQueue);
	connect(m_uiThread.get(), &UIThread::imageUpdated,
			this, [this](int cameraId, cv::Mat image, bool hasDefect, cv::Mat originalImage) {
				onImageUpdated(cameraId, image, hasDefect, originalImage);
			});
	connect(m_uiThread.get(), &UIThread::defectStatusUpdated,
			this, &MetalStripVision::onDefectStatusUpdated);

	// 创建IO保存线程
	m_ioThread = std::make_unique<IOThread>(*m_ioQueue, &DetectionLogDatabase::instance(), 3);
	ImageSaveConfig imgConfig = ConfigManager::instance().getImageSaveConfig();
	m_ioThread->setImageSaveConfig(imgConfig);
	connect(m_ioThread.get(), &IOThread::recordAdded,
			this, &MetalStripVision::onRecordAdded);
	connect(m_ioThread.get(), &IOThread::saveCompleted,
			this, [this](int cameraId, unsigned int frameNum, bool success) {
				onSaveCompleted(cameraId, frameNum, success);
			});

	// 连接算法线程信号
	connect(m_algoThread1.get(), &AlgorithmThread::algorithmCompleted,
			this, &MetalStripVision::onAlgorithmCompleted);
	connect(m_algoThread2.get(), &AlgorithmThread::algorithmCompleted,
			this, &MetalStripVision::onAlgorithmCompleted);
	connect(m_algoThread3.get(), &AlgorithmThread::algorithmCompleted,
			this, &MetalStripVision::onAlgorithmCompleted);

	Logger::instance().info("New architecture initialized");
}

void MetalStripVision::startNewArchitecture()
{
	Logger::instance().info("Starting new architecture...");

	// 启动采图线程
	if (m_camera1Initialized || m_testMode) {
		m_captureThreadNew1->start();
	}
	if (m_camera2Initialized || m_testMode) {
		m_captureThreadNew2->start();
	}
	if (m_camera3Initialized || m_testMode) {
		m_captureThreadNew3->start();
	}

	// 启动算法线程
	if (m_camera1Initialized || m_testMode) {
		m_algoThread1->start();
	}
	if (m_camera2Initialized || m_testMode) {
		m_algoThread2->start();
	}
	if (m_camera3Initialized || m_testMode) {
		m_algoThread3->start();
	}

	// 启动分发线程
	if (m_camera1Initialized || m_testMode) {
		m_dispatchThread1->start();
	}
	if (m_camera2Initialized || m_testMode) {
		m_dispatchThread2->start();
	}
	if (m_camera3Initialized || m_testMode) {
		m_dispatchThread3->start();
	}

	// 启动后处理线程
	// if (m_postProcessThread) m_postProcessThread->start();

	// 启动UI显示线程
	if (m_uiThread) {
		m_uiThread->start();
	}

	// 启动IO保存线程
	if (m_ioThread) {
		m_ioThread->start();
	}

	Logger::instance().info("New architecture started");

	// 同步AI阈值到分发线程
	updateAIThresholdsToDispatch();
}

void MetalStripVision::stopNewArchitecture()
{
	Logger::instance().info("Stopping new architecture...");

	// 停止采图线程
	if (m_captureThreadNew1) m_captureThreadNew1->stop();
	if (m_captureThreadNew2) m_captureThreadNew2->stop();
	if (m_captureThreadNew3) m_captureThreadNew3->stop();

	// 停止算法线程
	if (m_algoThread1) m_algoThread1->stop();
	if (m_algoThread2) m_algoThread2->stop();
	if (m_algoThread3) m_algoThread3->stop();

	// 停止分发线程
	if (m_dispatchThread1) m_dispatchThread1->stop();
	if (m_dispatchThread2) m_dispatchThread2->stop();
	if (m_dispatchThread3) m_dispatchThread3->stop();

	// 停止后处理线程
	// if (m_postProcessThread) m_postProcessThread->stop();

	// 停止UI显示线程
	if (m_uiThread) m_uiThread->stop();

	// 停止IO保存线程
	if (m_ioThread) m_ioThread->stop();

	Logger::instance().info("New architecture stopped");
}

void MetalStripVision::onImageUpdated(int cameraId, cv::Mat image, bool hasDefect, const cv::Mat& originalImage)
{
	// 不限制帧率，每帧都显示

	// 更新缺陷状态
	switch (cameraId) {
	case 1:
		m_camera1HasDefect.store(hasDefect);
		break;
	case 2:
		m_camera2HasDefect.store(hasDefect);
		break;
	case 3:
		m_camera3HasDefect.store(hasDefect);
		break;
	}

		// 使用原始分辨率图像存储（用于getCurrentCameraImage，ROI绘制需要原始分辨率）
		if (!originalImage.empty()) {
			std::lock_guard<std::mutex> lock(m_cvMtx);
			switch (cameraId) {
			case 1:
				m_camera1Image = originalImage;
				break;
			case 2:
				m_camera2Image = originalImage;
				break;
			case 3:
				m_camera3Image = originalImage;
				break;
			}
		}

		// 存储带标注的图像（用于放大镜显示缺陷）
		if (!image.empty()) {
			std::lock_guard<std::mutex> lock(m_cvMtx);
			switch (cameraId) {
			case 1:
				m_camera1Annotated = image;
				break;
			case 2:
				m_camera2Annotated = image;
				break;
			case 3:
				m_camera3Annotated = image;
				break;
			}
		}

	// ===== 关键优化：先获取显示尺寸，再用cv::resize缩放 =====
	QSize targetSize;
	switch (cameraId) {
	case 1:
		targetSize = ui.Camera1->size();
		break;
	case 2:
		targetSize = ui.Camera2->size();
		break;
	case 3:
		targetSize = ui.Camera3->size();
		break;
	}

	// 计算保持宽高比的缩放尺寸
	double aspectRatio = static_cast<double>(image.cols) / image.rows;
	int targetWidth = targetSize.width();
	int targetHeight = static_cast<int>(targetWidth / aspectRatio);

	if (targetHeight > targetSize.height()) {
		targetHeight = targetSize.height();
		targetWidth = static_cast<int>(targetHeight * aspectRatio);
	}

	// 使用cv::resize进行高效缩放（比Qt scaled更快）
	cv::Mat resizedImage;
	cv::resize(image, resizedImage, cv::Size(targetWidth, targetHeight),
	           0, 0, cv::INTER_LINEAR);

	// 在缩放后的图像上绘制检测结果
	DetectionState state = hasDefect ? DetectionState::NG : DetectionState::OK;
	cv::Mat displayImg = draw_pic(resizedImage, state);

	// 转换为QImage（图像已缩小，无需copy）
	QImage qImg = Mat2QImage(displayImg);

	// 直接显示，无需再次缩放
	switch (cameraId) {
	case 1:
		ui.Camera1->setPixmap(QPixmap::fromImage(qImg));
		break;
	case 2:
		ui.Camera2->setPixmap(QPixmap::fromImage(qImg));
		break;
	case 3:
		ui.Camera3->setPixmap(QPixmap::fromImage(qImg));
		break;
	}

	// 更新放大视图窗口（如果打开的话）- 放大视图使用带标注的图像
	if (m_enlargedDialogOpen && m_enlargedDialog) {
		int camId = m_enlargedDialog->getCurrentCameraId();
		if (camId == cameraId) {
			m_enlargedDialog->updateImage(cameraId, image);
		}
	}
}

void MetalStripVision::onRecordAdded(const QString& record)
{
	// 添加到NG记录显示（使用NG_textedit）
	ui.NG_textedit->append(record);

	// 限制显示数量
	QString text = ui.NG_textedit->toPlainText();
	QStringList lines = text.split('\n');
	if (lines.size() > 100) {
		// 保留最近100条
		lines = lines.mid(lines.size() - 100);
		ui.NG_textedit->setPlainText(lines.join('\n'));
	}
}

void MetalStripVision::onAlgorithmCompleted(int cameraId, bool hasDefect, double processingTime)
{
	// 更新统计信息
	Logger::instance().debug(QString("[TIME] Camera%1 algorithm completed: %2, time=%3ms")
		.arg(cameraId)
		.arg(hasDefect ? "NG" : "OK")
		.arg(processingTime, 0, 'f', 2).toStdString());
}

void MetalStripVision::onDefectStatusUpdated(int cameraId, bool hasDefect, bool algorithmFailed)
{
	// 更新算法失败状态
	switch (cameraId) {
	case 1:
		m_camera1Fail.store(algorithmFailed);
		if (hasDefect) m_traditionalDefectCount++;
		m_traditionalTotalCount++;
		break;
	case 2:
		m_camera2Fail.store(algorithmFailed);
		if (hasDefect) m_aiDefectCount++;
		m_aiUpperTotalCount++;
		break;
	case 3:
		m_camera3Fail.store(algorithmFailed);
		if (hasDefect) m_aiDefectCount++;
		m_aiLowerTotalCount++;
		break;
	}

	// 更新统计显示
	int totalCount = m_traditionalTotalCount.load() + m_aiUpperTotalCount.load() + m_aiLowerTotalCount.load();
	int totalDefect = m_traditionalDefectCount.load() + m_aiDefectCount.load();

	ui.Total_Edit->setText(QString::number(totalCount));
	ui.defective_lineEdit->setText(QString::number(totalDefect));

	// 计算NG率
	if (totalCount > 0) {
		double rate = (double)totalDefect / totalCount * 100.0;
		ui.Defective_Rate_Edit->setText(QString("%1%").arg(rate, 0, 'f', 1));
	} else {
		ui.Defective_Rate_Edit->setText("0.0%");
	}
}

void MetalStripVision::onSaveCompleted(int cameraId, unsigned int frameNum, bool success)
{
	if (!success) {
		Logger::instance().error(QString("Camera%1 frame %2 save failed")
			.arg(cameraId).arg(frameNum).toStdString());
	}
}
