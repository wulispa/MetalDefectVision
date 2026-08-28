#pragma execution_character_set("utf-8")

#include "YOLOAlgorithm.h"
#include "Logger.h"
#include <QTextStream>
#include <QDomDocument>
#include <QDomElement>
#include <chrono>
#include <windows.h>

YOLOAlgorithm::YOLOAlgorithm() : m_initialized(false), model(nullptr) {
}

YOLOAlgorithm::~YOLOAlgorithm() {
    release();
}

bool YOLOAlgorithm::initialize(const AlgorithmConfig* config) {
    if (m_initialized) {
        Logger::instance().warn("YOLOAlgorithm already initialized");
        return true;
    }

    if (config) {
        try {
            const YOLOConfig* yoloConfig = dynamic_cast<const YOLOConfig*>(config);
            if (yoloConfig) {
                m_yoloConfig = *yoloConfig;
            }
        } catch (const std::exception& e) {
            Logger::instance().error("Invalid config type: " + std::string(e.what()));
            return false;
        }
    }

    Logger::instance().info("YOLOAlgorithm initialized (DLL-based implementation)");
    m_initialized = true;
    return true;
}

AlgorithmResult YOLOAlgorithm::process(const cv::Mat& image) {
    AlgorithmResult result;
    result.success = false;

    if (!m_initialized) {
        result.message = "Algorithm not initialized";
        Logger::instance().error("YOLOAlgorithm: " + result.message);
        return result;
    }

    if (!model) {
        result.message = "Model not loaded";
        Logger::instance().error("YOLOAlgorithm: " + result.message);
        return result;
    }

    if (image.empty()) {
        result.message = "Empty input image";
        Logger::instance().error("YOLOAlgorithm: " + result.message);
        return result;
    }

    // 检查图像尺寸是否与模型配置匹配，如果不匹配则重新创建模型
    int actualWidth = image.cols;
    int actualHeight = image.rows;
    if (actualWidth != m_yoloConfig.srcWidth || actualHeight != m_yoloConfig.srcHeight) {
        Logger::instance().warn("YOLOAlgorithm: Image size mismatch, recreating model...");

        m_yoloConfig.srcWidth = actualWidth;
        m_yoloConfig.srcHeight = actualHeight;

        if (!m_currentModelName.isEmpty()) {
            QString currentModel = m_currentModelName;
            m_currentModelName.clear();
            if (!loadModelByName(currentModel)) {
                result.message = "Failed to recreate model with new image size";
                Logger::instance().error("YOLOAlgorithm: " + result.message);
                return result;
            }
            Logger::instance().info("YOLOAlgorithm: Model recreated successfully with new size");
        }
    }

    // ===== 分段计时开始 =====
    using clock = std::chrono::high_resolution_clock;

    auto t_start = clock::now();

    // Step 1: 图像格式转换
    cv::Mat imageClone;
    if (image.channels() == 1) {
        cv::cvtColor(image, imageClone, cv::COLOR_GRAY2BGR);
    } else if (image.channels() == 4) {
        cv::cvtColor(image, imageClone, cv::COLOR_BGRA2BGR);
    } else {
        imageClone = image;  // 浅拷贝
    }

    auto t_convert = clock::now();

    try {
        // Step 2: DLL推理（核心）
        auto final_result = model->DetectImage(imageClone);

        auto t_dll = clock::now();

        // Step 3: 结果转换
        m_detections.clear();
        for (const auto& p : final_result) {
            int classId = p.first;
            for (const auto& box : p.second) {
                Detection det;
                det.classId = classId;
                det.confidence = 1.0f;
                det.bbox = box;
                m_detections.push_back(det);
            }
        }

        auto t_result = clock::now();

        result.hasDefect = !m_detections.empty();
        result.defectCount = static_cast<int>(m_detections.size());
        result.success = true;
        result.message = "YOLO detection completed";

        // 分段计时（用info级别确保输出）
        double convertMs = std::chrono::duration<double, std::milli>(t_convert - t_start).count();
        double dllMs = std::chrono::duration<double, std::milli>(t_dll - t_convert).count();
        double resultMs = std::chrono::duration<double, std::milli>(t_result - t_dll).count();
        Logger::instance().debug("[AI_PERF] convert=" + std::to_string((int)convertMs)
            + "ms dll=" + std::to_string((int)dllMs)
            + "ms result=" + std::to_string((int)resultMs) + "ms");

    } catch (const std::exception& e) {
        result.message = "Error: " + std::string(e.what());
        Logger::instance().error(result.message);
    } catch (...) {
        result.message = "DLL crash detected (unknown exception)";
        Logger::instance().error("YOLOAlgorithm: Unknown exception during detection");
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    result.processingTime = std::chrono::duration<double, std::milli>(
        endTime - t_start).count();

    return result;
}

bool YOLOAlgorithm::loadModel(const std::string& modelPath, const std::string& configPath) {
    // 设置Model根目录（包含Tag和Trt子目录）
    m_yoloConfig.basePath = QString::fromStdString(modelPath);
    Logger::instance().info("Model base path set to: " + modelPath);
    return true;
}

void YOLOAlgorithm::setInferenceParams(float confThreshold, float nmsThreshold) {
    m_yoloConfig.confThreshold = confThreshold;
    m_yoloConfig.iouThreshold = nmsThreshold;
}

std::vector<Detection> YOLOAlgorithm::getDetections() const {
    return m_detections;
}

void YOLOAlgorithm::setClassNames(const std::vector<std::string>& names) {
    // 将vector转换为map
    classNames.clear();
    for (size_t i = 0; i < names.size(); ++i) {
        classNames[static_cast<int>(i)] = QString::fromStdString(names[i]);
    }
}

bool YOLOAlgorithm::loadModelByName(const QString& modelName) {
    if (modelName.isEmpty()) {
        Logger::instance().error("Model name is empty");
        return false;
    }

    // 1. 构建TRT模型文件路径（支持按料号加载）
    QString trtPath, tagFilePath;

    if (!m_partNumber.isEmpty()) {
        // 新路径：Model/{料号}/Trt/{模型名}.trt
        trtPath = QString("%1/%2/Trt/%3.trt").arg(m_yoloConfig.basePath).arg(m_partNumber).arg(modelName);
        tagFilePath = QString("%1/%2/Tag/%3.txt").arg(m_yoloConfig.basePath).arg(m_partNumber).arg(modelName);
        Logger::instance().info("Loading model with part number: " + m_partNumber.toStdString());
    } else {
        // 兼容旧路径：Model/Trt/{模型名}.trt
        trtPath = QString("%1/Trt/%2.trt").arg(m_yoloConfig.basePath).arg(modelName);
        tagFilePath = QString("%1/Tag/%2.txt").arg(m_yoloConfig.basePath).arg(modelName);
    }

    // 检查TRT文件是否存在
    QFile trtFile(trtPath);
    if (!trtFile.exists()) {
        Logger::instance().error("TRT file not found: " + trtPath.toStdString());
        return false;
    }

    // 2. 读取标签文件
    QFile tagFile(tagFilePath);

    if (!tagFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        Logger::instance().error("Cannot open tag file: " + tagFilePath.toStdString());
        return false;
    }

    classNames.clear();
    QTextStream in(&tagFile);
    int id = 0;
    while (!in.atEnd()) {
        QString t = in.readLine().trimmed();
        if (!t.isEmpty()) {
            classNames[id++] = t;
        }
    }
    tagFile.close();

    if (classNames.empty()) {
        Logger::instance().error("No class names found in tag file");
        return false;
    }

    // 3. 释放旧模型
    if (model) {
        delete model;
        model = nullptr;
    }

    // 4. 创建新模型
    model = CreateAIModel(
        m_yoloConfig.srcHeight,              // 源图片高度（相机图片尺寸）
        m_yoloConfig.srcWidth,               // 源图片宽度（相机图片尺寸）
        trtPath.toStdString().c_str(),
        classNames.size(),
        m_yoloConfig.confThreshold,
        m_yoloConfig.iouThreshold,
        m_yoloConfig.inputHeight,            // 目标高度（模型训练尺寸）
        m_yoloConfig.inputWidth              // 目标宽度（模型训练尺寸）
    );

    if (model == nullptr) {
        Logger::instance().error("Failed to create AI model for: " + modelName.toStdString());
        return false;
    }

    m_currentModelName = modelName;
    Logger::instance().info("Model loaded successfully: " + modelName.toStdString() +
                           " (Classes: " + std::to_string(classNames.size()) + ")");

    // 输出类别信息
    for (const auto& pair : classNames) {
        Logger::instance().debug("  Class " + std::to_string(pair.first) + ": " + pair.second.toStdString());
    }

    return true;
}

void YOLOAlgorithm::setPartNumber(const QString& partNumber) {
    if (m_partNumber != partNumber) {
        m_partNumber = partNumber;
        Logger::instance().info("Part number set to: " + partNumber.toStdString());
    }
}

bool YOLOAlgorithm::checkModelExists(const QString& modelName) const {
    QString trtPath, tagFilePath;

    if (!m_partNumber.isEmpty()) {
        trtPath = QString("%1/%2/Trt/%3.trt").arg(m_yoloConfig.basePath).arg(m_partNumber).arg(modelName);
        tagFilePath = QString("%1/%2/Tag/%3.txt").arg(m_yoloConfig.basePath).arg(m_partNumber).arg(modelName);
    } else {
        trtPath = QString("%1/Trt/%2.trt").arg(m_yoloConfig.basePath).arg(modelName);
        tagFilePath = QString("%1/Tag/%2.txt").arg(m_yoloConfig.basePath).arg(modelName);
    }

    return QFile::exists(trtPath) && QFile::exists(tagFilePath);
}

QString YOLOAlgorithm::getClassName(int classId) const {
    auto it = classNames.find(classId);
    if (it != classNames.end()) {
        return it->second;
    }
    return "Unknown";
}

cv::Mat YOLOAlgorithm::preprocess(const cv::Mat& image) {
    // DLL接口内部处理预处理，这里直接返回原图
    return image;
}

std::vector<Detection> YOLOAlgorithm::postprocess(const void* rawOutput, int imgWidth, int imgHeight) {
    // DLL接口已经返回后处理结果，这里返回空
    return std::vector<Detection>();
}

void YOLOAlgorithm::release() {
    if (model) {
        delete model;
        model = nullptr;
    }
    m_initialized = false;
    classNames.clear();
    classColors.clear();
    m_detections.clear();
    Logger::instance().info("YOLOAlgorithm released");
}

bool YOLOAlgorithm::loadColorConfig(const QString& configPath) {
    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        Logger::instance().warn("Cannot open color config file: " + configPath.toStdString() + ", using default colors");
        // 使用默认颜色
        classColors.clear();
        classColors[0] = cv::Scalar(0, 0, 255);    // 红色 (BGR)
        classColors[1] = cv::Scalar(0, 255, 0);    // 绿色
        classColors[2] = cv::Scalar(255, 0, 0);    // 蓝色
        classColors[3] = cv::Scalar(0, 255, 255);  // 黄色
        classColors[4] = cv::Scalar(255, 0, 255);  // 品红
        classColors[5] = cv::Scalar(255, 255, 0);  // 青色
        classColors[6] = cv::Scalar(0, 165, 255);  // 橙色
        return false;
    }

    QDomDocument doc;

    auto result = doc.setContent(&file);

    if (!result) {
        Logger::instance().error(
            std::string("Failed to parse color config XML: ") +
            result.errorMessage.toStdString() +
            " at line " + std::to_string(result.errorLine) +
            ", column " + std::to_string(result.errorColumn)
        );
        file.close();
        return false;
    }

    file.close();


    // 清除现有颜色配置
    classColors.clear();

    // 解析颜色配置
    QDomElement root = doc.documentElement();
    QDomNodeList colorNodes = root.elementsByTagName("Color");

    for (int i = 0; i < colorNodes.size(); ++i) {
        QDomElement colorElem = colorNodes.at(i).toElement();
        int classId = colorElem.attribute("classId", "-1").toInt();

        if (classId < 0) {
            Logger::instance().warn("Invalid classId in color config, skipping");
            continue;
        }

        int r = colorElem.attribute("r", "255").toInt();
        int g = colorElem.attribute("g", "0").toInt();
        int b = colorElem.attribute("b", "0").toInt();
        QString name = colorElem.attribute("name", "");

        // OpenCV使用 BGR 顺序
        cv::Scalar color(b, g, r);
        classColors[classId] = color;

        Logger::instance().debug("Loaded color config: classId=" + std::to_string(classId) +
                               ", name=" + name.toStdString() +
                               ", RGB=(" + std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) + ")");
    }

    if (classColors.empty()) {
        Logger::instance().warn("No valid color configurations found, using default colors");
        // 使用默认颜色
        classColors[0] = cv::Scalar(0, 0, 255);    // 红色
        classColors[1] = cv::Scalar(0, 255, 0);    // 绿色
        classColors[2] = cv::Scalar(255, 0, 0);    // 蓝色
        return false;
    }

    Logger::instance().debug("Color config loaded successfully: " + std::to_string(classColors.size()) + " colors");
    return true;
}

cv::Scalar YOLOAlgorithm::getClassColor(int classId) const {
    auto it = classColors.find(classId);
    if (it != classColors.end()) {
        return it->second;
    }

    // 如果未找到配置，返回默认颜色（白色）
    static const cv::Scalar defaultColor(255, 255, 255);
    return defaultColor;
}

void YOLOAlgorithm::setSrcImageSize(int width, int height) {
    m_yoloConfig.srcWidth = width;
    m_yoloConfig.srcHeight = height;
    Logger::instance().debug("Source image size set to: " + std::to_string(width) + "x" + std::to_string(height));

    // 如果模型已经加载，需要重新创建模型以应用新的尺寸
    if (model != nullptr && !m_currentModelName.isEmpty()) {
        Logger::instance().debug("Recreating model with new source image size...");

        // 保存当前模型信息
        QString currentModel = m_currentModelName;

        // 释放旧模型
        delete model;
        model = nullptr;

        // 重新加载模型（会使用新的 srcWidth/srcHeight）
        loadModelByName(currentModel);
    }
}
