# TensorRT 模型权重目录

此目录用于存放 TensorRT 序列化后的推理引擎文件：

```
Model/
├── Tag/
│   ├── Up_metal.txt      # 上表面缺陷类别标签
│   └── Down_metal.txt    # 下表面缺陷类别标签
└── Trt/
    ├── Up_metal.trt      # 上表面检测模型（需自行提供）
    └── Down_metal.trt    # 下表面检测模型（需自行提供）
```

## 说明

出于商业保密考虑，**本仓库不包含已训练的 `.trt` 权重文件**。

权重文件需在部署机上按以下步骤生成：

1. 使用自己的缺陷数据集训练 YOLO 检测模型（类别需与 `Model/Tag/`
   中的标签文件保持一致，每行一个类别名，行号即 classId）；
2. 使用 `trtexec` 或 TensorRT API 将 `.onnx` 导出为 `.trt` 引擎：

   ```bash
   trtexec --onnx=Up_metal.onnx --saveEngine=Up_metal.trt --fp16
   ```

3. 将生成的 `.trt` 文件放入本目录，文件名需与配置保持一致。

> `.trt` 引擎与 GPU 架构和 TensorRT 版本绑定，需在目标机上生成。
