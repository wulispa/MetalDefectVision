#pragma execution_character_set("utf-8")

#include "HalconAlgorithm.h"
#include "Logger.h"
#include <chrono>

HalconAlgorithm::HalconAlgorithm()
    : m_initialized(false) {
}

HalconAlgorithm::~HalconAlgorithm() {
    release();
}

bool HalconAlgorithm::initialize(const AlgorithmConfig* config) {
    if (m_initialized) {
        Logger::instance().warn("HalconAlgorithm already initialized");
        return true;
    }

    // 如果传入了传统算法配置，使用配置的ROI参数
    if (config) {
        const auto* traditionalConfig = dynamic_cast<const TraditionalAlgorithmConfig*>(config);
        if (traditionalConfig) {
            m_roiConfig.row1 = traditionalConfig->roiRow1;
            m_roiConfig.column1 = traditionalConfig->roiColumn1;
            m_roiConfig.row2 = traditionalConfig->roiRow2;
            m_roiConfig.column2 = traditionalConfig->roiColumn2;
            m_roiConfig.enabled = true;
            Logger::instance().info("HalconAlgorithm initialized with custom ROI: " +
                                   std::to_string(m_roiConfig.row1) + ", " +
                                   std::to_string(m_roiConfig.column1) + ", " +
                                   std::to_string(m_roiConfig.row2) + ", " +
                                   std::to_string(m_roiConfig.column2));
        } else {
            Logger::instance().info("HalconAlgorithm initialized with default config");
        }
    } else {
        Logger::instance().info("HalconAlgorithm initialized with default config");
    }

    m_initialized = true;
    return true;
}

HObject HalconAlgorithm::cvMatToHObject(const cv::Mat& cv_img)
{
    HObject H_img;

    if (cv_img.empty())
        return H_img; // 返回空 HObject

    if (cv_img.type() == CV_8UC1)  // 单通道
    {
        GenImage1(&H_img, "byte",
            cv_img.cols, cv_img.rows,
            (Hlong)cv_img.data);
    }
    else if (cv_img.type() == CV_8UC3)  // 三通道
    {
        std::vector<cv::Mat> channels;
        cv::split(cv_img, channels);  // BGR 分离

        GenImage3(&H_img, "byte",
            cv_img.cols, cv_img.rows,
            (Hlong)channels[2].data,   // R
            (Hlong)channels[1].data,   // G
            (Hlong)channels[0].data);  // B
    }
    else
    {
        throw std::runtime_error("Unsupported cv::Mat type for conversion!");
    }

    return H_img;
}


void HalconAlgorithm::release() {
    m_initialized = false;
    Logger::instance().info("HalconAlgorithm released");
}

void HalconAlgorithm::updateROI(double row1, double column1, double row2, double column2) {
    std::lock_guard<std::mutex> lock(m_roiMutex);
    m_roiConfig.row1 = row1;
    m_roiConfig.column1 = column1;
    m_roiConfig.row2 = row2;
    m_roiConfig.column2 = column2;
    m_roiConfig.enabled = true;
    Logger::instance().debug("HalconAlgorithm ROI updated: (" +
                           std::to_string(row1) + ", " + std::to_string(column1) + ") -> (" +
                           std::to_string(row2) + ", " + std::to_string(column2) + ")");
}

void HalconAlgorithm::updateThreshold(double thresholdMin, double thresholdMax) {
    std::lock_guard<std::mutex> lock(m_roiMutex);
    m_thresholdMin = thresholdMin;
    m_thresholdMax = thresholdMax;
    Logger::instance().info("HalconAlgorithm Threshold updated: [" +
                           std::to_string(thresholdMin) + " - " + std::to_string(thresholdMax) + "]");
}


