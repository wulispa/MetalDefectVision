#include "ManualCaptureDialog.h"
#include "Logger.h"

ManualCaptureDialog::ManualCaptureDialog(CameraHK1* cam1, CameraHK1* cam2, CameraHK1* cam3,
                                           const QString& savePath, QWidget* parent)
    : QDialog(parent)
    , m_savePath(savePath)
{
    m_cameras[0] = cam1;
    m_cameras[1] = cam2;
    m_cameras[2] = cam3;

    setupUI();
}

void ManualCaptureDialog::setupUI() {
    setWindowTitle("手动取图");
    setMinimumSize(500, 400);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // 顶部：相机选择和操作按钮
    QHBoxLayout* topLayout = new QHBoxLayout();

    topLayout->addWidget(new QLabel("选择相机:"));
    m_cameraCombo = new QComboBox();
    m_cameraCombo->addItem("Camera 1", 0);
    m_cameraCombo->addItem("Camera 2", 1);
    m_cameraCombo->addItem("Camera 3", 2);
    topLayout->addWidget(m_cameraCombo);

    m_captureBtn = new QPushButton("取图");
    m_captureBtn->setStyleSheet(
        "QPushButton { background-color: #a6e3a1; color: #1e1e2e; border: none; "
        "border-radius: 6px; padding: 8px 20px; font-weight: bold; font-size: 14px; }"
        "QPushButton:hover { background-color: #94e2d5; }");
    connect(m_captureBtn, &QPushButton::clicked, this, &ManualCaptureDialog::onCaptureClicked);
    topLayout->addWidget(m_captureBtn);

    m_saveBtn = new QPushButton("保存");
    m_saveBtn->setEnabled(false);
    m_saveBtn->setStyleSheet(
        "QPushButton { background-color: #89b4fa; color: #1e1e2e; border: none; "
        "border-radius: 6px; padding: 8px 20px; font-weight: bold; font-size: 14px; }"
        "QPushButton:hover { background-color: #74c7ec; }"
        "QPushButton:disabled { background-color: #45475a; color: #6c7086; }");
    connect(m_saveBtn, &QPushButton::clicked, this, &ManualCaptureDialog::onSaveClicked);
    topLayout->addWidget(m_saveBtn);

    topLayout->addStretch();
    mainLayout->addLayout(topLayout);

    // 中间：图片预览
    QGroupBox* previewGroup = new QGroupBox("图片预览");
    QVBoxLayout* previewLayout = new QVBoxLayout(previewGroup);
    m_previewLabel = new QLabel("点击取图按钮采集图片");
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setMinimumHeight(300);
    m_previewLabel->setStyleSheet("QLabel { background-color: #313244; color: #6c7086; "
                                   "border: 1px solid #45475a; border-radius: 4px; }");
    previewLayout->addWidget(m_previewLabel);
    mainLayout->addWidget(previewGroup);

    // 底部：状态标签
    m_statusLabel = new QLabel("就绪");
    m_statusLabel->setStyleSheet("QLabel { color: #a6adc8; font-size: 12px; }");
    mainLayout->addWidget(m_statusLabel);
}

void ManualCaptureDialog::onCaptureClicked() {
    int cameraIndex = m_cameraCombo->currentIndex();
    CameraHK1* camera = m_cameras[cameraIndex];

    if (!camera || !camera->IsOpen()) {
        m_statusLabel->setText("相机未连接，请检查相机状态");
        m_statusLabel->setStyleSheet("QLabel { color: #f38ba8; font-size: 12px; }");
        return;
    }

    m_statusLabel->setText("正在取图...");
    m_statusLabel->setStyleSheet("QLabel { color: #f9e2af; font-size: 12px; }");
    QApplication::processEvents();

    // 保存当前触发模式
    int savedMode = camera->getTriggerMode();

    // 切换到软触发模式
    camera->SetTriggerModel(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    camera->ClearBuff();

    // 发送软触发 + 等待图像就绪 + 取图
    camera->TriggerSoftware();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    cv::Mat image = camera->GetImage();

    // 恢复原始触发模式（使用SetTriggerModel确保currentTriggerMode同步更新）
    camera->SetTriggerModel(savedMode);
    camera->ClearBuff();  // 恢复后清空缓存，避免残留数据干扰后续硬触发

    if (image.empty()) {
        m_statusLabel->setText("取图失败，请检查相机连接");
        m_statusLabel->setStyleSheet("QLabel { color: #f38ba8; font-size: 12px; }");
        return;
    }

    // 显示预览
    m_capturedImage = image;
    m_capturedCameraIndex = cameraIndex;

    QImage qImg = Mat2QImage(image);
    QPixmap pixmap = QPixmap::fromImage(qImg).scaled(
        m_previewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_previewLabel->setPixmap(pixmap);
    m_previewLabel->setStyleSheet("");

    m_statusLabel->setText(QString("Camera%1 取图成功 (%2x%3)，点击保存按钮保存图片")
        .arg(cameraIndex + 1).arg(image.cols).arg(image.rows));
    m_statusLabel->setStyleSheet("QLabel { color: #a6e3a1; font-size: 12px; }");
    m_saveBtn->setEnabled(true);

    Logger::instance().info("Manual capture: Camera" + std::to_string(cameraIndex + 1) +
                           " (" + std::to_string(image.cols) + "x" + std::to_string(image.rows) + ")");
}

void ManualCaptureDialog::onSaveClicked() {
    if (m_capturedImage.empty() || m_capturedCameraIndex < 0) return;

    QDir().mkpath(m_savePath);

    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz");
    QString filename = QString("%1/Camera%2_%3.bmp")
        .arg(m_savePath)
        .arg(m_capturedCameraIndex + 1)
        .arg(timestamp);

    if (cv::imwrite(filename.toStdString(), m_capturedImage)) {
        m_statusLabel->setText("保存成功: " + filename);
        m_statusLabel->setStyleSheet("QLabel { color: #a6e3a1; font-size: 12px; }");
        Logger::instance().info("Manual capture saved: " + filename.toStdString());
    } else {
        m_statusLabel->setText("保存失败，请检查保存路径");
        m_statusLabel->setStyleSheet("QLabel { color: #f38ba8; font-size: 12px; }");
        Logger::instance().error("Manual capture save failed: " + filename.toStdString());
    }
}
