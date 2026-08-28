#pragma execution_character_set("utf-8")

#include "ParameterSettingsDialog.h"
#include "ui_ParameterSettingsDialog.h"
#include "Logger.h"
#include "MetalStripVision.h"
#include <QPainter>
#include <QMouseEvent>
#include <QInputDialog>
#include <QFileDialog>
#include <QTimer>
#include <QTextStream>
#include <QDir>
#include <QFile>

// ========== ImageDisplayWidget 实现 ==========

ImageDisplayWidget::ImageDisplayWidget(QWidget* parent)
    : QLabel(parent), m_roiMode(false), m_drawing(false) {
    setMinimumSize(640, 480);
    setStyleSheet("QLabel { background-color: #2b2b2b; border: 2px solid #555; }");
    setAlignment(Qt::AlignCenter);
    setText("暂无图片\n请选择用户集并加载图片");
}

void ImageDisplayWidget::setImage(const cv::Mat& image) {
    m_image = image.clone();
    if (!m_image.empty()) {
        // 转换BGR到RGB
        cv::Mat rgbImage;
        cv::cvtColor(m_image, rgbImage, cv::COLOR_BGR2RGB);

        // 缩放到合适大小
        QSize labelSize = size();
        cv::Mat resizedImage;
        double scale = std::min(
            static_cast<double>(labelSize.width()) / rgbImage.cols,
            static_cast<double>(labelSize.height()) / rgbImage.rows
        );
        if (scale < 1.0) {
            cv::resize(rgbImage, resizedImage, cv::Size(), scale, scale);
        } else {
            resizedImage = rgbImage;
        }

        // 转换为QImage
        QImage qImage(resizedImage.data, resizedImage.cols, resizedImage.rows,
                      static_cast<int>(resizedImage.step), QImage::Format_RGB888);
        setPixmap(QPixmap::fromImage(qImage));
    } else {
        clear();
        setText("暂无图片\n请选择用户集并加载图片");
    }
    update();
}

void ImageDisplayWidget::setROI(const AlgorithmROIConfig& roi) {
    m_currentROI = roi;
    update();
}

void ImageDisplayWidget::setROIMode(bool enabled) {
    m_roiMode = enabled;
    if (!enabled) {
        m_drawing = false;
    }
}

void ImageDisplayWidget::mousePressEvent(QMouseEvent* event) {
    if (!m_roiMode || m_image.empty()) return;

    if (event->button() == Qt::LeftButton) {
        m_drawing = true;
        m_startPoint = event->pos();
        m_endPoint = event->pos();
    }
}

void ImageDisplayWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!m_drawing) return;

    m_endPoint = event->pos();
    update();
}

void ImageDisplayWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (!m_drawing) return;

    if (event->button() == Qt::LeftButton) {
        m_drawing = false;

        // 转换为图像坐标
        cv::Point startImg = mapToImagePoint(m_startPoint);
        cv::Point endImg = mapToImagePoint(m_endPoint);

        // 更新ROI
        m_currentROI.row1 = std::min(startImg.y, endImg.y);
        m_currentROI.column1 = std::min(startImg.x, endImg.x);
        m_currentROI.row2 = std::max(startImg.y, endImg.y);
        m_currentROI.column2 = std::max(startImg.x, endImg.x);
        m_currentROI.enabled = true;

        update();  // 触发重绘显示框

        emit roiChanged(m_currentROI);
    }
}

void ImageDisplayWidget::paintEvent(QPaintEvent* event) {
    QLabel::paintEvent(event);

    if (!m_image.empty() && m_currentROI.enabled) {
        QPainter painter(this);
        painter.setPen(QPen(Qt::green, 2));

        // 将ROI坐标转换为控件坐标
        cv::Point topLeftImg(m_currentROI.column1, m_currentROI.row1);
        cv::Point bottomRightImg(m_currentROI.column2, m_currentROI.row2);

        QPoint topLeft = mapToWidgetPoint(topLeftImg);
        QPoint bottomRight = mapToWidgetPoint(bottomRightImg);

        // 绘制矩形框
        QRect roiRect(topLeft, bottomRight);
        painter.drawRect(roiRect);
    }
}

