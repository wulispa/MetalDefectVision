# 配置文件夹

本文件夹包含MetalStripVision程序的所有配置文件。

## 文件说明

### config.xml
**主配置文件** - 包含以下配置：
- 📷 相机配置（序列号、算法类型、触发模式）
- 💾 图片保存配置（保存路径、日期文件夹）
- 🧪 测试模式配置（无相机调试）
- 📋 系统日志配置（日志级别控制）
- 🔌 PLC通信配置（Modbus TCP）
- 👤 用户配置（管理员和操作员账户）

**默认用户：**
- 管理员：`admin` / `admin`
- 操作员：`operator` / `123`

### algorithm_roi_profiles.xml
**ROI配置文件** - 定义传统算法的ROI区域参数

### defect_colors.xml
**缺陷颜色配置** - 定义不同缺陷类型的显示颜色

**颜色对应关系：**
- classId=0 → Model/Tag文件第1行
- classId=1 → Model/Tag文件第2行
- 依此类推...

---

## config.xml 配置项详解

### 相机配置 (Cameras)

| 配置项 | 类型 | 说明 |
|--------|------|------|
| `SerialNumber` | 字符串 | 相机序列号，用于识别相机 |
| `AlgorithmType` | 字符串 | `Traditional`(传统算法) 或 `AI`(AI算法) |
| `TriggerMode` | 整数 | `0`=硬触发, `1`=软触发 |
| `Enabled` | 布尔 | 是否启用该相机 |
| `OriginalFormat` | 字符串 | 相机原始格式：`bmp`, `tiff` |

```xml
<Camera id="1">
  <SerialNumber>CAMERA1_SN</SerialNumber>
  <AlgorithmType>Traditional</AlgorithmType>
  <TriggerMode>1</TriggerMode>
  <Enabled>true</Enabled>
  <OriginalFormat>bmp</OriginalFormat>
</Camera>
```

### 测试模式配置 (TestMode)

| 配置项 | 类型 | 说明 |
|--------|------|------|
| `TestModeEnabled` | 布尔 | 是否启用测试模式（无相机时使用图片） |
| `TestImagePath` | 字符串 | 测试图片文件夹路径 |
| `TestLoopImages` | 布尔 | 是否循环读取测试图片 |
| `TestImageDelayMs` | 整数 | 测试图片读取间隔（毫秒） |

```xml
<TestMode>
  <TestModeEnabled>true</TestModeEnabled>
  <TestImagePath>E:\C_Project\MetalStripVision\test_img</TestImagePath>
  <TestLoopImages>true</TestLoopImages>
  <TestImageDelayMs>1000</TestImageDelayMs>
</TestMode>
```

### 系统日志配置 (SystemLog) ⭐ 重要

| 配置项 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `SystemLogEnabled` | 布尔 | true | 是否启用系统日志 |
| `DebugLogEnabled` | 布尔 | false | 是否启用DEBUG级别日志 |

**DebugLogEnabled 说明：**
- `false`（默认，推荐生产环境）：只记录重要日志（启动、停止、错误、警告）
- `true`（调试用）：记录详细日志，包括：
  - 每帧检测结果（OK/NG）
  - ROI更新信息
  - 图像处理详情
  - 颜色配置加载
  - 帧捕获信息

```xml
<SystemLog>
  <SystemLogEnabled>true</SystemLogEnabled>
  <DebugLogEnabled>false</DebugLogEnabled>
</SystemLog>
```

### PLC通信配置 (PLC)

| 配置项 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `PLCEnabled` | 布尔 | true | 是否启用PLC通信 |
| `PLCIP` | 字符串 | 192.168.1.10 | PLC设备IP地址 |
| `PLCPort` | 整数 | 502 | Modbus TCP端口 |
| `PLCSlaveId` | 整数 | 4 | Modbus从站ID |

```xml
<PLC>
  <PLCEnabled>true</PLCEnabled>
  <PLCIP>192.168.1.10</PLCIP>
  <PLCPort>502</PLCPort>
  <PLCSlaveId>4</PLCSlaveId>
</PLC>
```

### 图片保存配置 (ImageSave)

| 配置项 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `SavePath` | 字符串 | save_img_file/ | 图片保存路径 |
| `EnableDateFolder` | 布尔 | true | 是否按日期创建子文件夹 |
| `SaveImageEnabled` | 布尔 | false | 是否保存检测图片 |
| `SaveAsCompressed` | 布尔 | true | `true`=JPEG压缩, `false`=原图 |

---

## 快速配置指南

### 修改相机
1. 用文本编辑器打开 `config.xml`
2. 找到 `<Cameras>` 部分
3. 修改对应相机的 `serialNumber`
4. 保存并重启程序

### 修改颜色
1. 用文本编辑器打开 `defect_colors.xml`
2. 修改对应classId的 `r`, `g`, `b` 值（0-255）
3. 保存并重启程序

### 修改密码
**方法1（推荐）：** 通过程序界面
1. 启动程序并登录
2. 点击"修改密码"按钮

**方法2：** 手动修改
1. 打开 `config.xml`
2. 找到对应用户的 `<PasswordHash>`
3. 用SHA256哈希替换
4. 重启程序

### 开启详细日志（调试用）
```xml
<SystemLog>
  <DebugLogEnabled>true</DebugLogEnabled>
</SystemLog>
```
调试完成后记得改回 `false`，避免日志文件过大。

---

## 常见场景配置

### 场景1：首次部署（无相机调试）
```xml
<TestMode>
  <TestModeEnabled>true</TestModeEnabled>
  <TestImagePath>你的测试图片路径</TestImagePath>
</TestMode>
<SystemLog>
  <DebugLogEnabled>true</DebugLogEnabled>
</SystemLog>
```

### 场景2：生产环境
```xml
<TestMode>
  <TestModeEnabled>false</TestModeEnabled>
</TestMode>
<SystemLog>
  <DebugLogEnabled>false</DebugLogEnabled>
</SystemLog>
```

### 场景3：现场调试
```xml
<SystemLog>
  <DebugLogEnabled>true</DebugLogEnabled>
</SystemLog>
```
调试完成后改回 `false`。

---

## 文件格式

所有XML文件使用UTF-8编码，建议使用支持UTF-8的编辑器：
- Windows: Notepad++, VS Code
- Linux: vim, nano, gedit
- macOS: TextMate, VS Code

## 备份建议

定期备份此文件夹，特别是在修改配置之前：
```bash
# 备份整个config文件夹
cp -r config config_backup_$(date +%Y%m%d)
```

## 详细文档

- 📖 [配置文件说明.md](配置文件说明.md) - 详细的配置指南
- 🎨 [缺陷颜色配置说明.md](../缺陷颜色配置说明.md) - 颜色配置详解
- 🤖 [YOLO使用说明.md](../YOLO使用说明.md) - AI模型配置
- 🔐 [用户登录系统](#) - 用户权限管理

## 故障排除

### 程序无法启动
- 检查 `config.xml` 格式是否正确
- 查看日志文件 `log_YYYY_MM_DD.txt`

### 颜色不显示
- 检查 `defect_colors.xml` 是否存在
- 确认classId与Tag文件行号对应正确

### 找不到配置文件
- 确保 `config/` 文件夹在程序目录下
- 检查文件权限

### 日志文件太大
- 将 `DebugLogEnabled` 设为 `false`
- 定期清理 `Log/system_log/` 文件夹

## 技术支持

如遇到问题，请：
1. 查看 `Log/system_log/` 日志文件
2. 检查配置文件格式是否正确
3. 参考相关文档
4. 联系技术支持

---

**最后更新：** 2026-03-27
