#pragma execution_character_set("utf-8")

#include "DataStatsDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QColor>
#include <QDesktopServices>
#include <QUrl>
#include <QFile>
#include <algorithm>  // for std::min

DataStatsDialog::DataStatsDialog(QWidget* parent)
    : QDialog(parent)
    , m_lastQueryWasWeek(false)
    , m_currentPage(1)
    , m_totalPages(0)
{
    ui.setupUi(this);
    setWindowTitle("数据统计");
    resize(900, 600);

    // 创建并配置自动刷新定时器（60秒刷新一次）
    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &DataStatsDialog::onAutoRefresh);
    m_refreshTimer->start(60000);  // 60秒 = 60000毫秒

    // 连接信号
    connect(ui.queryBtn, &QPushButton::clicked, this, &DataStatsDialog::onQueryClicked);
    connect(ui.exportBtn, &QPushButton::clicked, this, &DataStatsDialog::onExportClicked);
    connect(ui.todayBtn, &QPushButton::clicked, this, &DataStatsDialog::onTodayClicked);
    connect(ui.weekBtn, &QPushButton::clicked, this, &DataStatsDialog::onWeekClicked);

    // 连接表格双击信号，用于打开图片
    connect(ui.table, &QTableWidget::cellDoubleClicked, this, &DataStatsDialog::onTableDoubleClicked);

    // 连接分页按钮
    connect(ui.prevPageBtn, &QPushButton::clicked, this, &DataStatsDialog::onPrevPageClicked);
    connect(ui.nextPageBtn, &QPushButton::clicked, this, &DataStatsDialog::onNextPageClicked);

    // 设置表格选择行为
    ui.table->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui.table->setSelectionMode(QAbstractItemView::SingleSelection);
    ui.table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui.table->horizontalHeader()->setStretchLastSection(true);
    ui.table->verticalHeader()->setVisible(false);

    // 设置文本不省略（完整显示）
    ui.table->setTextElideMode(Qt::ElideNone);

    // 设置表格列宽拉伸
    ui.table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui.table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    // 缺陷个数列：固定宽度
    ui.table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    ui.table->setColumnWidth(2, 80);   // 缺陷个数固定80像素

    // 是否次品列：固定宽度
    ui.table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    ui.table->setColumnWidth(3, 80);   // 是否次品固定80像素

    // 图片路径列：拉伸填充剩余空间
    ui.table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);

    // 算法类型列：固定宽度，确保显示完整
    ui.table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
    ui.table->setColumnWidth(5, 110);  // 算法类型固定110像素

    // 处理时间列：固定宽度
    ui.table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Fixed);
    ui.table->setColumnWidth(6, 80);   // 处理时间固定80像素

    // 设置行高
    ui.table->verticalHeader()->setDefaultSectionSize(35);

    // 设置当前日期
    ui.dateEdit->setDate(QDate::currentDate());

    // 设置查询按钮的禁用样式（与用户集按钮一致）
    QString btnStyle =
        "QPushButton {"
        "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #45475a, stop:1 #313244);"
        "  color: #cdd6f4;"
        "  border: 1px solid #585b70;"
        "  border-radius: 8px;"
        "  padding: 8px 16px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #585b70, stop:1 #45475a);"
        "  border: 1px solid #74c7ec;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #313244;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #1e1e2e;"
        "  color: #45475a;"
        "  border: 1px solid #313244;"
        "}";
    ui.queryBtn->setStyleSheet(btnStyle);
    ui.todayBtn->setStyleSheet(btnStyle);
    ui.weekBtn->setStyleSheet(btnStyle);
}

DataStatsDialog::~DataStatsDialog()
{
    // 停止定时器
    if (m_refreshTimer) {
        m_refreshTimer->stop();
    }
}

void DataStatsDialog::onAutoRefresh()
{
    // 自动刷新：根据最后一次查询的类型重新加载数据
    if (m_lastQueryWasWeek) {
        loadWeekData();
        ui.statusLabel->setText("已自动刷新本周的数据");
    } else if (m_lastQueryDate.isValid()) {
        loadData(m_lastQueryDate);
        ui.statusLabel->setText(QString("已自动刷新 %1 年 %2 月 %3 日的数据")
                                .arg(m_lastQueryDate.year())
                                .arg(m_lastQueryDate.month())
                                .arg(m_lastQueryDate.day()));
    } else {
        // 如果还没有查询过，默认加载今天的数据
        loadTodayData();
        ui.statusLabel->setText("已自动刷新今天的数据");
    }
}