cv::Point ImageDisplayWidget::mapToImagePoint(const QPoint& widgetPoint) const
{
    if (m_image.empty())
        return cv::Point(0, 0);

    QPixmap pix = this->pixmap(Qt::ReturnByValue);
    if (pix.isNull())
        return cv::Point(0, 0);

    // 计算图片在控件中的偏移（居中对齐）
    int offsetX = (width() - pix.width()) / 2;
    int offsetY = (height() - pix.height()) / 2;

    // 从控件坐标转换为 pixmap 坐标
    int pixmapX = widgetPoint.x() - offsetX;
    int pixmapY = widgetPoint.y() - offsetY;

    // 计算缩放比例
    double scale_x = static_cast<double>(m_image.cols) / pix.width();
    double scale_y = static_cast<double>(m_image.rows) / pix.height();

    // 转换为图片坐标
    int imgX = static_cast<int>(pixmapX * scale_x);
    int imgY = static_cast<int>(pixmapY * scale_y);

    // 边界检查，防止越界
    imgX = std::max(0, std::min(imgX, m_image.cols - 1));
    imgY = std::max(0, std::min(imgY, m_image.rows - 1));

    return cv::Point(imgX, imgY);
}

QPoint ImageDisplayWidget::mapToWidgetPoint(const cv::Point& imagePoint) const
{
    if (m_image.empty())
        return QPoint(0, 0);

    QPixmap pix = this->pixmap(Qt::ReturnByValue);
    if (pix.isNull())
        return QPoint(0, 0);

    // 计算图片在控件中的偏移（居中对齐）
    int offsetX = (width() - pix.width()) / 2;
    int offsetY = (height() - pix.height()) / 2;

    // 计算缩放比例
    double scale_x = static_cast<double>(pix.width()) / m_image.cols;
    double scale_y = static_cast<double>(pix.height()) / m_image.rows;

    // 转换为控件坐标（加上偏移）
    int widgetX = static_cast<int>(imagePoint.x * scale_x) + offsetX;
    int widgetY = static_cast<int>(imagePoint.y * scale_y) + offsetY;

    return QPoint(widgetX, widgetY);
}

// ========== ParameterSettingsDialog 实现 ==========

ParameterSettingsDialog::ParameterSettingsDialog(const QString& role, QWidget* parent)
    : QDialog(parent), ui(new Ui::ParameterSettingsDialog),
      m_currentCameraId(1), m_modified(false), m_currentRole(role) {
    ui->setupUi(this);

    // 设置用户集管理按钮的样式（包括禁用状态）
    QString profileBtnStyle =
        "QPushButton {"
        "  background-color: #45475a;"
        "  color: #cdd6f4;"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 8px 16px;"
        "  font-size: 14px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #585b70;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #313244;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #1e1e2e;"
        "  color: #45475a;"
        "  border: 1px solid #313244;"
        "}";

    ui->newProfileBtn->setStyleSheet(profileBtnStyle);
    ui->renameProfileBtn->setStyleSheet(profileBtnStyle);
    ui->deleteProfileBtn->setStyleSheet(profileBtnStyle);
    ui->setDefaultBtn->setStyleSheet(profileBtnStyle);

    setupConnections();
    loadCameraList();

    // 确保参考图片目录存在
    ensureReferenceImagesDirectoryExists();

    // 设置图片显示控件
    ui->imageDisplay->setROIMode(true);

    // 根据角色限制UI功能
    bool isAdmin = (m_currentRole == "admin");

    if (!isAdmin) {
        // 操作员：禁用新建、重命名、删除按钮
        ui->newProfileBtn->setEnabled(false);
        ui->renameProfileBtn->setEnabled(false);
        ui->deleteProfileBtn->setEnabled(false);

        // 禁用ROI编辑（设置为只读模式）
        ui->imageDisplay->setROIMode(false);

        // 禁用阈值编辑
        ui->thresholdMinEdit->setReadOnly(true);
        ui->thresholdMaxEdit->setReadOnly(true);
    }

    // 延迟加载配置，避免阻塞UI
    QTimer::singleShot(50, this, [this]() {
        // 加载默认相机（相机1）
        ui->cameraCombo->setCurrentIndex(0);
        onCameraChanged(0);
    });
}

