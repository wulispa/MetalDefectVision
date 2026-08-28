#pragma once

#include <QDialog>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QDateTime>
#include <QDir>
#include <opencv2/opencv.hpp>
#include "CameraHK1.h"
#include "MetalStripVision.h"

class ManualCaptureDialog : public QDialog {
    Q_OBJECT

public:
    explicit ManualCaptureDialog(CameraHK1* cam1, CameraHK1* cam2, CameraHK1* cam3,
                                  const QString& savePath, QWidget* parent = nullptr);

private slots:
    void onCaptureClicked();
    void onSaveClicked();

private:
    void setupUI();

    QComboBox* m_cameraCombo;
    QPushButton* m_captureBtn;
    QPushButton* m_saveBtn;
    QLabel* m_previewLabel;
    QLabel* m_statusLabel;
    CameraHK1* m_cameras[3];
    QString m_savePath;
    cv::Mat m_capturedImage;
    int m_capturedCameraIndex = -1;
};
