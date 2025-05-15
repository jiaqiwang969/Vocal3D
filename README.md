# VocalTractLab3D - 简化版 (Simplified Version)

本项目是对原始 VocalTractLab3D (VTL3D) 的一个修改版本，主要目的是**大幅简化图形用户界面 (GUI)** 和**移除不常用的复杂功能模块**，使其更轻量、更专注于核心的声学仿真与分析，特别是围绕 `Acoustic3dSimulation` 模块。

## 修改意图

原始的 VocalTractLab3D 提供了非常全面和强大的功能，但对于某些特定用户或用例，其复杂的GUI和众多的高级特性可能并非必需。本项目的修改旨在：

1.  **简化用户界面**：移除不常用的对话框、菜单项、工具栏按钮和快捷键。
2.  **减少资源占用**：通过移除重量级的3D可视化组件和部分计算密集型但非核心的功能模块。
3.  **聚焦核心功能**：保留并可能增强围绕 `Acoustic3dSimulation` 的基础声学参数调整、仿真运行及必要的分析与输出。
4.  **提高可维护性（潜在）**：通过精简代码库，降低后续维护和二次开发的复杂度。

## 已移除/计划移除的主要功能模块

为了达到上述目标，当前版本已通过注释相关代码的方式，进行了以下主要修改和移除计划：

1.  **UI层面简化**:
    *   **移除了大部分键盘快捷键**：简化了输入交互。
    *   **移除了 `VocalTractDialog` (3D声道模型可视化对话框)**：这是最主要的界面简化，相关的菜单项和依赖此对话框的导出功能已受限或失效。
    *   **移除了 `AnatomyParamsDialog`**: 用于高级解剖参数调整的对话框及其后端逻辑 (`Backend/AnatomyParams.h/.cpp`) 已被移除，其在 `MainWindow` 和 `Data` 类中的依赖也已处理。
    *   **移除了 `AnnotationDialog` 的核心功能**: 由于其依赖的 `SegmentSequence` 被移除，此对话框的主要功能已失效，计划从构建系统中移除其编译。

2.  **核心逻辑与数据模型简化**:
    *   **移除了 `GesturalScore` (姿态乐谱) 功能**: 所有与姿态乐谱加载、保存、编辑、合成、转换相关的UI、API (`VocalTractLabApi`) 及后端逻辑 (`Backend/GesturalScore.h/.cpp`, `Data` 类中的相关成员) 均已移除或注释。
    *   **移除了 `SegmentSequence` (音段序列) 功能**: 作为 `GesturalScore` 的主要前端，其UI、API及后端逻辑 (`Backend/SegmentSequence.h/.cpp`, `Data` 类中的相关成员) 也已移除或注释。
    *   **移除了 `PoleZeroPlan` 功能 (计划中)**: `Backend/PoleZeroPlan.h/.cpp` 定义的基于极零点模型的声道分析/合成路径，与 `Acoustic3dSimulation` 目标不直接相关，计划移除。 `Data.h` 中对其的包含和成员变量已注释。

## 正在进行的后端依赖分析与决策

目前正在通过迭代编译的方式，梳理 `Data.h` 及核心仿真组件对 `Backend` 目录下各模块的依赖，目标是仅保留与 `Acoustic3dSimulation` 核心流程直接相关或必要的支持模块。

*   **`VocalTract.h/.cpp`**: 核心声道几何模型。虽然其许多参数化和高级功能可能对于纯CSV驱动的 `Acoustic3dSimulation` 是冗余的，但由于 `Acoustic3dSimulation` 目前的接口仍需 `VocalTract*` 对象，故暂时完整恢复。未来的计划是根据 `Acoustic3dSimulation` 对CSV的直接处理能力，大幅简化此类，仅保留必要的数据结构和接口。相关算法分析已存至 `doc/01-VocalTractGeometryAlgorithms.md`。
*   **必要的底层 `Backend` 组件 (已恢复)**: 
    *   `Surface.h/.cpp` 和 `Splines.h/.cpp`: `VocalTract` 的核心几何计算依赖。
    *   `Signal.h/.cpp`: 基础信号处理数据结构。
    *   `IirFilter.h/.cpp`: IIR滤波器，用于信号处理。
    *   `SoundLib.h/.cpp`: 跨平台音频I/O，用于播放合成结果。
*   **声源模型 (`Glottis.h`及其派生类如 `TwoMassModel.h/.cpp`, `TriangularGlottis.h/.cpp`, `GeometricGlottis.h/.cpp`, 以及 `LfPulse.h/.cpp`) (已恢复/计划恢复)**: 对于从 `Acoustic3dSimulation` 结果合成可听语音是必需的，暂时保留。
*   **时域合成与分析组件 (暂时保留，待进一步评估)**:
    *   `TdsModel.h/.cpp`: 时域声道模型。
    *   `TubeSequence.h` (接口) 及其实现 (`StaticPhone.h/.cpp`, `VowelLf.h/.cpp`, `ImpulseExcitation.h/.cpp`): 主要服务于 `TdsModel`。
    由于用户表示时域功能可能仍有用，这些暂时恢复以解决编译依赖，未来可能根据对 `Acoustic3dSimulation` 的聚焦程度再做精简。
*   **信号分析工具 (已恢复)**:
    *   `F0EstimatorYin.h/.cpp` 和 `VoiceQualityEstimator.h/.cpp`: 用于F0和音质分析。用户决定暂时保留这些分析能力。

## 后续步骤

1.  **解决当前编译错误**: 继续恢复或移除 `Backend` 依赖，直到项目能够围绕 `Acoustic3dSimulation` 核心功能成功编译。
2.  **CSV几何导入流程分析**: 深入研究 `Acoustic3dSimulation` 如何从CSV加载几何数据，以及在此流程中对 `VocalTract` 对象的实际依赖程度。
3.  **`VocalTract` 精简**: 基于上述分析，大幅简化 `VocalTract` 类。
4.  **构建系统最终清理**: 从 `CMakeLists.txt` 或 `Makefile` 中彻底移除所有不再编译的 `.cpp` 文件。
5.  **菜单栏和残留UI清理**: 移除 `MainWindow.cpp` 中 `initWidgets` 函数内与已失效功能相关的菜单项代码。
6.  **功能测试与文档更新**。

## 如何编译和使用（占位）

（编译和使用说明待项目稳定后补充。）

---
*详细的模块分析记录在 `doc/` 目录下，规则文件 `.cursor/rules/module_removal_guidelines.mdc` 记录了决策过程。*