ParameterSettingsDialog::~ParameterSettingsDialog() {
    delete ui;
}

void ParameterSettingsDialog::setupConnections() {
    // 相机选择
    connect(ui->cameraCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ParameterSettingsDialog::onCameraChanged);

    // 用户集选择
    connect(ui->profileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ParameterSettingsDialog::onProfileChanged);

    // 用户集管理按钮
    connect(ui->newProfileBtn, &QPushButton::clicked, this, &ParameterSettingsDialog::onNewProfile);
    connect(ui->renameProfileBtn, &QPushButton::clicked, this, &ParameterSettingsDialog::onRenameProfile);
    connect(ui->deleteProfileBtn, &QPushButton::clicked, this, &ParameterSettingsDialog::onDeleteProfile);
    connect(ui->setDefaultBtn, &QPushButton::clicked, this, &ParameterSettingsDialog::onSetDefaultProfile);

    // 图片来源
    connect(ui->realTimeRadio, &QRadioButton::toggled, this, &ParameterSettingsDialog::onImageSourceChanged);
    connect(ui->reloadImageBtn, &QPushButton::clicked, this, &ParameterSettingsDialog::onReloadImage);

    // ROI变化
    connect(ui->imageDisplay, &ImageDisplayWidget::roiChanged, this, &ParameterSettingsDialog::onROIChanged);

    // 保存和取消
    connect(ui->saveBtn, &QPushButton::clicked, this, &ParameterSettingsDialog::onSave);
    connect(ui->cancelBtn, &QPushButton::clicked, this, &ParameterSettingsDialog::onCancel);
}

void ParameterSettingsDialog::loadCameraList() {
    // 相机列表已在UI中定义，这里设置itemData确保正确的cameraId
    // index 0 -> cameraId 1
    // index 1 -> cameraId 2
    // index 2 -> cameraId 3
    for (int i = 0; i < ui->cameraCombo->count(); ++i) {
        ui->cameraCombo->setItemData(i, i + 1);
    }
}

// 确保参考图片目录存在
void ParameterSettingsDialog::ensureReferenceImagesDirectoryExists() {
    QString dirPath = "config/reference_images";
    QDir dir;

    if (!dir.exists(dirPath)) {
        if (dir.mkpath(dirPath)) {
            Logger::instance().info("Created reference images directory: " + dirPath.toStdString());

            // 创建说明文件
            createReadmeFile();
        } else {
            Logger::instance().error("Failed to create reference images directory: " + dirPath.toStdString());
        }
    }
}

