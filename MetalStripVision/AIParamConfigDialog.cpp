#pragma execution_character_set("utf-8")

#include "AIParamConfigDialog.h"
#include "YOLOAlgorithm.h"
#include "AlgorithmROIManager.h"
#include "Logger.h"
#include <QGroupBox>
#include <QScrollArea>
#include <QSplitter>
#include <fstream>

AIParamConfigDialog::AIParamConfigDialog(YOLOAlgorithm* aiAlgoUpper,
                                           YOLOAlgorithm* aiAlgoLower,
                                           double pixelsPerMm,
                                           QWidget* parent)
    : QDialog(parent)
    , m_aiAlgoUpper(aiAlgoUpper)
    , m_aiAlgoLower(aiAlgoLower)
    , m_pixelsPerMm(pixelsPerMm)
{
    setupUI();
    loadThresholdsFromConfig();
    // 默认显示相机2
    populateTable(2);
}

AIParamConfigDialog::~AIParamConfigDialog() = default;

void AIParamConfigDialog::setupUI()
{
    setWindowTitle("AI参数配置");
    setFixedSize(520, 680);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    // 深色主题样式
    setStyleSheet(R"(
        QDialog {
            background-color: #1e1e2e;
            color: #cdd6f4;
        }
        QLabel {
            color: #cdd6f4;
            font-family: "Microsoft YaHei";
            font-size: 18pt;
        }
        QComboBox {
            background-color: #313244;
            color: #cdd6f4;
            border: 1px solid #45475a;
            border-radius: 4px;
            padding: 6px 12px;
            font-family: "Microsoft YaHei";
            font-size: 16pt;
            min-height: 36px;
        }
        QComboBox::drop-down {
            border: none;
        }
        QComboBox QAbstractItemView {
            background-color: #313244;
            color: #cdd6f4;
            selection-background-color: #45475a;
            font-family: "Microsoft YaHei";
            font-size: 16pt;
        }
        QTableWidget {
            background-color: #181825;
            color: #cdd6f4;
            gridline-color: #45475a;
            border: 1px solid #45475a;
            font-family: "Microsoft YaHei";
            font-size: 14pt;
        }
        QTableWidget::item {
            padding: 4px;
        }
        QTableWidget::item:selected {
            background-color: #45475a;
        }
        QHeaderView::section {
            background-color: #313244;
            color: #cdd6f4;
            border: 1px solid #45475a;
            padding: 6px;
            font-family: "Microsoft YaHei";
            font-size: 14pt;
            font-weight: bold;
        }
        QPushButton {
            font-family: "Microsoft YaHei";
            font-size: 16pt;
            font-weight: bold;
            border: none;
            border-radius: 8px;
            padding: 10px 30px;
            min-height: 40px;
        }
    )");

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(16, 16, 16, 16);

    // 顶部：相机选择
    auto* topLayout = new QHBoxLayout();
    auto* label = new QLabel("相机选择:", this);
    m_cameraCombo = new QComboBox(this);
    m_cameraCombo->addItem("相机2 (上表面)", 2);
    m_cameraCombo->addItem("相机3 (下表面)", 3);
    topLayout->addWidget(label);
    topLayout->addWidget(m_cameraCombo);
    topLayout->addStretch();
    mainLayout->addLayout(topLayout);

    // 表格区域
    m_tableWidget = new QTableWidget(this);
    m_tableWidget->setColumnCount(5);
    m_tableWidget->setHorizontalHeaderLabels(
        QStringList() << "缺陷名" << "最小宽度" << "最小高度" << "最大宽度" << "最大高度");

    // 表格属性设置
    m_tableWidget->horizontalHeader()->setStretchLastSection(true);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int col = 1; col < 5; ++col) {
        m_tableWidget->horizontalHeader()->setSectionResizeMode(col, QHeaderView::ResizeToContents);
    }
    m_tableWidget->verticalHeader()->setDefaultSectionSize(45);
    m_tableWidget->verticalHeader()->hide();
    m_tableWidget->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_tableWidget->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    mainLayout->addWidget(m_tableWidget);

    // 单位提示
    auto* unitLabel = new QLabel("单位：毫米 (mm)", this);
    unitLabel->setStyleSheet("font-size: 14pt; color: #a6adc8;");
    mainLayout->addWidget(unitLabel);

    // 底部按钮
    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    m_saveBtn = new QPushButton("保存", this);
    m_saveBtn->setStyleSheet(R"(
        QPushButton {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #a6e3a1, stop:1 #94e2d5);
            color: #1e1e2e;
        }
        QPushButton:hover {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #94e2d5, stop:1 #a6e3a1);
        }
    )");

    m_cancelBtn = new QPushButton("取消", this);
    m_cancelBtn->setStyleSheet(R"(
        QPushButton {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f38ba8, stop:1 #eba0ac);
            color: #1e1e2e;
        }
        QPushButton:hover {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #eba0ac, stop:1 #f38ba8);
        }
    )");

    btnLayout->addWidget(m_saveBtn);
    btnLayout->addWidget(m_cancelBtn);
    mainLayout->addLayout(btnLayout);

    // 信号连接
    connect(m_cameraCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AIParamConfigDialog::onCameraChanged);
    connect(m_saveBtn, &QPushButton::clicked, this, &AIParamConfigDialog::onSave);
    connect(m_cancelBtn, &QPushButton::clicked, this, &AIParamConfigDialog::onCancel);
    connect(m_tableWidget, &QTableWidget::cellChanged,
            this, &AIParamConfigDialog::onTableCellChanged);
}

