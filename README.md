# MetalDefectVision

> 金属带材表面缺陷在线检测系统(C++ / Qt / Halcon / TensorRT)

一套部署于工业产线的金属带材(金属片)表面缺陷视觉检测软件:3 台海康工业相机硬触发同步采图,**传统视觉算法 + YOLO-TensorRT 推理**双引擎并行判定 OK/NG,判定结果通过**相机 IO 频闪 + PLC Modbus TCP** 输出分拣信号,同时完成缺陷标注、存图、SQLite 落库与统计报表。采图→信号输出全链路 **< 200 ms**。

---

## 功能特性

- **多相机同步采集** — 3 台海康 GigE 工业相机,硬触发同步采图,按序列号自动绑定
- **双算法引擎**
  - 传统算法(Halcon 23.11):带材宽度/间距测量、暗区缺陷面积比对
  - AI 算法(YOLO + TensorRT 8.6):上/下表面缺陷检测,支持按料号切换模型
- **缺陷类别**:凹陷(aoxian)、划痕(huahen)、黑点(heidian)、污渍(wuzi)、缺pin 等
- **实时分拣输出** — OK/NG 经相机数字 IO 频闪输出至 PLC;Modbus TCP 心跳(D700)+ 结果寄存器(D701–D704),断线自动重连
- **多线程流水线** — 采集→算法→分发三级流水线 + 共享 UI/IO 线程池,算法线程最高优先级,队列自旋等待减少切换开销
- **ROI / 阈值可视化配置** — 每相机最多 20 个用户集(料号配方),ROI 绘制精度 0.01 mm,AI 检出框按类别+尺寸双阈值过滤
- **数据留存** — 缺陷图按日期/料号归档(jpg 压缩可选),检测记录 SQLite 落库,按日/周统计、分页查询、导出
- **权限体系** — admin / operator 双角色登录,SHA-256 口令哈希,参数修改按角色放行
- **测试模式** — 无相机环境下循环读取本地图片调试整条流水线

## 系统架构

```
 CameraHK1 ×3 (海康 MVS SDK, GigE, 硬触发)
     │ GetImage(frameNum)
     ▼
 CaptureThread ×3 ──ImageTask──▶ ThreadSafeQueue<ImageTask> ×3
                                        │
                                        ▼
                          AlgorithmThread ×3   ← 检测线程优先级: 最高
                          ├─ Cam1: HalconAlgorithm   (传统: 间距测量)
                          ├─ Cam2: YOLO Up_metal     (AI: 上表面)
                          └─ Cam3: YOLO Down_metal   (AI: 下表面)
                          │  判定完成立即输出 OK/NG
                          │   ├─ 相机 IO 频闪 (Line1=NG / Line2=OK)
                          │   └─ Modbus TCP 写寄存器 (备援路径)
                          ▼
                  ThreadSafeQueue<DispatchTask> ×3
                                        │
                    DispatchThread ×3 ──┤ QPainter 标注(中文/尺寸/距离线)
                          │             │ 按尺寸阈值过滤检出框
                          │   缩放至高800px
                          ├────────────▶ m_uiQueue ──▶ UIThread ──▶ Qt 信号刷新界面
                          └────────────▶ m_ioQueue ──▶ IOThread 池 ×3
                                                        ├─ 存图(按日期/料号)
                                                        └─ SQLite 写检测记录

 PLCInterface: 独立监视线程,500ms 心跳(D703 翻转),写合并+批量写,
               断线 1s 自动重连 —— D701~D704 对应 CCD1~4 的 OK/NG
```

各队列均为有界 `ThreadSafeQueue`(自旋 + 条件变量):UI 队列可丢帧保流畅,IO 队列不丢数据保落盘。

## 技术栈

| 组件 | 版本 |
|---|---|
| 语言 / IDE | C++17,Visual Studio 2022 (v143),x64 |
| UI | Qt 6.8.3(msvc2022_64;core / gui / widgets / xml / sql) |
| 传统视觉 | Halcon 23.11 Progress |
| 通用视觉 | OpenCV 4.11.0(opencv_world) |
| AI 推理 | TensorRT 8.6.1.6 + CUDA 11.8 |
| 相机 SDK | 海康 MVS(MvCameraControl) |
| PLC 通讯 | libmodbus(Modbus TCP) |
| 其他 | FreeImage、OpenSSL、QSQLITE |

## 目录结构