// 创建说明文件
void ParameterSettingsDialog::createReadmeFile() {
    QString readmePath = "config/reference_images/README.txt";
    QFile file(readmePath);

    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out.setAutoDetectUnicode(true);

        out << "==================================================\n";
        out << "相机参考图片使用说明\n";
        out << "==================================================\n\n";

        out << "【功能说明】\n";
        out << "此目录用于存放每个相机的参考图片，用于ROI绘制。\n";
        out << "参考图片用于在参数设置中显示和绘制ROI框。\n\n";

        out << "【命名规则】\n";
        out << "文件名格式：camera{相机ID}.jpg\n\n";
        out << "示例：\n";
        out << "  - camera1.jpg  (相机1的参考图片)\n";
        out << "  - camera2.jpg  (相机2的参考图片)\n";
        out << "  - camera3.jpg  (相机3的参考图片)\n\n";

        out << "【如何添加参考图片】\n";
        out << "方法1：通过参数设置界面\n";
        out << "  1. 打开参数设置界面\n";
        out << "  2. 选择对应的相机\n";
        out << "  3. 点击\"从实时相机更新\"按钮\n";
        out << "  4. 系统会自动保存当前画面为参考图片\n\n";

        out << "方法2：手动添加\n";
        out << "  1. 准备好对应的图片文件（.jpg格式）\n";
        out << "  2. 将文件重命名为：camera1.jpg, camera2.jpg, camera3.jpg\n";
        out << "  3. 复制到此目录（config/reference_images/）\n\n";

        out << "【图片要求】\n";
        out << "  - 格式：JPG（.jpg）\n";
        out << "  - 建议分辨率：与相机实际分辨率一致\n";
        out << "  - 文件大小：建议不超过5MB\n\n";

        out << "【注意事项】\n";
        out << "  1. 每个相机只能有1张参考图片\n";
        out << "  2. 新图片会覆盖旧图片\n";
        out << "  3. 参考图片与用户集无关，是相机级别的配置\n";
        out << "  4. 参考图片损坏或丢失不影响程序运行\n\n";

        out << "【常见问题】\n";
        out << "Q: 为什么看不到参考图片？\n";
        out << "A: 请检查文件名是否正确（camera1.jpg等）\n";
        out << "   确保图片格式为JPG，且文件在此目录下\n\n";

        out << "Q: ROI参数保存在哪里？\n";
        out << "A: ROI参数保存在用户集配置中，与参考图片分离\n";
        out << "   每个相机可以有多个用户集，每个用户集有独立的ROI参数\n\n";

        out << "Q: 如何切换使用参考图片或实时相机？\n";
        out << "A: 在参数设置界面中，选择\"历史图片\"或\"实时相机采图\"\n\n";

        out << "==================================================\n";
        out << "更新日期：2026-03-21\n";
        out << "==================================================\n";

        file.close();
        Logger::instance().info("Created reference images README: " + readmePath.toStdString());
    }
}

void ParameterSettingsDialog::onCameraChanged(int index) {
    if (index < 0) return;

    m_currentCameraId = ui->cameraCombo->itemData(index).toInt();
    updateAlgorithmTypeDisplay(m_currentCameraId);
    loadProfileList(m_currentCameraId);
}

void ParameterSettingsDialog::updateAlgorithmTypeDisplay(int cameraId) {
    const CameraConfig& config = ConfigManager::instance().getCameraConfig(cameraId);

    QString displayType;
    bool enableUserSet = config.useTraditionalAlgo;  // 只要启用传统算法就启用用户集

    // 判断是否为管理员
    bool isAdmin = (m_currentRole == "admin");

    if (config.useTraditionalAlgo && config.useAIAlgo) {
        displayType = "Traditional + AI (传统算法 + AI)";
        // 只有管理员可以编辑ROI
        ui->imageDisplay->setROIMode(isAdmin);
        ui->hintLabel->setText(
            "• 拖拽鼠标绘制ROI框\n"
            "• 传统算法需要设置ROI参数\n"
            "• 同时启用了AI算法检测\n"
            "• 保存后立即生效"
        );
    } else if (config.useTraditionalAlgo) {
        displayType = "Traditional (传统算法)";
        // 只有管理员可以编辑ROI
        ui->imageDisplay->setROIMode(isAdmin);
        ui->hintLabel->setText(
            "• 拖拽鼠标绘制ROI框\n"
            "• 传统算法需要设置ROI参数\n"
            "• 保存后立即生效"
        );
    } else if (config.useAIAlgo) {
        displayType = "AI";
        ui->imageDisplay->setROIMode(false);
        ui->hintLabel->setText(
            "• AI算法使用YOLO模型自动检测\n"
            "• 无需手动配置ROI参数\n"
            "• 模型自动识别缺陷"
        );
    } else {
        displayType = "Unknown (未配置算法)";
        ui->imageDisplay->setROIMode(false);
    }

    // 根据enableUserSet设置用户集UI状态
    ui->profileCombo->setEnabled(enableUserSet);
    ui->setDefaultBtn->setEnabled(enableUserSet);

    // 新建、重命名、删除按钮：只有管理员且启用传统算法时才可用
    ui->newProfileBtn->setEnabled(enableUserSet && isAdmin);
    ui->renameProfileBtn->setEnabled(enableUserSet && isAdmin);
    ui->deleteProfileBtn->setEnabled(enableUserSet && isAdmin);

    if (enableUserSet) {
        loadProfileList(cameraId);
    } else {
        ui->profileCombo->clear();
        ui->profileCombo->addItem("AI算法无需用户集配置");
    }

    ui->algorithmTypeLabel->setText(displayType);
}