void DataStatsDialog::onQueryClicked()
{
    QDate selectedDate = ui.dateEdit->date();
    m_lastQueryDate = selectedDate;
    m_lastQueryWasWeek = false;
    loadData(selectedDate);
    ui.statusLabel->setText(QString("已加载 %1 年 %2 月 %3 日的数据")
                            .arg(selectedDate.year())
                            .arg(selectedDate.month())
                            .arg(selectedDate.day()));
}

void DataStatsDialog::onTodayClicked()
{
    m_lastQueryDate = QDate::currentDate();
    m_lastQueryWasWeek = false;
    loadTodayData();
    ui.statusLabel->setText("已加载今天的数据");
}

void DataStatsDialog::onWeekClicked()
{
    m_lastQueryWasWeek = true;
    loadWeekData();
    ui.statusLabel->setText("已加载本周的数据");
}

void DataStatsDialog::onExportClicked()
{
    QString fileName = QFileDialog::getSaveFileName(this, "导出数据", "", "CSV Files (*.csv)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "错误", "无法打开文件进行写入");
        return;
    }

    QTextStream out(&file);
    // 写入CSV表头
    out << "时间,相机,缺陷个数,是否次品,图片路径,算法类型,处理时间(ms)\n";

    // 写入数据
    for (int row = 0; row < ui.table->rowCount(); ++row) {
        for (int col = 0; col < ui.table->columnCount(); ++col) {
            if (col > 0) out << ",";
            out << ui.table->item(row, col)->text();
        }
        out << "\n";
    }

    file.close();
    ui.statusLabel->setText(QString("数据已导出到: %1").arg(fileName));
}

void DataStatsDialog::loadData(const QDate& date)
{
    m_allRecords.clear();
    m_currentPage = 1;

    DetectionLogDatabase& db = DetectionLogDatabase::instance();

    // 获取统计数据（总数和次品数，用于计算次品率）
    int totalCount = db.getTotalCount(date);
    int defectCount = db.getDefectCount(date);
    m_totalRecordCount = totalCount;

    // 只查询最近的1000条记录用于显示
    int recordCount = db.getRecordCount(date);
    if (recordCount == 0) {
        ui.table->setRowCount(0);
        ui.totalCountEdit->setText("0");
        ui.defectCountEdit->setText("0");
        ui.defectRateEdit->setText("0.00%");
        ui.statusLabel->setText(QString("未找到 %1 的数据").arg(date.toString("yyyyMMdd")));
        updatePaginationInfo();
        return;
    }

    // 限制查询数量为最多1000条
    int queryCount = std::min(recordCount, MAX_DISPLAY_RECORDS);
    m_allRecords = db.queryRecords(date, queryCount, 0);

    // 计算总页数（基于已加载的记录）
    m_totalPages = (m_allRecords.size() + PAGE_SIZE - 1) / PAGE_SIZE;
    if (m_totalPages == 0) m_totalPages = 1;

    // 更新统计摘要（显示实际总数）
    ui.totalCountEdit->setText(QString::number(totalCount));
    ui.defectCountEdit->setText(QString::number(defectCount));

    if (totalCount > 0) {
        double rate = (double)defectCount / totalCount * 100.0;
        ui.defectRateEdit->setText(QString("%1%").arg(rate, 0, 'f', 2));
    } else {
        ui.defectRateEdit->setText("0.00%");
    }

    // 显示当前页
    displayCurrentPage();
    updatePaginationInfo();

    // 状态栏显示：显示条数和总条数
    if (recordCount > MAX_DISPLAY_RECORDS) {
        ui.statusLabel->setText(QString("显示最近 %1 条，共 %2 条记录")
                                .arg(m_allRecords.size()).arg(recordCount));
    } else {
        ui.statusLabel->setText(QString("加载完成，共 %1 条记录").arg(recordCount));
    }
}

void DataStatsDialog::displayCurrentPage()
{
    ui.table->setRowCount(0);

    if (m_allRecords.empty()) {
        return;
    }

    // 正序显示，最新的在前面（数据库已按ID降序排列）
    int startIdx = (m_currentPage - 1) * PAGE_SIZE;
    int endIdx = std::min((int)m_allRecords.size(), m_currentPage * PAGE_SIZE);

    for (int i = startIdx; i < endIdx; i++) {
        const auto& record = m_allRecords[i];
        int row = ui.table->rowCount();
        ui.table->insertRow(row);

        ui.table->setItem(row, 0, new QTableWidgetItem(record.time));
        ui.table->setItem(row, 1, new QTableWidgetItem(record.camera));
        ui.table->setItem(row, 2, new QTableWidgetItem(QString::number(record.defectNum)));
        ui.table->setItem(row, 3, new QTableWidgetItem(record.isDefect ? "是" : "否"));
        ui.table->setItem(row, 4, new QTableWidgetItem(record.imagePath));

        QTableWidgetItem* algoItem = new QTableWidgetItem(record.algorithm);
        algoItem->setTextAlignment(Qt::AlignCenter);
        ui.table->setItem(row, 5, algoItem);

        QTableWidgetItem* timeItem = new QTableWidgetItem(record.processTime);
        timeItem->setTextAlignment(Qt::AlignCenter);
        ui.table->setItem(row, 6, timeItem);

        // 设置交替行背景颜色和字体颜色
        for (int col = 0; col < ui.table->columnCount(); col++) {
            QTableWidgetItem* item = ui.table->item(row, col);
            if (item) {
                if (row % 2 == 0) {
                    item->setBackground(QColor(230, 240, 255));
                } else {
                    item->setBackground(QColor(255, 255, 255));
                }
                item->setForeground(QBrush(QColor(0, 0, 0)));
            }
        }
    }
}

