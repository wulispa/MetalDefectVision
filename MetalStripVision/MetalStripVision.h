#pragma once

#include <QtWidgets/QWidget>
#include "ui_MetalStripVision.h"
#include "CameraHK1.h"
#include "ConfigManager.h"
#include "IAlgorithm.h"
#include "HalconAlgorithm.h"
#include "YOLOAlgorithm.h"
#include "AIParamConfigDialog.h"
#include "ThreadSafeQueue.h"
#include "ImageTask.h"
#include "CaptureThread.h"
#include "AlgorithmThread.h"
#include "UIThread.h"
#include "IOThread.h"
#include "DispatchThread.h"
#include <algorithm>
#include <QMessageBox>
#include <thread>
#include <mutex>
#include <QFile>
#include <QTimer>
#include <QDateTime>
#include <QCloseEvent>
#include <fstream>
#include <memory>
#include <vector>
#include <atomic>
#include "UserManager.h"
#include "DetectionLogDatabase.h"
#include "PLC_Interface.h"

class LoginDialog;
class ChangePasswordDialog;
class ConfigManagerDialog;
class EnlargedViewDialog;
class ManualCaptureDialog;

// 检测结果状态枚举
enum class DetectionState {
	OK,     // 正常（绿色）
	NG,     // 次品（红色）
	FAIL    // 失败（黄色）
};

class MetalStripVision : public QWidget
{
	Q_OBJECT

public:
	MetalStripVision(QWidget *parent = nullptr);
	~MetalStripVision();

public:

	// 检测结果（4个变量）
	// 传统算法检测结果（相机1）
	std::atomic<int> m_traditionalTotalCount{0};   // 传统算法检测总数
	std::atomic<int> m_traditionalDefectCount{0};  // 传统算法次品数

	// AI 算法检测结果（相机2/3）
	std::atomic<int> m_aiUpperTotalCount{0};       // AI上表面检测总数（相机2）
	std::atomic<int> m_aiLowerTotalCount{0};       // AI下表面检测总数（相机3）
	std::atomic<int> m_aiDefectCount{0};           // AI算法次品总数（相机2+3）

	void Camera_Init();
	void loadConfiguration();

	void updateSystemTime();
	void onDataStatsClicked();

	// 获取当前相机图像（用于参数设置对话框的实时预览）
	cv::Mat getCurrentCameraImage(int cameraId) const;

	// 获取检测状态（用于参数设置对话框判断是否可切换料号）
	bool isDetecting() const { return m_running.load(); }

public slots:
	void updateNGRecordDisplay(const QString& record);  // 更新NG记录显示（槽函数）
	void Detect_Btn_click();
	void exit_Btn_click();

	// 测试模式
	void loadTestImages();  // 加载测试图片列表
	cv::Mat getNextTestImage(int cameraId);  // 获取下一个测试图片

signals:
	void newNGRecord(const QString& record);  // 新增NG记录信号（跨线程）

private:
	// 测试模式辅助函数 - 从文件夹加载图片
	std::vector<std::string> loadImagesFromFolder(const std::string& folderPath, const std::string& cameraName);

protected:
	// 禁用窗口关闭按钮（X按钮），只能通过退出按钮退出
	void closeEvent(QCloseEvent* event) override;
	// 事件过滤器 - 用于捕获相机窗口的点击事件
	bool eventFilter(QObject* watched, QEvent* event) override;


	// 算法处理（重构）
	void processFrame();

	// 在图像上绘制检测结果（NG/OK）
	// 在图像上绘制检测结果（支持 OK/NG/FAIL 三种状态）
	cv::Mat draw_pic(cv::Mat img, DetectionState state);


private:
	// 相机配置
	int TriggerModel = -1;

	// 直接使用3个相机实例
	CameraHK1 m_camera1;
	CameraHK1 m_camera2;
	CameraHK1 m_camera3;