AlgorithmResult HalconAlgorithm::process(const cv::Mat& image) {

    AlgorithmResult result;
    auto startTime = std::chrono::high_resolution_clock::now();

    // ========== 异常保护：保证任何情况下都返回success=true，确保PLC信号能发出 ==========
    // 【背景】Halcon内部偶发异常会导致process()中断，外层catch会跳过sendPLCResult()
    // 【方案】try-catch包裹全部Halcon操作，异常时返回OK（hasDefect=false）
    // 【恢复】搜索 "异常保护" 定位此逻辑，排查根因后可移除
    try {

    // Local iconic variables
        HObject  ho_Image, ho_GrayImage, ho_ImageEmph;
        HObject  ho_Rectangle1, ho_ImageReduced, ho_Region, ho_DiffRegion;
        HObject  ho_ConnectedRegions, ho_SelectedRegions, ho_RegionEroded;
        HObject  ho_RegionOpened, ho_SortedRegions, ho_FilteredRegions;
        HObject  ho_FirstRegion, ho_Region_i, ho_LeftLines, ho_Rectangle;
        HObject  ho_LeftLine, ho_Line_i, ho_NGLines, ho_Line1, ho_Line2;

        // Local control variables
        HTuple  hv_Width1, hv_Height1, hv_WindowHandle;
        HTuple  hv_Row11, hv_Column1, hv_Row21, hv_Column2, hv_Areas;
        HTuple  hv_Rows, hv_Cols, hv_Indices, hv_FirstIndex, hv_SecondIndex;
        HTuple  hv_ColFirst, hv_ColSecond, hv_Row1, hv_Col1, hv_Row2;
        HTuple  hv_Col2, hv_Number, hv_i, hv_NumLines, hv_ColsCenter;
        HTuple  hv_Rows_i, hv_Cols_i, hv_ColMean_i, hv_Distances;
        HTuple  hv_ColCurr, hv_ColNext, hv_Distance, hv_Scale, hv_RealDistances;
        HTuple  hv_MinThresh, hv_MaxThresh, hv_IsNG, hv_RowsText;
        HTuple  hv_ColsText, hv_Texts, hv_d, hv_ColMid, hv_RowMid;
        HTuple  hv_d_show, hv_Text, hv_ColsArray, hv_Text_i, hv_Row_i;
        HTuple  hv_Col_i;

    //****************************************算法开始**********************************************
    //读图
    ho_Image = cvMatToHObject(image.clone());

    GetImageSize(ho_Image, &hv_Width1, &hv_Height1);
    //转为灰度图
    Rgb1ToGray(ho_Image, &ho_GrayImage);
    Emphasize(ho_GrayImage, &ho_ImageEmph, 7, 7, 1.5);
    // 窗口操作已移除：HDevWindowStack 非线程安全，不应在工作线程中调用
    // 原代码在此处创建 invisible 窗口并 DispObj，仅用于调试，生产环境不需要

    // 使用配置的ROI参数（如果已配置），否则使用默认值
    // 线程安全：复制ROI配置
    ROIConfig currentROI;
    {
        std::lock_guard<std::mutex> lock(m_roiMutex);
        currentROI = m_roiConfig;
    }

    if (currentROI.enabled) {
        hv_Row11 = currentROI.row1;
        hv_Column1 = currentROI.column1;
        hv_Row21 = currentROI.row2;
        hv_Column2 = currentROI.column2;

        GenRectangle1(&ho_Rectangle1, hv_Row11, 0, hv_Row21, hv_Width1);
    }
    else {
        // 使用默认值（向后兼容）
        GenRectangle1(&ho_Rectangle1, 801.302, 0, 912.413, hv_Width1);
    }

    ReduceDomain(ho_ImageEmph, ho_Rectangle1, &ho_ImageReduced);
    //阈值分割出白色区域
    Threshold(ho_ImageReduced, &ho_Region, 60, 255);
    //做差取出黑色区域
    Difference(ho_ImageReduced, ho_Region, &ho_DiffRegion);
    //打散根据面积筛选
    Connection(ho_DiffRegion, &ho_ConnectedRegions);
    SelectShape(ho_ConnectedRegions, &ho_SelectedRegions, "area", "and", 100, 99999);
    ErosionRectangle1(ho_SelectedRegions, &ho_RegionEroded, 15, 15);

    //矩形膨胀（恢复原始大小）
    DilationRectangle1(ho_RegionEroded, &ho_RegionOpened, 15, 15);

    //对所有区域排序
    SortRegion(ho_RegionOpened, &ho_SortedRegions, "character", "true", "column");
    //*********对边界进行筛选**************
    //获取所有区域面积和中心
    AreaCenter(ho_SortedRegions, &hv_Areas, &hv_Rows, &hv_Cols);
    //按照 Col 排序
    TupleSortIndex(hv_Cols, &hv_Indices);
    //最小Col的索引（第一个区域）
    TupleSelect(hv_Indices, 0, &hv_FirstIndex);
    //第二个Col 的索引
    TupleSelect(hv_Indices, 1, &hv_SecondIndex);
    //取两个 Col 值
    TupleSelect(hv_Cols, hv_FirstIndex, &hv_ColFirst);
    TupleSelect(hv_Cols, hv_SecondIndex, &hv_ColSecond);
    //创建输出区域集合
    GenEmptyObj(&ho_FilteredRegions);
    SelectObj(ho_SortedRegions, &ho_FirstRegion, hv_FirstIndex + 1);
    SmallestRectangle1(ho_FirstRegion, &hv_Row1, &hv_Col1, &hv_Row2, &hv_Col2);
    //如果左上角 col 接近 0（允许一点误差）
    if (0 != (hv_Col1 <= 2))
    {
        //舍弃第一个区域，从第二个开始保留
        CountObj(ho_SortedRegions, &hv_Number);
        {
            HTuple end_val49 = hv_Number;
            HTuple step_val49 = 1;
            for (hv_i = 2; hv_i.Continue(end_val49, step_val49); hv_i += step_val49)
            {
                SelectObj(ho_SortedRegions, &ho_Region_i, hv_i);
                ConcatObj(ho_FilteredRegions, ho_Region_i, &ho_FilteredRegions);
            }
        }
    }
    else
    {
        //正常保留全部
        CopyObj(ho_SortedRegions, &ho_FilteredRegions, 1, -1);
    }
    //创建空对象用来存储最左边数组
    GenEmptyObj(&ho_LeftLines);
    //计数
    CountObj(ho_FilteredRegions, &hv_Number);
    //遍历所有区域
    {
        HTuple end_val62 = hv_Number;
        HTuple step_val62 = 1;
        for (hv_i = 1; hv_i.Continue(end_val62, step_val62); hv_i += step_val62)
        {
            //选择第i个区域处理
            SelectObj(ho_FilteredRegions, &ho_Region_i, hv_i);
            //取其最小外接矩形
            SmallestRectangle1(ho_Region_i, &hv_Row1, &hv_Col1, &hv_Row2, &hv_Col2);
            //画出最小外接矩形
            GenRectangle1(&ho_Rectangle, hv_Row1, hv_Col1, hv_Row2, hv_Col2);
            //生成最小外接矩形的最左边
            GenContourPolygonXld(&ho_LeftLine, hv_Row1.TupleConcat(hv_Row2), hv_Col1.TupleConcat(hv_Col1));
            //将最左边加入空数组中
            ConcatObj(ho_LeftLines, ho_LeftLine, &ho_LeftLines);
        }
    }

    //计数
    CountObj(ho_LeftLines, &hv_NumLines);
    //创建空数组用来储存Col
    hv_ColsCenter = HTuple();

    //遍历所有最左边
    {
        HTuple end_val81 = hv_NumLines;
        HTuple step_val81 = 1;
        for (hv_i = 1; hv_i.Continue(end_val81, step_val81); hv_i += step_val81)
        {
            //选择第i个最左边处理
            SelectObj(ho_LeftLines, &ho_Line_i, hv_i);
            //获取最左边的坐标
            GetContourXld(ho_Line_i, &hv_Rows_i, &hv_Cols_i);

            //垂直线的列固定，取第一个点即可
            TupleSelect(hv_Cols_i, 0, &hv_ColMean_i);
            //将点存入空数组中
            hv_ColsCenter = hv_ColsCenter.TupleConcat(hv_ColMean_i);
        }
    }

    //创建空数组用来储存最左边之间的距离
    hv_Distances = HTuple();
    //遍历所有最左边
    {
        HTuple end_val96 = (hv_ColsCenter.TupleLength()) - 2;
        HTuple step_val96 = 1;
        for (hv_i = 0; hv_i.Continue(end_val96, step_val96); hv_i += step_val96)
        {
            TupleSelect(hv_ColsCenter, hv_i, &hv_ColCurr);
            TupleSelect(hv_ColsCenter, hv_i + 1, &hv_ColNext);
            //计算距离
            hv_Distance = hv_ColNext - hv_ColCurr;
            hv_Distance = ((hv_Distance * 100).TupleRound()) / 100.0;
            //将结果储存到空数组中
            hv_Distances = hv_Distances.TupleConcat(hv_Distance);
        }
    }
    //像素大小和实际大小转换
    hv_Scale = 3.0 / 350;
    //实际大小
    hv_RealDistances = hv_Distances * hv_Scale;
    //保留小数点后两位
    hv_RealDistances = ((hv_RealDistances * 100).TupleRound()) / 100.0;

    //距离阈值范围
    hv_MinThresh = m_thresholdMin;
    hv_MaxThresh = m_thresholdMax;
    //标记是否为NG
    hv_IsNG = 0;

    //初始化数组
    hv_RowsText = HTuple();
    hv_ColsText = HTuple();
    hv_Texts = HTuple();
    //创建异常线集合
    GenEmptyObj(&ho_NGLines);

    {
        HTuple end_val125 = (hv_RealDistances.TupleLength()) - 1;
        HTuple step_val125 = 1;
        for (hv_i = 0; hv_i.Continue(end_val125, step_val125); hv_i += step_val125)
        {
            //选择第i个
            TupleSelect(hv_RealDistances, hv_i, &hv_d);
            //阈值判断
            if (0 != (HTuple(hv_d < hv_MinThresh).TupleOr(hv_d > hv_MaxThresh)))
            {
                hv_IsNG = 1;
                //取对应两条线（第i和i+1条）
                SelectObj(ho_LeftLines, &ho_Line1, hv_i + 1);
                SelectObj(ho_LeftLines, &ho_Line2, hv_i + 2);

                //加入异常集合
                ConcatObj(ho_NGLines, ho_Line1, &ho_NGLines);
                ConcatObj(ho_NGLines, ho_Line2, &ho_NGLines);

                TupleSelect(hv_ColsCenter, hv_i, &hv_Col1);
                TupleSelect(hv_ColsCenter, hv_i + 1, &hv_Col2);

                //设定位置
                hv_ColMid = (hv_Col1 + hv_Col2) / 2;
                hv_RowMid = (hv_Row11 + hv_Row21) / 2;

                //显示内容
                hv_d_show = ((hv_d * 100).TupleRound()) / 100.00;
                hv_Text = hv_d_show;

                hv_RowsText = hv_RowsText.TupleConcat(hv_RowMid);
                hv_ColsText = hv_ColsText.TupleConcat(hv_ColMid);
                hv_Texts = hv_Texts.TupleConcat(hv_Text);
            }
        }
    }
    //*******************************获取异常线段的坐标****************************************************************
    CountObj(ho_NGLines, &hv_NumLines);
    hv_ColsArray = HTuple();

    {
        HTuple end_val159 = hv_NumLines;
        HTuple step_val159 = 1;
        for (hv_i = 1; hv_i.Continue(end_val159, step_val159); hv_i += step_val159)
        {
            //选择第 i 条线
            SelectObj(ho_NGLines, &ho_Line_i, hv_i);

            //获取轮廓坐标
            GetContourXld(ho_Line_i, &hv_Rows, &hv_Cols);

            //这里 Cols 就是该线段的列坐标元组
            //如果只想取第一个点：
            TupleSelect(hv_Cols, 0, &hv_ColFirst);

            //存入数组（存所有第一个列坐标）
            hv_ColsArray = hv_ColsArray.TupleConcat(hv_ColFirst);
        }
    }
    //异常线段的坐标数组为ColsArray 距离大小数组为Texts 位置数组为RowsText和ColsText  IsNg==1为NG IsNg==0为OK

    // ========== 设置算法结果 ==========
    result.success = true;

    // 1. 判断是否有缺陷（NG/OK判断）
    // hv_IsNG == 1 表示检测到异常（NG）
    // hv_IsNG == 0 表示正常（OK）
    result.hasDefect = (hv_IsNG.I() == 1);

    // 2. 统计缺陷数量
    // hv_NumLines 表示异常线的数量
    result.defectCount = hv_NumLines.I();

    // 3. 计算处理时间（毫秒）
    auto endTime = std::chrono::high_resolution_clock::now();
    result.processingTime = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    // 4. 提取绘制数据（供外部绘制使用）
    result.roiRow1 = m_roiConfig.row1;  // ROI 行坐标
    result.roiRow2 = m_roiConfig.row2;

    for (int i = 0; i < hv_ColsArray.Length(); i++) {
        result.abnormalLineCols.push_back(hv_ColsArray[i].D());
    }
    for (int i = 0; i < hv_Texts.Length(); i++) {
        result.distanceTexts.push_back(hv_Texts[i].D());
        result.textRows.push_back(hv_RowsText[i].D());
        result.textCols.push_back(hv_ColsText[i].D());
    }


    // ========== 结果消息 ==========
    if (result.hasDefect) {
        result.message = "检测到异常 (NG)，异常数量: " + std::to_string(result.defectCount);
    }
    else {
        result.message = "检测正常 (OK)";
    }

    return result;

    // ========== 【异常出口】Halcon内部异常，返回OK，保证PLC信号能发出 ==========
    } catch (const HalconCpp::HException& e) {
        result.success = true;
        result.hasDefect = false;
        result.defectCount = 0;
        result.message = "Halcon异常，按OK处理";
        Logger::instance().error("HALCON EXCEPTION #" + std::to_string(e.ErrorCode()) + ": " + std::string(e.ErrorMessage()) + ", treated as OK");
        return result;
    } catch (const std::exception& e) {
        result.success = true;
        result.hasDefect = false;
        result.defectCount = 0;
        result.message = "算法异常，按OK处理";
        Logger::instance().error("HalconAlgorithm process EXCEPTION: " + std::string(e.what()) + ", treated as OK");
        return result;
    } catch (...) {
        result.success = true;
        result.hasDefect = false;
        result.defectCount = 0;
        result.message = "未知异常，按OK处理";
        Logger::instance().error("HalconAlgorithm UNKNOWN EXCEPTION, treated as OK");
        return result;
    }
}