void ParameterSettingsDialog::loadProfileList(int cameraId) {
    ui->profileCombo->clear();

    std::vector<std::string> profiles = AlgorithmROIManager::instance().getUserProfiles(cameraId);
    for (const auto& profileName : profiles) {
        ui->profileCombo->addItem(QString::fromStdString(profileName));
    }

    if (ui->profileCombo->count() > 0) {
        // 获取默认用户集并选中它
        std::string defaultProfile = AlgorithmROIManager::instance().getDefaultProfile(cameraId);
        int defaultIndex = 0;
        if (!defaultProfile.empty()) {
            for (int i = 0; i < ui->profileCombo->count(); ++i) {
                if (ui->profileCombo->itemText(i).toStdString() == defaultProfile) {
                    defaultIndex = i;
                    break;
                }
            }
        }
        ui->profileCombo->setCurrentIndex(defaultIndex);
        onProfileChanged(defaultIndex);
    } else {
        m_currentProfile = UserProfile();
        ui->imageDisplay->setImage(cv::Mat());
        ui->imageDisplay->setROI(AlgorithmROIConfig());
        updateROICoordinatesDisplay();
    }
}

void ParameterSettingsDialog::onProfileChanged(int index) {
    if (index < 0) return;

    std::string profileName = ui->profileCombo->currentText().toStdString();
    m_currentProfileName = profileName;
    m_currentProfile = AlgorithmROIManager::instance().getUserProfile(m_currentCameraId, profileName);

    // 更新ROI显示
    ui->imageDisplay->setROI(m_currentProfile.roi);
    updateROICoordinatesDisplay();

    // 加载示例图片
    loadImageForCurrentProfile();
}

void ParameterSettingsDialog::updateROICoordinatesDisplay() {
    if (m_currentProfile.roi.enabled) {
        ui->row1Edit->setText(QString::number(m_currentProfile.roi.row1, 'f', 3));
        ui->column1Edit->setText(QString::number(m_currentProfile.roi.column1, 'f', 3));
        ui->row2Edit->setText(QString::number(m_currentProfile.roi.row2, 'f', 3));
        ui->column2Edit->setText(QString::number(m_currentProfile.roi.column2, 'f', 3));
    } else {
        ui->row1Edit->clear();
        ui->column1Edit->clear();
        ui->row2Edit->clear();
        ui->column2Edit->clear();
    }

    // 更新阈值显示（使用2位小数精度）
    ui->thresholdMinEdit->setText(QString::number(m_currentProfile.thresholdMin, 'f', 2));
    ui->thresholdMaxEdit->setText(QString::number(m_currentProfile.thresholdMax, 'f', 2));
}

