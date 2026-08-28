#pragma once

#include <QDialog>
#include "ConfigManager.h"
#include "AlgorithmROIManager.h"

namespace Ui {
class ConfigManagerDialog;
}

// 配置文件信息结构
struct ConfigFileInfo {
    QString name;           // 配置文件名称
    QString path;           // 配置文件路径
    QString description;    // 描述
};

class ConfigManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConfigManagerDialog(QWidget* parent = nullptr);
    ~ConfigManagerDialog();

private slots:
    void onFormatSelectedClicked();      // 格式化选中的配置
    void onFormatAllClicked();           // 格式化所有配置
    void onOpenConfigDirClicked();       // 打开配置目录
    void onRefreshClicked();             // 刷新状态
    void onCloseClicked();               // 关闭对话框

private:
    void refreshConfigStatus();           // 刷新配置状态
    QString getStatusText(ConfigManager::ConfigStatus status);  // 获取状态文本
    QString getROISTatusText(AlgorithmROIManager::ConfigStatus status);  // 获取ROI状态文本

    Ui::ConfigManagerDialog* ui;
    QList<ConfigFileInfo> m_configFiles; // 配置文件列表
};