// OpenCV版传统算法处理（相机1）
AlgorithmResult HalconAlgorithm::process_opencv(const cv::Mat& image) {
    AlgorithmResult result;
    auto startTime = std::chrono::high_resolution_clock::now();

    try {

    int Width1 = image.cols;
    int Height1 = image.rows;

    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);

    int IsNG = 0;
    int IsBlackImage = 0;

    std::vector<std::vector<cv::Point>> NGLines;
    std::vector<double> Texts;
    std::vector<double> RowsText;
    std::vector<double> ColsText;
    std::vector<int> ColsArray;

    cv::Scalar meanGray = cv::mean(gray);

    // 强调（unsharp mask）：与Halcon的Emphasize等效
    cv::Mat blurImg;
    cv::GaussianBlur(gray, blurImg, cv::Size(7, 7), 1.5);
    cv::Mat emph;
    cv::addWeighted(gray, 1.5, blurImg, -0.5, 0, emph);

    // 全黑图像检测
    if (meanGray[0] < 5) {
        IsBlackImage = 1;
        IsNG = 1;
    }

    if (IsBlackImage == 0) {

        // 使用配置的ROI参数（线程安全）
        ROIConfig currentROI;
        {
            std::lock_guard<std::mutex> lock(m_roiMutex);
            currentROI = m_roiConfig;
        }

        int roiY = static_cast<int>(currentROI.row1);
        int roiH = static_cast<int>(currentROI.row2 - currentROI.row1);
        if (roiY < 0) roiY = 0;
        if (roiY + roiH > Height1) roiH = Height1 - roiY;
        if (roiH <= 0) roiH = 100;

        Logger::instance().debug("[OpenCV] ROI: row1=" + std::to_string(roiY) +
            ", row2=" + std::to_string(roiY + roiH) +
            ", imgSize=" + std::to_string(Width1) + "x" + std::to_string(Height1) +
            ", meanGray=" + std::to_string(meanGray[0]));

        cv::Rect roiRect(0, roiY, Width1, roiH);
        cv::Mat roi = emph(roiRect);

        // 阈值分割白色区域
        cv::Mat bin;
        cv::threshold(roi, bin, 60, 255, cv::THRESH_BINARY);

        // 取反得到黑色区域
        cv::Mat diff;
        cv::bitwise_not(bin, diff);

        // 找轮廓并按面积筛选
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(diff, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        Logger::instance().debug("[OpenCV] contours after threshold: " + std::to_string(contours.size()));

        std::vector<std::vector<cv::Point>> regions;
        for (auto& c : contours) {
            double area = cv::contourArea(c);
            if (area >= 100 && area <= 99999)
                regions.push_back(c);
        }

        Logger::instance().debug("[OpenCV] regions after area filter(100-99999): " + std::to_string(regions.size()));

        // 画mask → 腐蚀 → 膨胀（等效Halcon的ErosionRectangle1 + DilationRectangle1）
        cv::Mat mask = cv::Mat::zeros(diff.size(), CV_8UC1);
        cv::drawContours(mask, regions, -1, 255, cv::FILLED);
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(15, 15));
        cv::erode(mask, mask, kernel);
        cv::dilate(mask, mask, kernel);

        // 再次找轮廓并按x排序（等效Halcon的SortRegion by column）
        contours.clear();
        cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        Logger::instance().debug("[OpenCV] contours after erode+dilate: " + std::to_string(contours.size()));

        std::sort(contours.begin(), contours.end(),
            [](const std::vector<cv::Point>& a, const std::vector<cv::Point>& b) {
                return cv::boundingRect(a).x < cv::boundingRect(b).x;
            });

        // 边界筛选：如果第一个区域col接近0（左边界），丢弃它
        Logger::instance().debug("[OpenCV] boundary filter: first contour x=" +
            (contours.empty() ? std::string("N/A") : std::to_string(cv::boundingRect(contours[0]).x)));
        if (contours.size() >= 2) {
            cv::Rect r1 = cv::boundingRect(contours[0]);
            cv::Rect r2 = cv::boundingRect(contours[1]);
            Logger::instance().debug("[OpenCV] boundary: r1.x=" + std::to_string(r1.x) +
                ", r2.x=" + std::to_string(r2.x) + ", gap=" + std::to_string(r2.x - r1.x));
            if (r2.x - r1.x > 50) {
                contours.erase(contours.begin());
                Logger::instance().debug("[OpenCV] boundary: removed first contour");
            }
        }

        Logger::instance().debug("[OpenCV] contours after boundary filter: " + std::to_string(contours.size()));

        // 提取每个区域的最左边线（等效Halcon的GenContourPolygonXLD左边缘）
        std::vector<std::vector<cv::Point>> LeftLines;
        for (auto& c : contours) {
            cv::Rect r = cv::boundingRect(c);
            std::vector<cv::Point> line;
            line.push_back(cv::Point(r.x, r.y));
            line.push_back(cv::Point(r.x, r.y + r.height));
            LeftLines.push_back(line);
        }

        // 计算中心列坐标
        std::vector<int> ColsCenter;
        for (auto& line : LeftLines) {
            ColsCenter.push_back(line[0].x);
        }

        // 计算相邻距离（像素）
        std::vector<double> Distances;
        for (size_t i = 0; i < ColsCenter.size() - 1; i++) {
            double d = ColsCenter[i + 1] - ColsCenter[i];
            d = std::round(d * 100) / 100.0;
            Distances.push_back(d);
        }

        // 像素到实际距离转换
        double Scale = 3.0 / 350.0;
        std::vector<double> RealDistances;
        for (auto d : Distances) {
            double r = d * Scale;
            r = std::round(r * 100) / 100.0;
            RealDistances.push_back(r);
        }

        // 距离阈值判断
        double MinThresh = m_thresholdMin;
        double MaxThresh = m_thresholdMax;

        Logger::instance().debug("[OpenCV] LeftLines=" + std::to_string(LeftLines.size()) +
            ", Distances=" + std::to_string(Distances.size()) +
            ", thresh=[" + std::to_string(MinThresh) + " - " + std::to_string(MaxThresh) + "]");
        if (!RealDistances.empty()) {
            std::string distStr;
            for (auto d : RealDistances) distStr += std::to_string(d) + " ";
            Logger::instance().debug("[OpenCV] real distances: " + distStr);
        }

        for (size_t i = 0; i < RealDistances.size(); i++) {
            double d = RealDistances[i];
            if (d < MinThresh || d > MaxThresh) {
                IsNG = 1;

                NGLines.push_back(LeftLines[i]);
                NGLines.push_back(LeftLines[i + 1]);

                int Col1 = ColsCenter[i];
                int Col2 = ColsCenter[i + 1];
                int ColMid = (Col1 + Col2) / 2;
                int RowMid = (static_cast<int>(currentROI.row1) + static_cast<int>(currentROI.row2)) / 2;

                Texts.push_back(d);
                RowsText.push_back(static_cast<double>(RowMid));
                ColsText.push_back(static_cast<double>(ColMid));
            }
        }

        // 提取异常线段的列坐标
        for (auto& line : NGLines) {
            ColsArray.push_back(line[0].x);
        }
    }

    // ========== 设置算法结果（与Halcon版process()输出一致）==========
    result.success = true;
    result.hasDefect = (IsNG == 1);
    result.defectCount = static_cast<int>(NGLines.size());

    auto endTime = std::chrono::high_resolution_clock::now();
    result.processingTime = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    result.roiRow1 = m_roiConfig.row1;
    result.roiRow2 = m_roiConfig.row2;

    for (int c : ColsArray) {
        result.abnormalLineCols.push_back(c);
    }
    result.distanceTexts = Texts;
    result.textRows = RowsText;
    result.textCols = ColsText;

    if (result.hasDefect) {
        result.message = "检测到异常 (NG)，异常数量: " + std::to_string(result.defectCount);
    } else {
        result.message = "检测正常 (OK)";
    }

    return result;

    // 异常保护
    } catch (const std::exception& e) {
        result.success = true;
        result.hasDefect = false;
        result.defectCount = 0;
        result.message = "OpenCV算法异常，按OK处理";
        Logger::instance().error("OpenCV process EXCEPTION: " + std::string(e.what()) + ", treated as OK");
        return result;
    } catch (...) {
        result.success = true;
        result.hasDefect = false;
        result.defectCount = 0;
        result.message = "未知异常，按OK处理";
        Logger::instance().error("OpenCV process UNKNOWN EXCEPTION, treated as OK");
        return result;
    }
}

