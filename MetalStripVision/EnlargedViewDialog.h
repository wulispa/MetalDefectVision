#pragma once

#include <QDialog>
#include <QLabel>
#include <opencv2/opencv.hpp>

class EnlargedViewDialog : public QDialog {
    Q_OBJECT

public:
    explicit EnlargedViewDialog(QWidget* parent = nullptr);
    ~EnlargedViewDialog() = default;

    // 更新显示的图像
    void updateImage(int cameraId, const cv::Mat& image);

    // 获取当前显示的相机ID
    int getCurrentCameraId() const { return m_currentCameraId; }

signals:
    void dialogClosed();

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QLabel* m_imageLabel;
    cv::Mat m_currentImage;  // 缓存当前图像，用于窗口大小变化时重新缩放
    int m_currentCameraId = 0;  // 0 = 未显示任何相机
};