	// 算法实例
	std::shared_ptr<HalconAlgorithm> m_traditionalAlgo;   // 传统算法（相机1）
	std::shared_ptr<HalconAlgorithm> m_traditionalAlgo2;  // 传统算法（相机2）
	std::shared_ptr<YOLOAlgorithm> m_aiAlgoUpper;   // 上表面AI算法（相机2）
	std::shared_ptr<YOLOAlgorithm> m_aiAlgoLower;   // 下表面AI算法（相机3）

	// AI检测框尺寸显示配置
	BBoxSizeDisplayConfig m_bboxSizeConfig;

	// AI缺陷尺寸阈值配置（cameraId -> 阈值列表）
	std::map<int, std::vector<AIDefectThreshold>> m_aiThresholds;

	// PLC通信
	std::unique_ptr<PLCInterface> m_plc;

	// 图像存储
	cv::Mat m_camera1Image;
	cv::Mat m_camera2Image;
	cv::Mat m_camera3Image;
	// 带标注的图像（用于放大镜显示）
	cv::Mat m_camera1Annotated;
	cv::Mat m_camera2Annotated;
	cv::Mat m_camera3Annotated;

	// 相机初始化状态
	std::atomic<bool> m_camera1Initialized{false};  // 相机1初始化状态
	std::atomic<bool> m_camera2Initialized{false};  // 相机2初始化状态
	std::atomic<bool> m_camera3Initialized{false};  // 相机3初始化状态

	// 线程控制标志
	std::atomic<bool> m_running{false};  // 是否在检测
	std::atomic<bool> m_stop{false};     // 程序是否退出

	// 算法失败状态（用于UI显示FAIL提示）
	std::atomic<bool> m_camera1Fail{false};  // 相机1算法是否失败
	std::atomic<bool> m_camera2Fail{false};  // 相机2算法是否失败
	std::atomic<bool> m_camera3Fail{false};  // 相机3算法是否失败

	// 当前帧缺陷状态（用于UI显示NG/OK）
	std::atomic<bool> m_camera1HasDefect{false};  // 相机1当前帧是否有缺陷
	std::atomic<bool> m_camera2HasDefect{false};  // 相机2当前帧是否有缺陷
	std::atomic<bool> m_camera3HasDefect{false};  // 相机3当前帧是否有缺陷

	// UI更新帧率限制（避免UI卡顿）
	std::chrono::steady_clock::time_point m_lastUIUpdate1{};
	std::chrono::steady_clock::time_point m_lastUIUpdate2{};
	std::chrono::steady_clock::time_point m_lastUIUpdate3{};

	// 同步原语
	mutable std::mutex m_cvMtx;
	std::mutex m_saveFileMtx;  // 保护文件保存操作的互斥锁
	int frameId = 0;

	Ui::MetalStripVisionClass ui;
	QTimer* timer;
	QTimer* m_timeTimer;  // 系统时间更新定时器

	// 登录状态管理
	bool m_isLoggedIn = false;
	QString m_currentRole;  // "admin" or "operator" or empty
	bool m_isNormalExit = false;  // 标记是否是正常退出（通过退出按钮）

	// 测试模式
	bool m_testMode = false;  // 测试模式开关
	std::vector<std::string> m_testImages1;  // 相机1测试图片列表
	std::vector<std::string> m_testImages2;  // 相机2测试图片列表
	std::vector<std::string> m_testImages3;  // 相机3测试图片列表
	size_t m_testImageIndex1 = 0;  // 相机1当前图片索引
	size_t m_testImageIndex2 = 0;  // 相机2当前图片索引
	size_t m_testImageIndex3 = 0;  // 相机3当前图片索引
	std::string m_testImagePath;   // 测试图片路径

	// 当前料号（从用户集1获取，用于图片保存路径分类）
	std::string m_currentPartNumber;

	// 放大视图窗口
	EnlargedViewDialog* m_enlargedDialog = nullptr;
	ManualCaptureDialog* m_manualCaptureDialog = nullptr;
	std::atomic<bool> m_enlargedDialogOpen{false};

