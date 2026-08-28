#pragma execution_character_set("utf-8")

#include "ConfigManagerDialog.h"
#include "ui_ConfigManagerDialog.h"
#include "Logger.h"
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QHeaderView>

ConfigManagerDialog::ConfigManagerDialog(QWidget* parent)
    : QDialog(parent), ui(new Ui::ConfigManagerDialog)
{
    ui->setupUi(this);

    // 设置对话框样式
    setStyleSheet(
        "QDialog {"
        "  background-color: #1e1e2e;"
        "}"
    );

    // 设置表格列宽模式
    ui->configTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);       // 配置名称：固定
    ui->configTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);       // 状态：固定
    ui->configTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);     // 路径：自动拉伸填充
    ui->configTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive); // 描述：可手动调整

    // 设置固定列的宽度
    ui->configTable->setColumnWidth(0, 120);  // 配置名称
    ui->configTable->setColumnWidth(1, 80);   // 状态
    ui->configTable->setColumnWidth(3, 250);  // 描述（初始宽度）

    // 初始化配置文件列表
    m_configFiles.append({"系统配置", "config/config.xml", "相机、图片保存、测试模式等配置"});
    m_configFiles.append({"ROI配置", "config/algorithm_roi_profiles.xml", "算法ROI用户集配置"});
    m_configFiles.append({"颜色配置", "config/defect_colors.xml", "缺陷类型显示颜色配置"});

    // 刷新状态
    refreshConfigStatus();

    // 连接信号槽
    // connect(ui->formatSelectedBtn, &QPushButton::clicked, this, &ConfigManagerDialog::onFormatSelectedClicked);  // 注释：格式化选中配置按钮
    // connect(ui->formatAllBtn, &QPushButton::clicked, this, &ConfigManagerDialog::onFormatAllClicked);  // 注释：格式化所有配置按钮
    connect(ui->openDirBtn, &QPushButton::clicked, this, &ConfigManagerDialog::onOpenConfigDirClicked);
    connect(ui->refreshBtn, &QPushButton::clicked, this, &ConfigManagerDialog::onRefreshClicked);
    connect(ui->closeBtn, &QPushButton::clicked, this, &ConfigManagerDialog::onCloseClicked);
}

ConfigManagerDialog::~ConfigManagerDialog()
{
    delete ui;
}

void ConfigManagerDialog::refreshConfigStatus()
{
    ui->configTable->setRowCount(m_configFiles.size());

    for (int i = 0; i < m_configFiles.size(); ++i) {
        const auto& configInfo = m_configFiles[i];

        // 配置名称
        QTableWidgetItem* nameItem = new QTableWidgetItem(configInfo.name);
        nameItem->setTextAlignment(Qt::AlignCenter);
        ui->configTable->setItem(i, 0, nameItem);

        // 状态
        QString statusText;
        if (configInfo.path.contains("algorithm_roi")) {
            auto status = AlgorithmROIManager::checkConfigStatus(configInfo.path.toStdString());
            statusText = getROISTatusText(status);
        } else {
            auto status = ConfigManager::checkConfigStatus(configInfo.path.toStdString());
            statusText = getStatusText(status);
        }

        QTableWidgetItem* statusItem = new QTableWidgetItem(statusText);
        statusItem->setTextAlignment(Qt::AlignCenter);

        // 根据状态设置颜色
        if (statusText.contains("正常")) {
            statusItem->setForeground(QColor("#a6e3a1"));  // 绿色
        } else if (statusText.contains("缺失")) {
            statusItem->setForeground(QColor("#f38ba8"));  // 红色
        } else if (statusText.contains("损坏")) {
            statusItem->setForeground(QColor("#fab387"));  // 橙色
        }

        ui->configTable->setItem(i, 1, statusItem);

        // 路径
        QTableWidgetItem* pathItem = new QTableWidgetItem(configInfo.path);
        ui->configTable->setItem(i, 2, pathItem);

        // 描述
        QTableWidgetItem* descItem = new QTableWidgetItem(configInfo.description);
        ui->configTable->setItem(i, 3, descItem);
    }
}

QString ConfigManagerDialog::getStatusText(ConfigManager::ConfigStatus status)
{
    switch (status) {
        case ConfigManager::ConfigStatus::OK:
            return "✓ 正常";
        case ConfigManager::ConfigStatus::Missing:
            return "✗ 缺失";
        case ConfigManager::ConfigStatus::Corrupted:
            return "✗ 损坏";
        default:
            return "? 未知";
    }
}

QString ConfigManagerDialog::getROISTatusText(AlgorithmROIManager::ConfigStatus status)
{
    switch (status) {
        case AlgorithmROIManager::ConfigStatus::OK:
            return "✓ 正常";
        case AlgorithmROIManager::ConfigStatus::Missing:
            return "✗ 缺失";
        case AlgorithmROIManager::ConfigStatus::Corrupted:
            return "✗ 损坏";
        default:
            return "? 未知";
    }
}

