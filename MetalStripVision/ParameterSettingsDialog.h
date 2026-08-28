#pragma once

#include <QDialog>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QRadioButton>
#include <QLineEdit>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <opencv2/opencv.hpp>
#include "AlgorithmROIManager.h"
#include "ConfigManager.h"

// 自定义图片显示控件，支持ROI绘制
class ImageDisplayWidget : public QLabel {
    Q_OBJECT

public:
    explicit ImageDisplayWidget(QWidget* parent = nullptr);
    void setImage(const cv::Mat& image);
    cv::Mat getImage() const { return m_image; }
    void setROI(const AlgorithmROIConfig& roi);
    AlgorithmROIConfig getROI() const { return m_currentROI; }
    void setROIMode(bool enabled);

signals:
    void roiChanged(const AlgorithmROIConfig& roi);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    cv::Mat m_image;
    AlgorithmROIConfig m_currentROI;
    bool m_roiMode;
    bool m_drawing;
    QPoint m_startPoint;
    QPoint m_endPoint;

    cv::Point mapToImagePoint(const QPoint& widgetPoint) const;
    QPoint mapToWidgetPoint(const cv::Point& imagePoint) const;
};

namespace Ui {
class ParameterSettingsDialog;
}

// 参数设置对话框
class ParameterSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit ParameterSettingsDialog(const QString& role, QWidget* parent = nullptr);
    ~ParameterSettingsDialog();

private slots:
    void onCameraChanged(int index);
    void onProfileChanged(int index);
    void onNewProfile();
    void onRenameProfile();
    void onDeleteProfile();
    void onSetDefaultProfile();
    void onImageSourceChanged();
    void onReloadImage();
    void onROIChanged(const AlgorithmROIConfig& roi);
    void onSave();
    void onCancel();

private:
    void setupConnections();
    void loadCameraList();
    void ensureReferenceImagesDirectoryExists();
    void createReadmeFile();
    void loadProfileList(int cameraId);
    void updateAlgorithmTypeDisplay(int cameraId);
    void updateROICoordinatesDisplay();
    void loadImageForCurrentProfile();
    void saveCurrentProfile();
    bool validateInputs();

    Ui::ParameterSettingsDialog* ui;

    // 当前状态
    int m_currentCameraId;
    std::string m_currentProfileName;
    UserProfile m_currentProfile;
    bool m_modified;
    QString m_currentRole;  // 当前用户角色
};