void AIParamConfigDialog::loadThresholdsFromConfig()
{
    // 从AlgorithmROIManager加载已保存的阈值
    AlgorithmROIManager::instance().loadAIThresholds(m_thresholds);

    // 保存原始数据副本用于修改检测
    m_originalThresholds = m_thresholds;

    // 确保两个相机都有数据条目
    if (m_thresholds.find(2) == m_thresholds.end()) {
        m_thresholds[2] = std::vector<AIDefectThreshold>();
    }
    if (m_thresholds.find(3) == m_thresholds.end()) {
        m_thresholds[3] = std::vector<AIDefectThreshold>();
    }
}

void AIParamConfigDialog::populateTable(int cameraId)
{
    m_populating = true;

    // 先收集当前表格的数据
    collectTableData(m_cameraCombo->currentData().toInt());

    // 获取对应的YOLOAlgorithm实例
    YOLOAlgorithm* algo = (cameraId == 2) ? m_aiAlgoUpper : m_aiAlgoLower;

    m_tableWidget->setRowCount(0);

    if (!algo) {
        m_populating = false;
        return;
    }

    // 获取所有类别名称
    const auto& allNames = algo->getAllClassNames();
    auto& thresholds = m_thresholds[cameraId];

    // 如果阈值列表为空或类别数不匹配，初始化默认值
    if (thresholds.size() != allNames.size()) {
        thresholds.clear();
        for (const auto& [id, name] : allNames) {
            AIDefectThreshold t;
            t.classId = id;
            t.className = name;
            thresholds.push_back(t);
        }
    }

    // 填充表格
    m_tableWidget->setRowCount(static_cast<int>(thresholds.size()));
    for (int row = 0; row < static_cast<int>(thresholds.size()); ++row) {
        const auto& t = thresholds[row];

        // 缺陷名（只读）
        auto* nameItem = new QTableWidgetItem(t.className);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        nameItem->setTextAlignment(Qt::AlignCenter);
        m_tableWidget->setItem(row, 0, nameItem);

        // 最小宽度（像素→毫米）
        double minWmm = (m_pixelsPerMm > 0) ? (t.minWidth / m_pixelsPerMm) : 0.0;
        auto* minWItem = new QTableWidgetItem(QString::number(minWmm, 'f', 2));
        minWItem->setTextAlignment(Qt::AlignCenter);
        m_tableWidget->setItem(row, 1, minWItem);

        // 最小高度（像素→毫米）
        double minHmm = (m_pixelsPerMm > 0) ? (t.minHeight / m_pixelsPerMm) : 0.0;
        auto* minHItem = new QTableWidgetItem(QString::number(minHmm, 'f', 2));
        minHItem->setTextAlignment(Qt::AlignCenter);
        m_tableWidget->setItem(row, 2, minHItem);

        // 最大宽度（像素→毫米）
        double maxWmm = (m_pixelsPerMm > 0) ? (t.maxWidth / m_pixelsPerMm) : 0.0;
        auto* maxWItem = new QTableWidgetItem(QString::number(maxWmm, 'f', 2));
        maxWItem->setTextAlignment(Qt::AlignCenter);
        m_tableWidget->setItem(row, 3, maxWItem);

        // 最大高度（像素→毫米）
        double maxHmm = (m_pixelsPerMm > 0) ? (t.maxHeight / m_pixelsPerMm) : 0.0;
        auto* maxHItem = new QTableWidgetItem(QString::number(maxHmm, 'f', 2));
        maxHItem->setTextAlignment(Qt::AlignCenter);
        m_tableWidget->setItem(row, 4, maxHItem);
    }

    m_populating = false;
}

