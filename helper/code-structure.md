# VocalTractLab3D - sources/ 目录文件结构与作用推测

本文档旨在根据 `VTL3D_1.0.2/sources/` 目录下的文件和子目录命名，推测其主要功能，以便更好地理解代码库结构。这些推测基于常见的命名约定和已知的项目信息（如对 wxWidgets, CGAL, Eigen 的依赖）。

## 一、 核心后端逻辑 (`Backend/`)

此目录包含了 VocalTractLab3D 的核心计算引擎和模型，独立于图形用户界面。

*   **`Acoustic3dSimulation.h/.cpp`**: 核心的 **3D 声学模拟**引擎。实现了频域多模态方法，管理声道截面 (`CrossSection2d`)，执行模式计算和波传播模拟。
*   **`VocalTract.h/.cpp`**: **发音器官/声道模型**。处理发音参数，计算声道几何形状（中心线、轮廓、面积函数），加载和保存 `.speaker` 文件。
*   **`CrossSection2d.h/.cpp`**: 定义和处理声道的**二维横截面**。包括使用 CGAL 进行**网格生成 (Meshing)** 和使用 Eigen 进行**有限元模式计算 (FEM)**。
*   **`Constants.h`**: 定义物理常量 (如声速、空气密度)、数学常量和模拟中使用的固定参数。
*   **`Geometry.h/.cpp`**: 提供通用的几何计算函数和数据结构。
*   **`Surface.h/.cpp`**: 定义和处理构成声道壁的表面。
*   **`Tube.h/.cpp`**: 定义基本的管段模型，可能用于 1D 模拟或作为 `VocalTract` 的基础。
*   **`Glottis.h/.cpp`**: 声门模型基类或相关通用功能。
    *   **`GeometricGlottis.h/.cpp`**: 基于几何形状的声门模型。
    *   **`LfPulse.h/.cpp`**: Liljencrants-Fant (LF) 声门脉冲模型。
    *   **`TwoMassModel.h/.cpp`**: Ishizaka-Flanagan 双质量声门模型。
    *   **`TriangularGlottis.h/.cpp`**: 三角形成形的声门模型。
    *   **`VowelLf.h/.cpp`**: 可能包含针对元音优化的 LF 模型参数。
*   **`Synthesizer.h/.cpp`**: **合成器**核心类，协调声道模型、声门模型和声学模拟以生成声音。
*   **`TdsModel.h/.cpp`**, **`TlModel.h/.cpp`**: 可能实现了**时域合成 (TDS)** 模型，如传输线 (TL) 模型（这部分可能来自原始 VTL）。
*   **`Dsp.h/.cpp`**: 包含数字信号处理 (DSP) 的常用函数（如滤波、FFT 等）。
*   **`Signal.h/.cpp`**: 定义信号类，用于存储和操作音频数据或其他时间序列数据。
*   **`IirFilter.h/.cpp`**: IIR（无限脉冲响应）滤波器实现。
*   **`PoleZeroPlan.h/.cpp`**: 处理系统的极点和零点。
*   **`Splines.h/.cpp`**: 实现样条插值功能。
*   **`XmlHelper.h/.cpp`**, **`XmlNode.h/.cpp`**: 用于解析和处理 XML 文件（例如 `.speaker` 文件）。
*   **`AnatomyParams.h/.cpp`**: 管理解剖学相关的参数。
*   **`AudioFile.h`**: 可能用于读写音频文件 (如 WAV)。
*   **`Delaunay_mesh_vertex_base_with_info_2.h`**: CGAL 网格生成所需的自定义顶点基类头文件。
*   **`F0EstimatorYin.h/.cpp`**: YIN 算法实现，用于估计基频 (F0)。
*   **`GesturalScore.h/.cpp`**: 后端对发音动作序列（Gestural Score）的表示和处理。
*   **`ImpulseExcitation.h/.cpp`**: 生成脉冲序列激励源。
*   **`LICENSE`**: Backend 代码的许可证文件 (推测为 GPL)。
*   **`Matrix2x2.h/.cpp`**: 2x2 矩阵的工具类。
*   **`Sampa.h/.cpp`**: 处理 SAMPA 音标格式的工具。
*   **`SegmentSequence.h/.cpp`**, **`TubeSequence.h`**: 表示声道或管段的序列。
*   **`SoundLib.h/.cpp`**: 封装声音播放/录制的库接口。
*   **`StaticPhone.h/.cpp`**: 表示静态音素的数据。
*   **`TimeFunction.h/.cpp`**: 表示随时间变化的函数。
*   **`VocalTractLabApi.h/.cpp`**: 可能为外部程序或脚本提供 API 接口。
*   **`VoiceQualityEstimator.h/.cpp`**: 估计声音质量相关的参数。

## 二、 图形用户界面 (GUI) 组件

这些文件构成了应用程序的图形界面，使用了 wxWidgets 库。

### 2.1 核心应用与主窗口

*   **`Application.h/.cpp`**: 定义 `wxApp` 派生类，是 GUI 应用程序的入口和主循环。
*   **`MainWindow.h/.cpp`**: 定义 `wxFrame` 派生类，是应用程序的主窗口，包含菜单、工具栏，并管理各个功能页面 (Page)。

### 2.2 主要功能页面 (Pages)

