#include "CameraHK1.h"
#include "Logger.h"
#include <chrono>


CameraHK1::CameraHK1()
	: Handle(nullptr),
	nRet(MV_OK),
	isOpen(false),
	missCamera(false),
	GraphSuccess(false),
  	//ExposureTimeNum(10000),
	GainNum(24),
	currentTriggerMode(-1),
	SelectCamIndex(0)
{
}

//CameraHK1::CameraHK1()
//	: CameraHK1()   // 复用默认构造函数
//{
//}

CameraHK1::~CameraHK1()
{
	CloseCamera();
}

bool CameraHK1::IsOpen()
{
	return isOpen;
}
int CameraHK1::getTriggerMode() {
	return currentTriggerMode;
}

int CameraHK1::TriggerSoftware() {
	if (Handle == nullptr) return -1;
	return MV_CC_SetCommandValue(Handle, "TriggerSoftware");
}

int CameraHK1::GraphImage()
{
	if (!Handle) {
		missCamera = true;
		GraphSuccess = false;
		return -1;
	}

	missCamera = false;
	GraphSuccess = false;

	MV_FRAME_OUT stOutFrame = {0};
	auto t_bufStart = std::chrono::high_resolution_clock::now();
	nRet = MV_CC_GetImageBuffer(Handle, &stOutFrame, 0);
	auto t_bufEnd = std::chrono::high_resolution_clock::now();

	if (nRet != MV_OK) {
		missCamera = true;
		GraphSuccess = false;
		return -1;
	}


	try {
		int W = stOutFrame.stFrameInfo.nWidth;
		int H = stOutFrame.stFrameInfo.nHeight;
		void* pSrcData = stOutFrame.pBufAddr;

		if (!pSrcData) {
			MV_CC_FreeImageBuffer(Handle, &stOutFrame);
			missCamera = true;
			return -1;
		}

		lastnFrameNum = stOutFrame.stFrameInfo.nFrameNum;

		// 1. memcpy (reuse gray_, skip create when size unchanged)
		auto t_memcpyStart = std::chrono::high_resolution_clock::now();
		if (gray_.rows != H || gray_.cols != W) {
			gray_.create(H, W, CV_8UC1);
		}
		memcpy(gray_.data, pSrcData, W * H);
		auto t_memcpyEnd = std::chrono::high_resolution_clock::now();

		// 2. free buffer immediately
		MV_CC_FreeImageBuffer(Handle, &stOutFrame);

		// 3. store gray directly
		SingleImage = gray_;
		auto t_freeEnd = std::chrono::high_resolution_clock::now();

		// 时间戳放到memcpy之后（避免格式化延迟采图）
		QString timeStr = QDateTime::currentDateTime()
			.toString("yyyyMMdd_hh_mm_ss_zzz");
		world_time_std = timeStr.toStdString();

		// timing log
		double bufMs = std::chrono::duration<double, std::milli>(t_bufEnd - t_bufStart).count();
		double memcpyMs = std::chrono::duration<double, std::milli>(t_memcpyEnd - t_memcpyStart).count();
		Logger::instance().debug(QString("[CAP_DETAIL] GetBuf=%1ms memcpy=%2ms")
			.arg(bufMs, 0, 'f', 1).arg(memcpyMs, 0, 'f', 1).toStdString());

		GraphSuccess = true;
	}
	catch (...) {
		GraphSuccess = false;
		missCamera = true;
		MV_CC_FreeImageBuffer(Handle, &stOutFrame);
	}

	return GraphSuccess ? 1 : -1;
}



std::string CameraHK1::GetCameraWorldTimeStd()
{
	return world_time_std;
}

int CameraHK1::StopGraphImage()
{
	if (!Handle) return -1;
	return MV_CC_StopGrabbing(Handle);
}

