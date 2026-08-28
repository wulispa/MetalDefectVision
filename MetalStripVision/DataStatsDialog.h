#pragma once

#include <QDialog>
#include <QTimer>
#include <vector>
#include "ui_DataStatsDialog.h"
#include "DetectionLogDatabase.h"

class DataStatsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DataStatsDialog(QWidget* parent = nullptr);
    ~DataStatsDialog();

private slots:
    void onQueryClicked();
    void onExportClicked();
    void onTodayClicked();
    void onWeekClicked();
    void onAutoRefresh();  // 自动刷新槽函数
    void onTableDoubleClicked(int row, int column);  // 表格双击槽函数
    void onPrevPageClicked();  // 上一页
    void onNextPageClicked();  // 下一页

private:
    void loadData(const QDate& date);
    void loadTodayData();
    void loadWeekData();
    void displayCurrentPage();  // 显示当前页数据
    void updatePaginationInfo();  // 更新分页信息

    Ui::DataStatsDialog ui;
    QTimer* m_refreshTimer;  // 自动刷新定时器
    QDate m_lastQueryDate;   // 最后查询的日期
    bool m_lastQueryWasWeek; // 最后一次是否查询的是本周

    // 分页相关
    std::vector<DetectionRecord> m_allRecords;  // 当前加载的记录（最多MAX_DISPLAY_RECORDS条）
    int m_currentPage;      // 当前页码（从1开始）
    int m_totalPages;       // 总页数（基于已加载的记录）
    int m_totalRecordCount; // 数据库中的总记录数（用于统计显示）
    static const int PAGE_SIZE = 100;  // 每页显示条数
    static const int MAX_DISPLAY_RECORDS = 3000;  // 最多显示的记录条数
};