void DataStatsDialog::updatePaginationInfo()
{
    ui.pageInfoLabel->setText(QString("第 %1/%2 页").arg(m_currentPage).arg(m_totalPages));
    ui.prevPageBtn->setEnabled(m_currentPage > 1);
    ui.nextPageBtn->setEnabled(m_currentPage < m_totalPages);
}

void DataStatsDialog::onPrevPageClicked()
{
    if (m_currentPage > 1) {
        m_currentPage--;
        displayCurrentPage();
        updatePaginationInfo();
    }
}

void DataStatsDialog::onNextPageClicked()
{
    if (m_currentPage < m_totalPages) {
        m_currentPage++;
        displayCurrentPage();
        updatePaginationInfo();
    }
}

void DataStatsDialog::loadTodayData()
{
    loadData(QDate::currentDate());
}

void DataStatsDialog::loadWeekData()
{
    m_allRecords.clear();
    m_currentPage = 1;

    QDate today = QDate::currentDate();
    QDate startOfWeek = today.addDays(-(today.dayOfWeek() - 1));

    DetectionLogDatabase& db = DetectionLogDatabase::instance();

    // 获取统计数据（总数和次品数，用于计算次品率）
    int totalCount = db.getTotalCountWeek(startOfWeek);
    int defectCount = db.getDefectCountWeek(startOfWeek);
    m_totalRecordCount = totalCount;

    // 获取记录总数
    int recordCount = db.getRecordCountWeek(startOfWeek);
    if (recordCount > 0) {
        // 只查询最近的1000条记录用于显示
        int queryCount = std::min(recordCount, MAX_DISPLAY_RECORDS);
        m_allRecords = db.queryRecordsWeek(startOfWeek, queryCount, 0);
    }

    // 计算总页数（基于已加载的记录）
    m_totalPages = (m_allRecords.size() + PAGE_SIZE - 1) / PAGE_SIZE;
    if (m_totalPages == 0) m_totalPages = 1;

    // 更新统计摘要（显示实际总数）
    ui.totalCountEdit->setText(QString::number(totalCount));
    ui.defectCountEdit->setText(QString::number(defectCount));

    if (totalCount > 0) {
        double rate = (double)defectCount / totalCount * 100.0;
        ui.defectRateEdit->setText(QString("%1%").arg(rate, 0, 'f', 2));
    } else {
        ui.defectRateEdit->setText("0.00%");
    }

    // 显示当前页
    displayCurrentPage();
    updatePaginationInfo();

    // 状态栏显示：显示条数和总条数
    if (recordCount > MAX_DISPLAY_RECORDS) {
        ui.statusLabel->setText(QString("本周：显示最近 %1 条，共 %2 条记录")
                                .arg(m_allRecords.size()).arg(recordCount));
    } else {
        ui.statusLabel->setText(QString("本周数据加载完成，共 %1 条记录").arg(recordCount));
    }
}

// 表格双击事件处理：打开图片
void DataStatsDialog::onTableDoubleClicked(int row, int column)
{
    // 只处理图片路径列（第4列）
    if (column != 4) {
        return;
    }

    QTableWidgetItem* item = ui.table->item(row, column);
    if (!item) {
        return;
    }

    QString imagePath = item->text();

    // 检查是否为有效路径（不是 N/A 或空）
    if (imagePath.isEmpty() || imagePath == "N/A") {
        QMessageBox::information(this, "提示", "该记录没有保存图片。");
        return;
    }

    // 检查文件是否存在
    if (!QFile::exists(imagePath)) {
        QMessageBox::warning(this, "错误", QString("图片文件不存在：\n%1").arg(imagePath));
        return;
    }

    // 使用系统默认程序打开图片
    QUrl url = QUrl::fromLocalFile(imagePath);
    if (!QDesktopServices::openUrl(url)) {
        QMessageBox::warning(this, "错误", "无法打开图片。");
    }
}
