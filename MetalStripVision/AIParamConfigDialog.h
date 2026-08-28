#pragma once

#include <QDialog>
#include <QComboBox>
#include <QTableWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QCloseEvent>
#include <QMessageBox>
#include <unordered_map>
#include <vector>
#include <map>
#include "AIDefectThreshold.h"

// 前向声明
class YOLOAlgorithm;

class AIParamConfigDialog : public QDialog {
    Q_OBJECT

public:
    explicit AIParamConfigDialog(YOLOAlgorithm* aiAlgoUpper,
                                  YOLOAlgorithm* aiAlgoLower,
                                  double pixelsPerMm,
                                  QWidget* parent = nullptr);
    ~AIParamConfigDialog() override;

    // 获取指定相机的阈值配置
    std::vector<AIDefectThreshold> getThresholdsForCamera(int cameraId) const;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onCameraChanged(int index);
    void onSave();
    void onCancel();
    void onTableCellChanged(int row, int col);

private:
    void setupUI();
    void loadThresholdsFromConfig();
    void saveThresholdsToConfig();
    void populateTable(int cameraId);
    void collectTableData(int cameraId);
    void checkModified();

    // UI控件
    QComboBox* m_cameraCombo = nullptr;
    QTableWidget* m_tableWidget = nullptr;
    QPushButton* m_saveBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;

    // AI算法实例
    YOLOAlgorithm* m_aiAlgoUpper = nullptr;  // 相机2
    YOLOAlgorithm* m_aiAlgoLower = nullptr;  // 相机3

    // 像素到毫米的转换系数
    double m_pixelsPerMm;  // 每毫米对应的像素数

    // 数据存储：cameraId -> 阈值列表（内部以像素为单位存储）
    std::map<int, std::vector<AIDefectThreshold>> m_thresholds;

    // 原始数据（用于判断是否修改）
    std::map<int, std::vector<AIDefectThreshold>> m_originalThresholds;

    // 状态标志
    bool m_modified = false;
    bool m_populating = false;  // 防止populateTable时触发修改标记
};
