#include "EnlargedViewDialog.h"
#include <QVBoxLayout>
#include <QCloseEvent>
#include <QResizeEvent>

EnlargedViewDialog::EnlargedViewDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QString::fromUtf8("相机放大视图"));
    setFixedSize(1000, 750);
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint);

    m_imageLabel = new QLabel(this);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setStyleSheet("background-color: #1a1a2e; color: #6c7086; font-size: 16px; font-weight: bold;");
    m_imageLabel->setScaledContents(false);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_imageLabel);

    // 初始显示等待文字
    m_imageLabel->setText(QString::fromUtf8("等待图像..."));
}

void EnlargedViewDialog::updateImage(int cameraId, const cv::Mat& image)
{
    m_currentCameraId = cameraId;
    setWindowTitle(QString::fromUtf8("相机 %1 放大视图").arg(cameraId));

    if (image.empty()) {
        m_imageLabel->setText(QString::fromUtf8("等待相机 %1 图像...").arg(cameraId));
        m_currentImage.release();
        return;
    }

    // 缓存当前图像
    m_currentImage = image.clone();

    // 转换并显示
    QImage qimg;
    if (image.channels() == 3) {
        qimg = QImage(image.data, image.cols, image.rows, image.step, QImage::Format_BGR888).copy();
    } else if (image.channels() == 1) {
        // 灰度图需要先转换为BGR
        cv::Mat bgr;
        cv::cvtColor(image, bgr, cv::COLOR_GRAY2BGR);
        qimg = QImage(bgr.data, bgr.cols, bgr.rows, bgr.step, QImage::Format_BGR888).copy();
    } else if (image.channels() == 4) {
        // RGBA转BGR
        cv::Mat bgr;
        cv::cvtColor(image, bgr, cv::COLOR_RGBA2BGR);
        qimg = QImage(bgr.data, bgr.cols, bgr.rows, bgr.step, QImage::Format_BGR888).copy();
    } else {
        // 其他格式，先转RGB再转BGR
        cv::Mat bgr;
        cv::cvtColor(image, bgr, cv::COLOR_RGB2BGR);
        qimg = QImage(bgr.data, bgr.cols, bgr.rows, bgr.step, QImage::Format_BGR888).copy();
    }

    QPixmap pixmap = QPixmap::fromImage(qimg);
    QPixmap scaled = pixmap.scaled(m_imageLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_imageLabel->setPixmap(scaled);
}

void EnlargedViewDialog::closeEvent(QCloseEvent* event)
{
    emit dialogClosed();
    QDialog::closeEvent(event);
}

void EnlargedViewDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);

    // 窗口大小变化时重新缩放图像
    // 如果图像为空，不处理
    if (m_currentImage.empty()){
        return;
    }

    QImage qimg;
    if (m_currentImage.channels() == 3) {
        qimg = QImage(m_currentImage.data, m_currentImage.cols, m_currentImage.rows,
                      m_currentImage.step, QImage::Format_BGR888).copy();
    } else if (m_currentImage.channels() == 1) {
        cv::Mat bgr;
        cv::cvtColor(m_currentImage, bgr, cv::COLOR_GRAY2BGR);
        qimg = QImage(bgr.data, bgr.cols, bgr.rows, bgr.step, QImage::Format_BGR888).copy();
    } else if (m_currentImage.channels() == 4) {
        cv::Mat bgr;
        cv::cvtColor(m_currentImage, bgr, cv::COLOR_RGBA2BGR);
        qimg = QImage(bgr.data, bgr.cols, bgr.rows, bgr.step, QImage::Format_BGR888).copy();
    } else {
        cv::Mat bgr;
        cv::cvtColor(m_currentImage, bgr, cv::COLOR_RGB2BGR);
        qimg = QImage(bgr.data, bgr.cols, bgr.rows, bgr.step, QImage::Format_BGR888).copy();
    }

    QPixmap pixmap = QPixmap::fromImage(qimg);
    QPixmap scaled = pixmap.scaled(m_imageLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_imageLabel->setPixmap(scaled);
}
