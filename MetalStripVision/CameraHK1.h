#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <opencv.hpp>
#include "MvCameraControl.h"
#include <QTimer>
#include <QDateTime>
#include <QMessageBox>
#include "HalconCpp.h"
using namespace HalconCpp;
using namespace cv;


class CameraHK1
{
public:
	CameraHK1();

	// ��ֹ�����͸�ֵ
	CameraHK1(const CameraHK1&) = delete;
	CameraHK1& operator=(const CameraHK1&) = delete;

	~CameraHK1();
private:
	HObject sImage;
	//vector<HObject>ProductImages12;
	Mat SingleImage;//����ͼƬ
	Mat gray_;  // reused gray buffer, avoid per-frame malloc
	//std::vector<Mat>ProductImages;//һ��ͼƬ
	bool isOpen;
	int nRet;
	void* Handle;			//�豸���ӱ�־
	int CameraType;//��ͼ��ʽ �����ɼ��������ɼ�
	bool missCamera;//������߱�־

	bool GraphSuccess;
	//string CurrentIp;
	//string NetExportIp;
	unsigned char* pData;
	float ExposureTimeNum, GainNum;
	//int CamGrabNum;
	int currentTriggerMode = -1;
	std::string world_time_std = "";	//���������������ʱ��
private:
	MV_CC_DEVICE_INFO_LIST stDeviceList;
	unsigned int SelectCamIndex;
public:
	/************************************************
	*   ���ܣ���ȡ�������ģʽ
	*	������
	*	����ֵ��
	************************************************/
	int getTriggerMode();

	/************************************************
	*   ���ܣ���ȡ�������״̬
	*	������
	*	����ֵ��
	************************************************/
	bool IsOpen();

	/************************************************
	*   ���ܣ�ץͼ
	*	������
	*	����ֵ��1 �ɹ�  -1ʧ��
	************************************************/
	int GraphImage();

	/************************************************
	*   ���ܣ�ֹͣץͼ
	*	������
	*	����ֵ��
	************************************************/
	int StopGraphImage();

	/************************************************
	*   ���ܣ������
	*	������
	*	����ֵ��
	************************************************/
	bool OpenCamera(std::string serial, bool silentMode = false);

	/************************************************
	*   ���ܣ��������
	*	������
	*	����ֵ��
	************************************************/
	bool RestartCamera(std::string serial);

	/************************************************
	*   ���ܣ��ر����
	*	������
	*	����ֵ��
	************************************************/
	bool CloseCamera();

	/************************************************
	*   ���ܣ�����ͼ��
	*	������
	*	����ֵ��
	************************************************/
	Mat GetImage();//����һ��ͼƬ
	bool GetImage(Mat& img, unsigned int& nFrameNum);//����һ��ͼƬ��ʱ���������ȷ�����Ӳ�����Ƿ���ȷ��
	//std::vector<Mat> GetImages();//����һ��ͼƬ
	//HObject GetImage();//����һ��ͼƬ
	//vector<HObject> GetImages(int num);//����һ��ͼƬ

	/************************************************
	*   ���ܣ��������
	*	������
	*	����ֵ��
	************************************************/
	bool isMissCamera();

	/************************************************
	*   ���ܣ������ͼ�ɹ�
	*	������
	*	����ֵ��
	************************************************/
	bool isSuccessGraph();

	/************************************************
	*   ���ܣ�ˢ���������
	*	������
	*	����ֵ��
	************************************************/
	bool UpdateCamera();

	void OutPutResult_OK();

	void OutPutResult_NG();

	void SetTriggerModel(int type);

	void DisableStrobe();  // 关闭Strobe，防止干扰硬触发

	void SetupHardTriggerSync();//Ӳ����ͬ��ר��

	int TriggerSoftware();

	//int SavelmageToFile(void* handle, unsigned char* p_BufAddr, unsigned short n_Width, unsigned short n_Height, MvGvspPixelType en_PixelType, unsigned int n_FrameLen);
	//int SaveImageToFile(void* handle, unsigned char* p_BufAddr, unsigned short n_Width, unsigned short n_Height, enum MvGvspPixelType en_PixelType, unsigned int n_FrameLen);
	unsigned char* pImage;
	cv::Mat HImageToMat(HalconCpp::HObject& H_img);
public:
	HANDLE hCameraThread;//��ͼ�߳�

	unsigned int lastnFrameNum = 0;  // ���һ�����յ����ʱ���
	/************************************************
	*   ���ܣ�Ϊ�����ͼ���������̣߳�����ϵͳֹͣ�������ͼ��ͻ�����⡣
	* ����ʵ��˼·���£�
	* �����µ��̣߳�����ͼ��ӿڲɼ�ͼ��ֹͣ�ɼ�ʱ�����̣߳���ʼ�ɼ�ʱ�����̡߳�
	************************************************/
	void ClearBuff();

	std::string GetCameraWorldTimeStd();
};