void AIParamConfigDialog::collectTableData(int cameraId)
{
    if (cameraId < 2 || cameraId > 3) return;

    auto& thresholds = m_thresholds[cameraId];
    if (thresholds.empty()) return;

    for (int row = 0; row < m_tableWidget->rowCount() && row < static_cast<int>(thresholds.size()); ++row) {
        auto* minWItem = m_tableWidget->item(row, 1);
        auto* minHItem = m_tableWidget->item(row, 2);
        auto* maxWItem = m_tableWidget->item(row, 3);
        auto* maxHItem = m_tableWidget->item(row, 4);

        if (minWItem) thresholds[row].minWidth = qRound(minWItem->text().toDouble() * m_pixelsPerMm);
        if (maxWItem) thresholds[row].maxWidth = qRound(maxWItem->text().toDouble() * m_pixelsPerMm);
        if (minHItem) thresholds[row].minHeight = qRound(minHItem->text().toDouble() * m_pixelsPerMm);
        if (maxHItem) thresholds[row].maxHeight = qRound(maxHItem->text().toDouble() * m_pixelsPerMm);
    }
}

void AIParamConfigDialog::onCameraChanged(int index)
{
    int cameraId = m_cameraCombo->itemData(index).toInt();
    populateTable(cameraId);
}

void AIParamConfigDialog::onTableCellChanged(int row, int col)
{
    if (m_populating) return;
    if (col == 0) return;  // 缺陷名列不编辑

    // 验证输入为数字（支持小数，单位mm）
    auto* item = m_tableWidget->item(row, col);
    if (item) {
        bool ok = false;
        double val = item->text().toDouble(&ok);
        if (!ok || val < 0) {
            item->setText("0.0");
        }
    }

    m_modified = true;
}

void AIParamConfigDialog::onSave()
{
    // 收集当前表格数据
    collectTableData(m_cameraCombo->currentData().toInt());

    // 保存到XML
    saveThresholdsToConfig();

    m_modified = false;
    m_originalThresholds = m_thresholds;

    accept();
}

void AIParamConfigDialog::onCancel()
{
    if (m_modified) {
        auto ret = QMessageBox::warning(this, "未保存的更改",
            "检测到参数已修改，是否保存更改？",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);

        if (ret == QMessageBox::Save) {
            onSave();
            return;
        } else if (ret == QMessageBox::Cancel) {
            return;
        }
    }
    reject();
}

void AIParamConfigDialog::closeEvent(QCloseEvent* event)
{
    if (m_modified) {
        auto ret = QMessageBox::warning(this, "未保存的更改",
            "检测到参数已修改，是否保存更改？",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);

        if (ret == QMessageBox::Save) {
            collectTableData(m_cameraCombo->currentData().toInt());
            saveThresholdsToConfig();
            m_modified = false;
            event->accept();
        } else if (ret == QMessageBox::Discard) {
            event->accept();
        } else {
            event->ignore();
        }
    } else {
        event->accept();
    }
}

void AIParamConfigDialog::saveThresholdsToConfig()
{
    AlgorithmROIManager::instance().saveAIThresholds(m_thresholds);
}

std::vector<AIDefectThreshold> AIParamConfigDialog::getThresholdsForCamera(int cameraId) const
{
    auto it = m_thresholds.find(cameraId);
    if (it != m_thresholds.end()) {
        return it->second;
    }
    return std::vector<AIDefectThreshold>();
}