bool CameraHK1::OpenCamera(std::string serial, bool silentMode)
{
	try {
		int res;
		CloseCamera();

		// ch:枚举设备 | en:Enum device
		memset(&stDeviceList, 0, sizeof(MV_CC_DEVICE_INFO_LIST));
		nRet = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &stDeviceList);
		if (MV_OK != nRet)
		{
			if (!silentMode) {
				QMessageBox::information(nullptr, "Warning", "FIND NO DEVICE");
			}
			return false;
		}
		if (stDeviceList.nDeviceNum > 0)
		{
			bool found = false;
			for (unsigned int i = 0; i < stDeviceList.nDeviceNum; i++)
			{
				MV_CC_DEVICE_INFO* pDeviceInfo = stDeviceList.pDeviceInfo[i];
				if (pDeviceInfo == NULL) continue;

				//需要根据相机连接方式设置读取的数组
				//std::string camId = (char*)pDeviceInfo->SpecialInfo.stGigEInfo.chSerialNumber;
				std::string camId = (char*)pDeviceInfo->SpecialInfo.stUsb3VInfo.chSerialNumber;
				if (camId == serial) {
					SelectCamIndex = i;
					found = true;
					break;
				}
			}
			if (!found) {
				if (!silentMode) {
					QMessageBox::warning(nullptr, "警告", QString("无法找到序列号为 %1 的相机").arg(QString::fromStdString(serial)));
				}
				return false;
			}
			res = MV_CC_CreateHandle(&Handle, stDeviceList.pDeviceInfo[SelectCamIndex]);
			if (res != MV_OK) {
				if (!silentMode) {
					QMessageBox::warning(nullptr, "错误", QString("创建相机句柄失败，错误码: %1").arg(res));
				}
				return false;
			}

			res = MV_CC_OpenDevice(Handle);
			if (res != MV_OK) {
				if (!silentMode) {
					QMessageBox::warning(nullptr, "警告", "相机正在使用或无法打开");
				}
				MV_CC_DestroyHandle(Handle);
				Handle = nullptr;
				isOpen = false;
				return false;  // 重要：打开失败时返回false
			}

			isOpen = true;
			nRet = MV_CC_StartGrabbing(Handle);
			if (nRet != MV_OK) {
				if (!silentMode) {
					QMessageBox::warning(nullptr, "错误", QString("启动抓图失败，错误码: %1").arg(nRet));
				}
				MV_CC_CloseDevice(Handle);
				MV_CC_DestroyHandle(Handle);
				Handle = nullptr;
				isOpen = false;
				return false;
			}

			MV_CC_SetImageNodeNum(Handle, 6);
			MV_CC_ClearImageBuffer(Handle);

			// 配置Line1和Line2为频闪输出模式（用于OK/NG信号输出）
			int DurationValue = 70000, DelayValue = 0, PreDelayValue = 0; // us

			// 配置Line1（用于NG信号）
			nRet = MV_CC_SetEnumValue(Handle, "LineSelector", 1);
			nRet = MV_CC_SetEnumValue(Handle, "LineMode", 8);  // 8=Output
			nRet = MV_CC_SetEnumValue(Handle, "LineSource", 5);  // 5=SoftTriggerActive
			nRet = MV_CC_SetIntValue(Handle, "StrobeLineDuration", DurationValue);
			nRet = MV_CC_SetIntValue(Handle, "StrobeLineDelay", DelayValue);
			nRet = MV_CC_SetIntValue(Handle, "StrobeLinePreDelay", PreDelayValue);
			nRet = MV_CC_SetBoolValue(Handle, "StrobeEnable", TRUE);

			// 配置Line2（用于OK信号）
			nRet = MV_CC_SetEnumValue(Handle, "LineSelector", 2);
			nRet = MV_CC_SetEnumValue(Handle, "LineMode", 8);  // 8=Output
			nRet = MV_CC_SetEnumValue(Handle, "LineSource", 5);  // 5=SoftTriggerActive
			nRet = MV_CC_SetIntValue(Handle, "StrobeLineDuration", DurationValue);
			nRet = MV_CC_SetIntValue(Handle, "StrobeLineDelay", DelayValue);
			nRet = MV_CC_SetIntValue(Handle, "StrobeLinePreDelay", PreDelayValue);
			nRet = MV_CC_SetBoolValue(Handle, "StrobeEnable", TRUE);
		}
		else
		{
			//printf("Find No Devices!\n");

			if (!silentMode) {
				QMessageBox::warning(nullptr, "警告", "未查找到设备");
			}
			return false;
		}
	}
	catch (const std::exception&)
	{
		return	false;
	}
	return true;
}

