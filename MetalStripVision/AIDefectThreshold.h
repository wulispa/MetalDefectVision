#pragma once

#include <QString>
#include <vector>
#include <map>

// AI缺陷尺寸阈值配置
struct AIDefectThreshold {
    int classId = 0;
    QString className;
    int minWidth = 0;
    int maxWidth = 9999;
    int minHeight = 0;
    int maxHeight = 9999;
};