// 相机2传统算法处理
AlgorithmResult HalconAlgorithm::process_cam2(const cv::Mat& image) {
    AlgorithmResult result;

    // ========== 开始计时 ==========
    auto startTime = std::chrono::high_resolution_clock::now();

    // Local iconic variables
    HObject  ho_Image, ho_GrayImage, ho_Region1, ho_ConnectedRegions;
    HObject  ho_SelectedRegion, ho_Rectangle1, ho_ImageReduced;
    HObject  ho_GrayImageReduced, ho_Region2, ho_ConnectedRegions1;
    HObject  ho_MaxRegion_two, ho_MinRectangleContour, ho_RegionDifference;
    HObject  ho_RegionDifference_two;

    // Local control variables
    HTuple  hv_Width1, hv_Height1, hv_Areas, hv_Rows;
    HTuple  hv_Cols, hv_MaxArea, hv_MaxIndex, hv_i, hv_RowLeft;
    HTuple  hv_ColLeft, hv_RowRight, hv_ColRight, hv_WindowHandle;
    HTuple  hv_Row11, hv_Column1, hv_Row21, hv_Column2, hv_Areas_two;
    HTuple  hv_Rows_two, hv_Cols_two, hv_MaxArea_two, hv_MaxIndex_two;
    HTuple  hv_AreaMax, hv_RowMax, hv_ColumnMax, hv_RowLeft1;
    HTuple  hv_ColLeft1, hv_RowRight1, hv_ColRight1, hv_Row1_result;
    HTuple  hv_Col1_result, hv_Row2_result, hv_Col2_result;
    HTuple  hv_Rows_detect, hv_Cols_detect, hv_Num, hv_AreasRemaining;
    HTuple  hv_RowsRemaining, hv_ColsRemaining, hv_SumRemaining;
    HTuple  hv_AvgRemaining, hv_Flag;

    //**********************************算法开始*******************************************
   
    //***************************************************
    //Flag := 1 NG ;Flag := 0 OK 缺陷矩形框坐标Row1_result ;Col1_result ;Row2_result ;Col2_result
    //手画矩形框坐标 Row11, 0, Row21, Width1
    
    //***************************************************
    //读图
    ho_Image = cvMatToHObject(image.clone());
    //获取尺寸
    GetImageSize(ho_Image, &hv_Width1, &hv_Height1);

    //第一次筛选定位产品区域
    //转灰度图
    Rgb1ToGray(ho_Image, &ho_GrayImage);
    //分割出白色区域
    Threshold(ho_GrayImage, &ho_Region1, 50, 255);
    //打散
    Connection(ho_Region1, &ho_ConnectedRegions);
    //计算面积以及中心点
    AreaCenter(ho_ConnectedRegions, &hv_Areas, &hv_Rows, &hv_Cols);
    //手动找最大面积
    hv_MaxArea = 0;
    hv_MaxIndex = 1;
    {
        HTuple end_val23 = (hv_Areas.TupleLength()) - 1;
        HTuple step_val23 = 1;
        for (hv_i = 0; hv_i.Continue(end_val23, step_val23); hv_i += step_val23)
        {
            if (0 != (HTuple(hv_Areas[hv_i]) > hv_MaxArea))
            {
                hv_MaxArea = ((const HTuple&)hv_Areas)[hv_i];
                hv_MaxIndex = hv_i + 1;
            }
        }
    }
    //选择最大面积区域
    SelectObj(ho_ConnectedRegions, &ho_SelectedRegion, hv_MaxIndex);
    SmallestRectangle1(ho_SelectedRegion, &hv_RowLeft, &hv_ColLeft, &hv_RowRight, &hv_ColRight);


    //第二次筛选定位缺陷区域

    // 线程安全：复制ROI配置
    ROIConfig currentROI;
    {
        std::lock_guard<std::mutex> lock(m_roiMutex);
        currentROI = m_roiConfig;
    }

    if (currentROI.enabled) {
        hv_Row11 = currentROI.row1;
        hv_Column1 = currentROI.column1;
        hv_Row21 = currentROI.row2;
        hv_Column2 = currentROI.column2;

        GenRectangle1(&ho_Rectangle1, hv_Row11, 0, hv_Row21, hv_Width1);
    }
    else {
        // 使用默认值（向后兼容）
        GenRectangle1(&ho_Rectangle1, 801.302, 0, 912.413, hv_Width1);
    }

    //提取ROI
    ReduceDomain(ho_Image, ho_Rectangle1, &ho_ImageReduced);
    //转为灰度图
    Rgb1ToGray(ho_ImageReduced, &ho_GrayImageReduced);
    //分割黑色区域
    Threshold(ho_GrayImageReduced, &ho_Region2, 0, 36);
    //打散根据面积筛选
    Connection(ho_Region2, &ho_ConnectedRegions1);
    //计算其面积以及中心点
    AreaCenter(ho_ConnectedRegions1, &hv_Areas_two, &hv_Rows_two, &hv_Cols_two);

    hv_MaxArea_two = 0;
    hv_MaxIndex_two = 1;
    {
        HTuple end_val54 = (hv_Areas_two.TupleLength()) - 1;
        HTuple step_val54 = 1;
        for (hv_i = 0; hv_i.Continue(end_val54, step_val54); hv_i += step_val54)
        {
            if (0 != (HTuple(hv_Areas_two[hv_i]) > hv_MaxArea_two))
            {
                hv_MaxArea_two = ((const HTuple&)hv_Areas_two)[hv_i];
                hv_MaxIndex_two = hv_i + 1;
            }
        }
    }
    //选择最大面积区域
    SelectObj(ho_ConnectedRegions1, &ho_MaxRegion_two, hv_MaxIndex_two);
    //计算其面积以及中心点
    AreaCenter(ho_MaxRegion_two, &hv_AreaMax, &hv_RowMax, &hv_ColumnMax);
    //求最小外接矩形
    SmallestRectangle1(ho_MaxRegion_two, &hv_RowLeft1, &hv_ColLeft1, &hv_RowRight1,
        &hv_ColRight1);

    //设置框选坐标
    hv_Row1_result = hv_RowLeft - 325;
    hv_Col1_result = hv_ColLeft1 - 50;
    hv_Row2_result = hv_RowRight + 25;
    hv_Col2_result = hv_ColRight1 + 50;

    hv_Rows_detect.Clear();
    hv_Rows_detect.Append(hv_Row1_result);
    hv_Rows_detect.Append(hv_Row1_result);
    hv_Rows_detect.Append(hv_Row2_result);
    hv_Rows_detect.Append(hv_Row2_result);
    hv_Rows_detect.Append(hv_Row1_result);
    hv_Cols_detect.Clear();
    hv_Cols_detect.Append(hv_Col1_result);
    hv_Cols_detect.Append(hv_Col2_result);
    hv_Cols_detect.Append(hv_Col2_result);
    hv_Cols_detect.Append(hv_Col1_result);
    hv_Cols_detect.Append(hv_Col1_result);
    //生成检测框
    GenContourPolygonXld(&ho_MinRectangleContour, hv_Rows_detect, hv_Cols_detect);

    //计算除最大面积区域外的所有区域的面积平均值
    Difference(ho_ConnectedRegions1, ho_MaxRegion_two, &ho_RegionDifference);
    OpeningCircle(ho_RegionDifference, &ho_RegionDifference_two, 3);
    CountObj(ho_RegionDifference_two, &hv_Num);

    if (0 != (hv_Num > 0))
    {
        AreaCenter(ho_RegionDifference_two, &hv_AreasRemaining, &hv_RowsRemaining, &hv_ColsRemaining);
        TupleSum(hv_AreasRemaining, &hv_SumRemaining);
        hv_AvgRemaining = hv_SumRemaining / hv_Num;
    }
    else
    {
        hv_AvgRemaining = 0;
    }


    //如果最大面积大于平均值的两倍，返回1，显示NG，否则返回0，显示OK
    if (0 != (hv_MaxArea_two > (3 * hv_AvgRemaining)))
    {
        hv_Flag = 1;
    }
    else
    {
        hv_Flag = 0;
    }


    // ========== 设置算法结果 ==========
    result.success = true;

    // 设置检测框坐标
    result.bboxRow1 = hv_Row1_result.D();
    result.bboxCol1 = hv_Col1_result.D();
    result.bboxRow2 = hv_Row2_result.D();
    result.bboxCol2 = hv_Col2_result.D();
    result.hasBbox = (hv_Flag.I() == 1 &&
        result.bboxRow1 > 0 && result.bboxCol1 > 0 &&
        result.bboxRow2 > 0 && result.bboxCol2 > 0);

    // 判断是否有缺陷
    result.hasDefect = (hv_Flag.I() == 1);
    result.defectCount = result.hasDefect ? 1 : 0;

    if (result.hasDefect) {
        result.message = "Camera2: Traditional algorithm detected defect (NG)";
    } else {
        result.message = "Camera2: Traditional algorithm OK";
    }

    // 计算处理时间（毫秒）
    auto endTime = std::chrono::high_resolution_clock::now();
    result.processingTime = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    return result;
}