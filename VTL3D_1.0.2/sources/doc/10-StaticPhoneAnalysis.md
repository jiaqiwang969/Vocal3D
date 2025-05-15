# StaticPhone - 静态音素序列生成器分析

本文档分析 `StaticPhone` 类（主要基于 `StaticPhone.cpp` 和 `StaticPhone.h`）在 VocalTractLab3D 项目中的作用和功能。该类是 `TubeSequence` 接口的一个具体实现，专门用于生成静态音素的序列数据。

## 概述

`StaticPhone` 类旨在为语音合成提供一种机制，用于生成声道形状在一段时间内保持不变（或仅有非常缓慢变化）的音素，例如持续发音的元音或某些摩擦音。它实现了 `TubeSequence` 接口，因此可以被时域合成引擎（如 `TdsModel`）用来获取逐样本的声道几何和声源信息。

## 主要功能与特性

根据 `StaticPhone.cpp` 的实现，该类的关键功能包括：

1.  **实现 `TubeSequence` 接口**: `StaticPhone` 提供了 `TubeSequence` 纯虚基类所要求的所有方法的具体实现，包括 `getTube`, `getFlowSource`, `getPressureSource`, `resetSequence`, `incPos`, `getDuration_pt`, 和 `getPos_pt`。

2.  **静态声道配置与声门模型**: 
    *   类内部持有一个 `Tube` 对象 (`this->tube`)，代表一个固定的声道管模型（area function）。
    *   它还持有一个指向 `Glottis` 对象的指针 (`this->glottis`)，代表所使用的声门模型。在默认构造函数中，会创建一个 `GeometricGlottis` 作为默认声门模型。
    *   通过 `setup(const Tube &tube, Glottis *glottis, const int duration_samples)` 方法，可以从外部传入一个特定的声道管型 `Tube` 和一个具体的 `Glottis` 实例（如 `TwoMassModel`, `TriangularGlottis` 等），以及期望的合成时长。

3.  **声源参数的准静态处理**: 
    *   尽管声道的主体形状是静态的，但驱动声门模型的声源参数（如基频 F0 和声门下压）通常具有简单的时变包络。
    *   这些包络通过 `TimeFunction` 对象（`pressureTimeFunction` 用于声门下压，`f0TimeFunction` 用于基频）在 `setup()` 方法中定义。它们通常在音素的起始和结束阶段提供平滑的上升和下降过渡，而在中间部分保持目标值稳定。
    *   用户可以选择使用（`useConstantF0 = true`）或不使用（`useConstantF0 = false`）F0的时间包络，如果选择使用恒定F0，则直接采用 `glottis` 对象中控制参数设定的F0值。

4.  **`getTube(Tube &tube)` 方法实现**: 
    *   此方法是 `TubeSequence` 接口的核心之一。在每个时间步被调用时，它的主要职责是提供当前时刻的完整声道管模型。
    *   它首先将内部存储的静态声道管型 (`*this->tube`) 复制给输出参数 `tube`。
    *   然后，它会根据当前的时间点 (`pos`) 和预设的 `pressureTimeFunction` 及 `f0TimeFunction`（或 `constantF0`），来更新其内部 `glottis` 对象的F0和压力控制参数。
    *   接着，调用 `glottis->calcGeometry()` 来计算当前时刻声门部分的几何形状（如开口面积）。
    *   最后，通过 `glottis->getTubeData()` 获取计算出的动态声门管段的长度和面积，并使用 `tube.setGlottisGeometry(...)` 将这些参数更新到输出 `Tube` 对象的声门部分。同时，还会设置送气强度 (`tube.setAspirationStrength(...)`)。

5.  **声源类型**: 
    *   `getFlowSource()`: 此方法总是返回无效的流速源（通过将 `section` 设置为 -1 实现）。
    *   `getPressureSource()`: 此方法返回一个位于气管第一段 (`Tube::FIRST_TRACHEA_SECTION`) 的压力声源，其幅值由 `pressureTimeFunction` 根据当前时间动态决定。这表明 `StaticPhone` 设计为与压力驱动的声门模型配合使用。

6.  **序列状态管理**:
    *   `resetSequence()`: 将内部的时间位置计数器 `pos` 置零，并调用内部 `glottis` 对象的 `resetMotion()` 方法来重置声门模型的动力学状态。
    *   `incPos(const double pressure_dPa[])`: 将时间位置 `pos` 递增。同时，调用内部 `glottis` 对象的 `incTime()` 方法，将当前的声门上下游压力 `pressure_dPa` 传递给声门模型，以驱动其计算下一个时间步的动态状态。

## 与 `Acoustic3dSimulation` 的关系

`StaticPhone` 作为 `TubeSequence` 接口的一个具体实现，其主要目的是为时域合成模型 (`TdsModel`) 提供逐样本的声道管几何和声源信息。它与 `Acoustic3dSimulation` 的直接关系较弱：

*   **服务于 `TdsModel`**: `TdsModel` 在进行时域仿真时，通过 `TubeSequence` 接口调用 `StaticPhone` 的方法来获取每一时刻的声道配置（包括动态更新的声门部分）和压力源信息。
*   **`Acoustic3dSimulation` 的独立性**: `Acoustic3dSimulation` 通常在频域或模式域工作，计算声道的整体传递特性。如果需要合成静态音素，它会获取一个固定的 `VocalTract` 配置，计算其传递函数，然后与一个独立的声源模型（如 `LfPulse` 或某个 `Glottis` 派生类产生的完整波形）在时域进行卷积或在频域相乘。这个过程不直接需要 `StaticPhone` 提供的逐样本 `Tube` 对象更新机制。

## 结论

`StaticPhone` 类为VocalTractLab3D提供了一种生成具有静态声道形状和准静态声源参数的音素序列的方法，主要适配于驱动 `TdsModel` 进行时域语音合成。

在一个以 `Acoustic3dSimulation` 为核心并进行简化的系统中：
*   如果 `TdsModel` 被保留（例如用于某些特定类型的合成或分析），并且需要合成静态音素的功能，那么 `StaticPhone` 是有用的。
*   如果所有合成都将通过 `Acoustic3dSimulation` 及其配合的独立声源模型（如 `LfPulse` 或 `TwoMassModel` 直接生成波形）完成，那么 `StaticPhone` 对于 `Acoustic3dSimulation` 的核心流程而言就不是直接必需的。

用户已决定暂时保留 `TdsModel`，因此 `StaticPhone` 作为其驱动方式之一也将被暂时保留。 