*   **`Acoustic3dPage.h/.cpp`**: **3D 声学模拟**功能的主界面，用户在此页面进行 3D 相关的设置、触发计算和查看结果。
*   **`VocalTractPage.h/.cpp`**: **声道模型**的主界面，用于显示声道形状、调整发音参数等。
*   **`SignalPage.h/.cpp`**: **信号处理**界面，用于显示波形、频谱图等合成或加载的信号。
*   **`GesturalScorePage.h/.cpp`**: **发音动作序列 (Gestural Score)** 编辑和显示界面。
*   **`TdsPage.h/.cpp`**: 可能与**时域合成 (TDS)** 模型相关的界面。

### 2.3 设置与交互对话框 (Dialogs)

*   **`ParamSimu3DDialog.h/.cpp`**: **3D 声学模拟参数设置**对话框，是配置 3D 模拟细节的关键界面。
*   **`VocalTractDialog.h/.cpp`**: 显示可交互的 **3D 声道模型**的对话框。
*   **`VocalTractShapesDialog.h/.cpp`**: 管理和选择预定义声道**形状**的对话框。
*   **`AnatomyParamsDialog.h/.cpp`**: 调整**解剖学参数**的对话框。
*   **`GlottisDialog.h/.cpp`**: 配置**声门模型**参数的对话框。
*   **`LfPulseDialog.h/.cpp`**: 配置 **LF 声门脉冲模型**参数的对话框。
*   **`PoleZeroDialog.h/.cpp`**: 查看或编辑**极零点**的对话框。
*   `AnalysisResultsDialog.h/.cpp`: 显示分析结果。
*   `AnalysisSettingsDialog.h/.cpp`: 配置分析设置。
*   `AnnotationDialog.h/.cpp`: 添加注释。
*   `EmaConfigDialog.h/.cpp`: 配置 EMA 数据。
*   `FdsOptionsDialog.h/.cpp`: 频域合成选项？
*   `FormantOptimizationDialog.h/.cpp`: 共振峰优化设置。
*   `PhoneticParamsDialog.h/.cpp`: 音素参数设置。
*   `SpectrumOptionsDialog.h/.cpp`: 频谱显示选项。
*   `TdsOptionsDialog.h/.cpp`: 时域合成选项。
*   `TransitionDialog.h/.cpp`: 定义音/形之间的过渡。

### 2.4 可视化控件 (Pictures & Plots)

这些是自定义的 `wxWindow` 派生类，用于在 GUI 中绘制各种图形和数据。

*   **`VocalTractPicture.h/.cpp`**: 绘制**声道模型**（可能是 2D 视图）。
*   **`PropModesPicture.h/.cpp`**: 可视化计算出的**传播模式**。
*   **`SegmentsPicture.h/.cpp`**: 可视化声道**分段**。
*   **`AreaFunctionPicture.h/.cpp`**: 绘制**面积函数**曲线。
*   **`CrossSectionPicture.h/.cpp`**: 显示**二维横截面**。
*   **`SpectrumPicture.h/.cpp`**: 显示二维**频谱**。
*   **`SpectrogramPicture.h/.cpp`**: 显示**语谱图/声谱图**。
*   **`Spectrum3dPicture.h/.cpp`**: 显示 **3D 频谱**（瀑布图）。
*   **`SignalPicture.h/.cpp`**: 显示通用**信号波形**。
*   **`GlottisPicture.h/.cpp`**: 可视化**声门**相关信号或状态。
*   **`PoleZeroPlot.h/.cpp`**: 绘制**极零点图**。
*   `Graph.h/.cpp`: 通用的 2D 绘图控件基类或实现。
*   `BasicPicture.h/.cpp`: 更基础的绘图控件。
*   `ColorScale.h/.cpp`: 颜色图例控件。
*   `GesturalScorePicture.h/.cpp`: 可视化 Gestural Score。
*   `LfPulsePicture.h/.cpp`: 可视化 LF 脉冲。
*   `SignalComparisonPicture.h/.cpp`: 对比显示信号。
*   `SimpleSpectrumPicture.h/.cpp`: 简化的频谱显示。
*   `SpectrogramPlot.h/.cpp`: 语谱图绘制逻辑。
*   `TableTextPicture.h/.cpp`: 以表格形式显示文本。
*   `TdsSpatialSignalPicture.h/.cpp`: 可视化 TDS 空间信号。
*   `TdsTimeSignalPicture.h/.cpp`: 可视化 TDS 时间信号。
*   `TdsTubePicture.h/.cpp`: 可视化 TDS 管模型。
*   `TimeAxisPicture.h/.cpp`: 绘制时间轴。

### 2.5 其他 GUI 相关

*   **`GlottisPanel.h/.cpp`**: 一个专门用于显示和控制声门参数的面板 (`wxPanel`)。
*   **`Data.h/.cpp`**: 可能用于管理 GUI 层面的共享数据或状态。
*   **`SynthesisThread.h/.cpp`**: 在后台线程执行耗时的音频合成，避免 GUI 卡顿。
*   **`IconsXpm.h`**: 内嵌的 XPM 格式图标数据。
*   **`SilentMessageBox.h/.cpp`**: 自定义的消息提示框。
*   **`resource.h`**: Windows 平台资源文件头。

## 三、 构建与配置

*   **`CMakeLists.txt`**: 用于配置整个 `sources` 目录的编译过程 (生成 `VocalTractLab` 可执行文件)。
*   **`CMakeSettings.json`**: 为 CMake 提供配置信息，常用于 Visual Studio Code 等 IDE。

## 四、 构建产物 (`build/`)

此目录包含编译过程中生成的中间文件（如 `.o` 对象文件、`.d` 依赖文件）和最终的可执行文件。通常可以安全地删除并重新生成。
*   **`log.txt`**: 运行时生成的日志文件。
*   **`VocalTractLab`**: 编译生成的可执行文件。