bool CameraHK1::RestartCamera(std::string serial)
{
	if (isOpen) {
		CloseCamera();
		Sleep(100);
		OpenCamera(serial);
	}
	else {
		Sleep(100);
		OpenCamera(serial);
	}
	return true;
}

bool CameraHK1::CloseCamera()
{
	if (!Handle) {
		isOpen = false;
		return true;
	}

	MV_CC_StopGrabbing(Handle);
	MV_CC_CloseDevice(Handle);
	MV_CC_DestroyHandle(Handle);

	Handle = nullptr;
	isOpen = false;
	return true;
}

Mat CameraHK1::GetImage()
{
	GraphImage();
	if (missCamera) {
		//发送相机离线消息
		return Mat();
	}
	return SingleImage;//单张图片
}

bool CameraHK1::GetImage(Mat& img, unsigned int& nFrameNum)
{
	GraphImage();
	if (missCamera) return false;

	img = SingleImage;
	nFrameNum = lastnFrameNum;
	return true;
}


//std::vector<Mat> CameraHK1::GetImages()
//{
//	std::vector<Mat>().swap(ProductImages);
//	GraphImage();
//	if (missCamera) {
//		//发送相机离线消息
//		return std::vector<Mat>();
//	}
//	return ProductImages;
//}

bool CameraHK1::isMissCamera()
{
	return missCamera;
}

bool CameraHK1::isSuccessGraph()
{
	return GraphSuccess;
}


void CameraHK1::ClearBuff() {

	MV_CC_ClearImageBuffer(Handle);
}

bool CameraHK1::UpdateCamera()
{
	//ExposureTimeNum = atof(ExposureTime.c_str());
	//GainNum = atof(Gain.c_str());
	MV_CC_SetFloatValue(Handle, "ExposureTime", ExposureTimeNum);
	MV_CC_SetFloatValue(Handle, "Gain", GainNum);
	//MV_CC_SetImageNodeNum(Handle, CamGrabNum);

	//MV_CC_SetIntValue(Handle, "AcquisitionBurstFrameCount", CamGrabNum);
	MV_CC_SetFloatValue(Handle, "AcquisitionFrameRate", 88);

	//帧率控制使能，true表示打开，false标识关闭
	//nRet = MV_CC_SetBoolValue(Handle, "AcquisitionFrameRateEnable", true);

	//SetTriggerModel(false);
	return true;
}

void CameraHK1::OutPutResult_OK()
{
	if (!Handle) {
		Logger::instance().error("OutPutResult_OK: Handle is NULL");
		return;
	}

	// 选择Line2并触发（已在初始化时配置为频闪输出）
	nRet = MV_CC_SetEnumValue(Handle, "LineSelector", 2);
	nRet = MV_CC_SetCommandValue(Handle, "LineTriggerSoftware");
	if (nRet != 0) {
		Logger::instance().error("OutPutResult_OK: LineTriggerSoftware failed, nRet=" + std::to_string(nRet));
	}
}

void CameraHK1::OutPutResult_NG()
{
	if (!Handle) {
		Logger::instance().error("OutPutResult_NG: Handle is NULL");
		return;
	}

	// 选择Line1并触发（已在初始化时配置为频闪输出）
	nRet = MV_CC_SetEnumValue(Handle, "LineSelector", 1);
	nRet = MV_CC_SetCommandValue(Handle, "LineTriggerSoftware");
	if (nRet != 0) {
		Logger::instance().error("OutPutResult_NG: LineTriggerSoftware failed, nRet=" + std::to_string(nRet));
	}
}