```
MetalStripVision/
├── main.cpp                  # 程序入口(工作目录切换/自启动注册)
├── MetalStripVision.*        # 主窗口:持有相机/算法/线程/队列,统计计数
├── CameraHK1.*               # 海康相机封装(枚举/开闭/取图/硬触发/IO频闪)
├── CaptureThread.*           # 采集线程 ×3
├── AlgorithmThread.*         # 算法线程 ×3(调度双引擎 + 分拣信号输出)
├── DispatchThread.*          # 分发线程 ×3(标注绘制/缩放/阈值过滤)
├── UIThread.*                # UI 刷新线程(跨线程 Qt 信号)
├── IOThread.*                # 存图+落盘线程池 ×3
├── ThreadSafeQueue.h         # 有界线程安全队列(自旋+条件变量)
├── ImageTask.h               # 流水线任务结构(ImageTask/Result/Dispatch/...)
├── IAlgorithm.h              # 算法抽象接口
├── HalconAlgorithm.*         # 传统算法(间距测量/暗区比对;含OpenCV移植版)
├── YOLOAlgorithm.*           # TensorRT 推理封装(调用 myAIMODELDLL)
├── AIAlgorithm.h             # AI 算法接口扩展(loadModel/预处理/后处理)
├── AIDefectThreshold.h       # AI 检出框尺寸阈值(类别+宽高区间)
├── AlgorithmROIManager.*     # ROI 用户集管理(料号配方,最多20套/相机)
├── PLC_Interface.*           # Modbus TCP(心跳/写合并/批量写/自动重连)
├── ConfigManager.*           # 配置管理(config/config.xml)
├── DetectionLogDatabase.*    # SQLite 检测记录(多线程独立连接/事务)
├── UserManager.*             # 用户与角色权限
├── LoginDialog / DataStatsDialog / ParameterSettingsDialog / ...
├── config/                   # 运行时配置(见 config/README.md)
├── config_template/          # 默认配置模板
└── Model/
    ├── Tag/                  # 类别标签(行号即 classId)
    └── Trt/                  # TensorRT 引擎(权重不入库,见该目录 README)
```

## 快速开始

### 1. 环境要求

- Windows 10/11 x64,Visual Studio 2022 + Qt VS Tools
- Qt 6.8.x、Halcon 23.11、OpenCV 4.11、TensorRT 8.6 + CUDA 11.8
- 海康 MVS 客户端(相机驱动)
- NVIDIA GPU(推理)

### 2. 编译

1. 用 VS2022 打开 `MetalStripVision.sln`
2. 在属性管理器中配置 Qt 版本(`QtInstall=6.8.3_msvc2022_64`)
3. 修正各库的 include / lib 路径(OpenCV、TensorRT、CUDA、Halcon、MVS)
4. 编译 x64 Release

> TensorRT 推理封装在外部 `myAIMODELDLL.lib`(接口见 `include/yolo_api/AIMODEL.h`),需一并链接。

### 3. 配置

复制 `config_template/` 生成 `config/config.xml`,按现场修改:

- 相机序列号(`<SerialNumber>`,占位值 `CAMERA1_SN` 等需替换为实际 SN)
- PLC 地址(`<PLCIP>`,Modbus TCP 默认端口 502)
- 缺陷类别标签(`Model/Tag/*.txt`,行号即 classId)
- TensorRT 引擎放入 `Model/Trt/`(文件名与料号配置一致)

详细说明见 [MetalStripVision/config/README.md](MetalStripVision/config/README.md) 与
[配置文件说明](MetalStripVision/config/配置文件说明.md)。

### 4. 无相机调试(测试模式)

```xml
<TestMode>
  <TestModeEnabled>true</TestModeEnabled>
  <TestImagePath>你的测试图片目录</TestImagePath>
  <TestLoopImages>true</TestLoopImages>
  <TestImageDelayMs>200</TestImageDelayMs>
</TestMode>
```

将 `test_img/camera1..3/` 结构的图片放入测试目录即可循环跑通整条流水线。

## ⚠️ 部署提醒

- **首次部署请立即修改默认账户口令**(程序内"修改密码",或替换 config.xml 中的 SHA-256 哈希)
- `.trt` 引擎与 GPU 架构、TensorRT 版本绑定,须在目标机生成
- 生产环境建议 `DebugLogEnabled=false`,避免日志膨胀

## 说明

- 本仓库出于商业保密考虑**不包含**:已训练的模型权重(`.trt`)、现场产线数据与真实设备参数(相机序列号、PLC 地址等均已替换为占位值)
- `include/` 下的 opencv、modbus、halcon、yolo_api 等第三方头文件与 DLL(海康 SDK 等)版权归原厂所有,仅为本工程编译所需
- 缺陷类别标签为拼音命名:`aoxian`=凹陷,`huahen`=划痕,`heidian`=黑点,`wuzi`=污渍

---

<div align="center">

**MetalDefectVision** — 3 相机 × 三级流水线 × 双算法引擎的金属表面缺陷在线检测

</div>