void ParameterSettingsDialog::loadImageForCurrentProfile() {
    if (m_currentProfileName.empty()) return;

    cv::Mat image;

    // 检查图片来源选择
    if (ui->realTimeRadio->isChecked()) {
        // 实时相机采图模式
        MetalStripVision* mainWindow = qobject_cast<MetalStripVision*>(parent());
        if (mainWindow) {
            image = mainWindow->getCurrentCameraImage(m_currentCameraId);
            if (image.empty()) {
                Logger::instance().warn("Failed to get real-time image from camera " + std::to_string(m_currentCameraId));
            } else {
                Logger::instance().debug("Loaded real-time image from camera " + std::to_string(m_currentCameraId) +
                    ", size: " + std::to_string(image.cols) + "x" + std::to_string(image.rows));
            }
        } else {
            Logger::instance().error("Cannot cast parent to MetalStripVision");
        }
    } else {
        // 历史图片模式
        image = AlgorithmROIManager::instance().loadProfileImage(
            m_currentCameraId, m_currentProfileName);
    }

    if (!image.empty()) {
        ui->imageDisplay->setImage(image);
    } else {
        ui->imageDisplay->setImage(cv::Mat());
    }
}

void ParameterSettingsDialog::onROIChanged(const AlgorithmROIConfig& roi) {
    m_currentProfile.roi = roi;
    updateROICoordinatesDisplay();
    m_modified = true;
}

void ParameterSettingsDialog::onNewProfile() {
    bool ok;
    QString name = QInputDialog::getText(this, "新建用户集",
                                         "请输入用户集名称:",
                                         QLineEdit::Normal, "", &ok);
    if (ok && !name.isEmpty()) {
        std::string nameStr = name.toStdString();

        if (AlgorithmROIManager::instance().addUserProfile(m_currentCameraId, nameStr)) {
            loadProfileList(m_currentCameraId);
            ui->profileCombo->setCurrentText(name);
            Logger::instance().info("Created new profile: " + nameStr);
        } else {
            QMessageBox::warning(this, "错误", "创建用户集失败，可能是名称已存在或数量已达上限");
        }
    }
}

void ParameterSettingsDialog::onRenameProfile() {
    if (m_currentProfileName.empty()) {
        QMessageBox::warning(this, "提示", "请先选择要重命名的用户集");
        return;
    }

    bool ok;
    QString newName = QInputDialog::getText(this, "重命名用户集",
                                            "请输入新的名称:",
                                            QLineEdit::Normal,
                                            QString::fromStdString(m_currentProfileName),
                                            &ok);
    if (ok && !newName.isEmpty()) {
        std::string newNameStr = newName.toStdString();

        if (AlgorithmROIManager::instance().renameUserProfile(
                m_currentCameraId, m_currentProfileName, newNameStr)) {
            m_currentProfileName = newNameStr;
            loadProfileList(m_currentCameraId);
            ui->profileCombo->setCurrentText(newName);
            Logger::instance().info("Renamed profile to: " + newNameStr);
        } else {
            QMessageBox::warning(this, "错误", "重命名失败，可能是名称已存在");
        }
    }
}

void ParameterSettingsDialog::onDeleteProfile() {
    if (m_currentProfileName.empty()) {
        QMessageBox::warning(this, "提示", "请先选择要删除的用户集");
        return;
    }

    auto reply = QMessageBox::question(this, "确认删除",
                                       QString("确定要删除用户集 \"%1\" 吗？")
                                       .arg(QString::fromStdString(m_currentProfileName)),
                                       QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        if (AlgorithmROIManager::instance().deleteUserProfile(
                m_currentCameraId, m_currentProfileName)) {
            loadProfileList(m_currentCameraId);
            Logger::instance().info("Deleted profile: " + m_currentProfileName);
        } else {
            QMessageBox::warning(this, "错误", "删除失败，可能是最后一个用户集");
        }
    }
}

void ParameterSettingsDialog::onSetDefaultProfile() {
    if (m_currentProfileName.empty()) {
        QMessageBox::warning(this, "提示", "请先选择用户集");
        return;
    }

    // 检查主窗口是否正在检测
    MetalStripVision* mainWindow = qobject_cast<MetalStripVision*>(parent());
    if (mainWindow && mainWindow->isDetecting()) {
        QMessageBox::warning(this, "操作受限",
            "检测运行中无法切换默认用户集（料号）！\n\n"
            "请先停止检测，然后再设置默认用户集。");
        return;
    }

    if (AlgorithmROIManager::instance().setDefaultProfile(
            m_currentCameraId, m_currentProfileName)) {
        m_modified = true;  // 标记为已修改，需要保存
        QMessageBox::information(this, "成功",
            "已设置为默认用户集\n\n"
            "注意：这是料号切换操作。\n"
            "请点击保存按钮使配置生效。");
        Logger::instance().info("Set default profile: " + m_currentProfileName);
    } else {
        QMessageBox::warning(this, "错误", "设置失败");
    }
}