// type: 0=连续采集, 1=软触发, 2=硬触发
void CameraHK1::SetTriggerModel(int type)
{
	if (!Handle) return;

	if (type == 0) // 连续采集
	{
		currentTriggerMode = 0;
		MV_CC_SetEnumValue(Handle, "TriggerMode", MV_TRIGGER_MODE_OFF);
	}
	else if (type == 1) // 软件触发
	{
		currentTriggerMode = 1;
		MV_CC_SetEnumValue(Handle, "TriggerMode", MV_TRIGGER_MODE_ON);
		MV_CC_SetEnumValue(Handle, "TriggerSource", MV_TRIGGER_SOURCE_SOFTWARE);
	}
	else if (type == 2) // 硬触发
	{
		currentTriggerMode = 2;
		MV_CC_SetEnumValue(Handle, "TriggerMode", MV_TRIGGER_MODE_ON);
		MV_CC_SetEnumValue(Handle, "TriggerSource", MV_TRIGGER_SOURCE_LINE0);
		MV_CC_SetEnumValue(Handle, "TriggerActivation", 1);  // 1=FallingEdge（下降沿触发）
	}
}

// 关闭Strobe，防止干扰硬触发
void CameraHK1::DisableStrobe()
{
	if (!Handle) return;

	// 关闭所有Line的Strobe使能
	for (int line = 0; line <= 2; line++) {
		MV_CC_SetEnumValue(Handle, "LineSelector", line);
		MV_CC_SetBoolValue(Handle, "StrobeEnable", FALSE);
	}
}

// 硬触发同步
void CameraHK1::SetupHardTriggerSync()
{
	// 1. 触发配置
	MV_CC_SetEnumValue(Handle, "TriggerMode", MV_TRIGGER_MODE_ON);
	MV_CC_SetEnumValue(Handle, "TriggerSource", MV_TRIGGER_SOURCE_LINE0);
	MV_CC_SetEnumValue(Handle, "TriggerActivation", 1);  // 1=FallingEdge（下降沿触发）

	// 2. 曝光 / 增益（手动）
	MV_CC_SetEnumValue(Handle, "ExposureAuto", 0); // Off
	MV_CC_SetEnumValue(Handle, "GainAuto", 0);     // Off
	MV_CC_SetFloatValue(Handle, "ExposureTime", ExposureTimeNum);

	// 连续模式下帧率无意义
	MV_CC_SetBoolValue(Handle, "AcquisitionFrameRateEnable", false);

	// 4. 允许触发重叠
	MV_CC_SetEnumValue(Handle, "TriggerOverlap", 1); // On
}



cv::Mat CameraHK1::HImageToMat(HalconCpp::HObject& H_img)
{
	cv::Mat cv_img;
	HalconCpp::HTuple channels, w, h;

	HalconCpp::ConvertImageType(H_img, &H_img, "byte");
	HalconCpp::CountChannels(H_img, &channels);

	if (channels.I() == 1)
	{
		HalconCpp::HTuple pointer;
		GetImagePointer1(H_img, &pointer, nullptr, &w, &h);
		int width = w.I(), height = h.I();
		int size = width * height;
		cv::Mat gray(height, width, CV_8UC1);
		memcpy(gray.data, (void*)(pointer.L()), size);
		// 灰度转BGR，保证后续绘制彩色标注和保存彩色标注图
		cv::cvtColor(gray, cv_img, cv::COLOR_GRAY2BGR);
	}

	else if (channels.I() == 3)
	{
		HalconCpp::HTuple pointerR, pointerG, pointerB;
		HalconCpp::GetImagePointer3(H_img, &pointerR, &pointerG, &pointerB, nullptr, &w, &h);
		int width = w.I(), height = h.I();
		int size = width * height;
		cv_img = cv::Mat::zeros(height, width, CV_8UC3);
		uchar* R = (uchar*)(pointerR.L());
		uchar* G = (uchar*)(pointerG.L());
		uchar* B = (uchar*)(pointerB.L());
		for (int i = 0; i < height; ++i)
		{
			uchar* p = cv_img.ptr<uchar>(i);
			for (int j = 0; j < width; ++j)
			{
				p[3 * j] = B[i * width + j];
				p[3 * j + 1] = G[i * width + j];
				p[3 * j + 2] = R[i * width + j];
			}
		}
	}
	return cv_img;
}