void ConfigManagerDialog::onFormatSelectedClicked()
{
    int currentRow = ui->configTable->currentRow();
    if (currentRow < 0 || currentRow >= m_configFiles.size()) {
        QMessageBox::warning(this, "提示", "请先选择要格式化的配置文件");
        return;
    }

    const auto& configInfo = m_configFiles[currentRow];

    // 确认对话框
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "确认格式化",
        QString("确定要格式化 %1 吗？\n\n"
                "此操作将从 config_template/ 文件夹复制默认配置，\n"
                "当前的配置将被覆盖。").arg(configInfo.name),
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply != QMessageBox::Yes) {
        return;
    }

    // 执行格式化
    bool success = false;
    if (configInfo.path.contains("algorithm_roi")) {
        success = AlgorithmROIManager::instance().resetToDefault();
    } else if (configInfo.path.contains("defect_colors")) {
        // 格式化颜色配置：直接从模板复制
        const std::string templateFile = "config_template/defect_colors.xml";
        const std::string targetFile = "config/defect_colors.xml";

        // 检查模板文件是否存在
        if (!QFile::exists(QString::fromStdString(templateFile))) {
            QMessageBox::critical(this, "错误", "模板文件不存在: " + QString::fromStdString(templateFile));
            return;
        }

        // 确保config目录存在
        QString targetDir = QString::fromStdString("config");
        if (!QDir().exists(targetDir)) {
            if (!QDir().mkpath(targetDir)) {
                QMessageBox::critical(this, "错误", "无法创建config目录");
                return;
            }
        }

        // 如果目标文件已存在，先删除
        if (QFile::exists(QString::fromStdString(targetFile))) {
            if (!QFile::remove(QString::fromStdString(targetFile))) {
                QMessageBox::critical(this, "错误", "无法删除旧配置文件");
                return;
            }
        }

        // 从模板复制文件
        success = QFile::copy(QString::fromStdString(templateFile),
                            QString::fromStdString(targetFile));

        if (success) {
            Logger::instance().info("Color config reset successfully from template");
        } else {
            Logger::instance().error("Failed to copy template color config");
        }
    } else {
        success = ConfigManager::instance().resetToDefault();
    }

    if (success) {
        QMessageBox::information(this, "成功", QString("%1 已成功格式化").arg(configInfo.name));
        refreshConfigStatus();
    } else {
        QMessageBox::critical(this, "失败", QString("格式化 %1 失败，请查看日志").arg(configInfo.name));
    }
}

void ConfigManagerDialog::onFormatAllClicked()
{
    // 确认对话框
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "确认格式化所有配置",
        "确定要格式化所有配置文件吗？\n\n"
        "此操作将从 config_template/ 文件夹复制所有默认配置，\n"
        "当前的所有配置将被覆盖。",
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply != QMessageBox::Yes) {
        return;
    }

    // 执行格式化所有配置
    bool allSuccess = true;
    QString failedConfigs;

    for (const auto& configInfo : m_configFiles) {
        bool success = false;
        if (configInfo.path.contains("algorithm_roi")) {
            success = AlgorithmROIManager::instance().resetToDefault();
        } else if (configInfo.path.contains("defect_colors")) {
            // 格式化颜色配置
            const std::string templateFile = "config_template/defect_colors.xml";
            const std::string targetFile = "config/defect_colors.xml";

            if (QFile::exists(QString::fromStdString(templateFile))) {
                // 如果目标文件已存在，先删除
                if (QFile::exists(QString::fromStdString(targetFile))) {
                    QFile::remove(QString::fromStdString(targetFile));
                }
                success = QFile::copy(QString::fromStdString(templateFile),
                                    QString::fromStdString(targetFile));
            }
        } else {
            success = ConfigManager::instance().resetToDefault();
        }

        if (!success) {
            allSuccess = false;
            failedConfigs += configInfo.name + "\n";
        }
    }

    if (allSuccess) {
        QMessageBox::information(this, "成功", "所有配置文件已成功格式化");
        refreshConfigStatus();
    } else {
        QMessageBox::warning(
            this,
            "部分失败",
            "以下配置文件格式化失败：\n" + failedConfigs + "\n请查看日志获取详细信息"
        );
        refreshConfigStatus();
    }
}

void ConfigManagerDialog::onOpenConfigDirClicked()
{
    QString configPath = QFileInfo("config/config.xml").absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(configPath));
}

void ConfigManagerDialog::onRefreshClicked()
{
    refreshConfigStatus();
    Logger::instance().info("Config status refreshed");
}

void ConfigManagerDialog::onCloseClicked()
{
    accept();
}