	// 权限控制
	void onUnlockBtnClicked();
	void logout();
	void updateUIByPermission();
	bool hasPermission(const QString& action) const;
	void onChangePasswordClicked();
	void onParameterSettingsClicked();  // 参数设置槽函数
	void onAIParamSettingsClicked();   // AI参数设置槽函数
	void onConfigManagementClicked();  //
	void onManualCaptureClicked();     // 配置管理槽函数

	// 放大视图相关
	void showEnlargedView(int cameraId);
	void onEnlargedDialogClosed();

	void initializeAlgorithmROI();  // 初始化算法ROI配置
	void reloadAlgorithmConfig();    // 重新加载算法配置
	void loadAIParamConfig();        // 加载AI参数配置
	void updateAIThresholdsToDispatch(); // 同步AI阈值到分发线程
	void updatePartNumber();         // 更新料号并同步到分发线程
	bool reloadAIModels();           // 重新加载AI模型（根据当前料号）

	// ========== 新架构：队列和线程 ==========
	// 每个相机独立的图像队列（采图 -> 算法）
	std::unique_ptr<ThreadSafeQueue<ImageTask>> m_imageQueue1;
	std::unique_ptr<ThreadSafeQueue<ImageTask>> m_imageQueue2;
	std::unique_ptr<ThreadSafeQueue<ImageTask>> m_imageQueue3;

	// 每个相机独立的分发队列（算法 -> 分发）
	std::unique_ptr<ThreadSafeQueue<DispatchTask>> m_dispatchQueue1;
	std::unique_ptr<ThreadSafeQueue<DispatchTask>> m_dispatchQueue2;
	std::unique_ptr<ThreadSafeQueue<DispatchTask>> m_dispatchQueue3;

	// UI显示队列（可丢帧）- 所有相机共享
	std::shared_ptr<ThreadSafeQueue<UIDisplayTask>> m_uiQueue;

	// IO保存队列（不能丢数据）- 所有相机共享
	std::shared_ptr<ThreadSafeQueue<IOSaveTask>> m_ioQueue;

	// 采图线程实例
	std::unique_ptr<CaptureThread> m_captureThreadNew1;
	std::unique_ptr<CaptureThread> m_captureThreadNew2;
	std::unique_ptr<CaptureThread> m_captureThreadNew3;

	// 算法线程实例
	std::unique_ptr<AlgorithmThread> m_algoThread1;
	std::unique_ptr<AlgorithmThread> m_algoThread2;
	std::unique_ptr<AlgorithmThread> m_algoThread3;

	// 分发线程实例（每个相机独立）
	std::unique_ptr<DispatchThread> m_dispatchThread1;
	std::unique_ptr<DispatchThread> m_dispatchThread2;
	std::unique_ptr<DispatchThread> m_dispatchThread3;

	// UI显示线程实例（单例，处理所有相机的UI更新）
	std::unique_ptr<UIThread> m_uiThread;

	// IO保存线程实例（单例，处理所有相机的IO保存）
	std::unique_ptr<IOThread> m_ioThread;

	// 新架构标志
	bool m_useNewArchitecture = false;  // 是否使用新架构

	// 新架构初始化
	void initializeNewArchitecture();
	void startNewArchitecture();
	void stopNewArchitecture();

	// 新架构槽函数
	void onImageUpdated(int cameraId, cv::Mat image, bool hasDefect, const cv::Mat& originalImage);
	void onDefectStatusUpdated(int cameraId, bool hasDefect, bool algorithmFailed);
	void onRecordAdded(const QString& record);
	void onAlgorithmCompleted(int cameraId, bool hasDefect, double processingTime);
	void onSaveCompleted(int cameraId, unsigned int frameNum, bool success);

};

// 工具函数
QImage Mat2QImage(const cv::Mat& mat);