void ParameterSettingsDialog::onImageSourceChanged() {
    loadImageForCurrentProfile();
}

void ParameterSettingsDialog::onReloadImage() {
    loadImageForCurrentProfile();
}

void ParameterSettingsDialog::onSave() {
    if (!validateInputs()) {
        return;
    }

    saveCurrentProfile();

    // 保存到文件
    if (AlgorithmROIManager::instance().saveConfig()) {
        QMessageBox::information(this, "成功",
            "配置已保存。\n\n"
            "注意：修改的ROI和阈值参数需要重新开始检测后才会生效。\n"
            "如果当前正在检测中，请先暂停检测，再重新开始。");
        Logger::instance().info("Algorithm ROI config saved successfully");
        accept();
    } else {
        QMessageBox::critical(this, "错误", "保存配置失败");
    }
}

void ParameterSettingsDialog::onCancel() {
    if (m_modified) {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle("确认退出");
        msgBox.setText("配置已修改，是否保存？");
        msgBox.setIcon(QMessageBox::Question);

        QPushButton* saveBtn = msgBox.addButton("保存", QMessageBox::AcceptRole);
        QPushButton* discardBtn = msgBox.addButton("不保存", QMessageBox::DestructiveRole);
        QPushButton* cancelBtn = msgBox.addButton("取消", QMessageBox::RejectRole);

        msgBox.exec();

        if (msgBox.clickedButton() == saveBtn) {
            onSave();
            return;
        } else if (msgBox.clickedButton() == cancelBtn) {
            return;
        }
        // 点击"不保存"则继续执行 reject()
    }
    reject();
}

void ParameterSettingsDialog::saveCurrentProfile() {
    // 保存阈值参数
    bool minOk = false, maxOk = false;
    double thresholdMin = ui->thresholdMinEdit->text().toDouble(&minOk);
    double thresholdMax = ui->thresholdMaxEdit->text().toDouble(&maxOk);

    if (minOk && maxOk) {
        m_currentProfile.thresholdMin = thresholdMin;
        m_currentProfile.thresholdMax = thresholdMax;
    }

    // 更新当前用户集配置
    if (!m_currentProfileName.empty()) {
        AlgorithmROIManager::instance().updateUserProfile(
            m_currentCameraId, m_currentProfileName, m_currentProfile);

        // 保存当前显示的图片
        cv::Mat currentImage = ui->imageDisplay->getImage();
        if (!currentImage.empty()) {
            AlgorithmROIManager::instance().saveProfileImage(
                m_currentCameraId, m_currentProfileName, currentImage);
        }
    }
}

bool ParameterSettingsDialog::validateInputs() {
    if (m_currentProfileName.empty()) {
        QMessageBox::warning(this, "错误", "请先选择用户集");
        return false;
    }

    if (m_currentProfile.roi.enabled) {
        if (!m_currentProfile.roi.isValid()) {
            QMessageBox::warning(this, "错误", "ROI坐标无效");
            return false;
        }
    }

    // 验证阈值输入
    bool minOk = false, maxOk = false;
    double thresholdMin = ui->thresholdMinEdit->text().toDouble(&minOk);
    double thresholdMax = ui->thresholdMaxEdit->text().toDouble(&maxOk);

    if (!minOk || !maxOk) {
        QMessageBox::warning(this, "错误", "阈值必须为有效数字");
        return false;
    }

    if (thresholdMin >= thresholdMax) {
        QMessageBox::warning(this, "错误", "阈值下限必须小于上限");
        return false;
    }

    return true;
}
