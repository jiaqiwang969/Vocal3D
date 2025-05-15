# SoundLib - 音频输入输出库分析

本文档分析 `SoundLib.h` 和 `SoundLib.cpp` 在 VocalTractLab3D 项目中的作用和功能。这些文件共同构成了一个跨平台的音频输入/输出接口及其实现。

## 概述

`SoundLib` 提供了一个抽象层，用于处理底层的音频播放和录制操作，使得上层应用程序代码可以以一种平台无关的方式与音频硬件交互。

### `SoundLib.h`

*   **定义接口 (`SoundInterface`)**: 这是一个纯虚基类，声明了音频操作的标准方法，包括：
    *   `init(int samplingRate)`: 初始化音频系统。
    *   `startPlayingWave(signed short *data, int numSamples, bool loop)`: 开始播放PCM音频数据。
    *   `stopPlaying()`: 停止播放。
    *   `startRecordingWave(signed short *data, int numSamples)`: 开始录音。
    *   `stopRecording()`: 停止录音。
    *   `getInstance()`: 一个静态工厂方法，用于获取具体平台实现的单例对象。
*   **向后兼容的全局函数**: 提供了一组如 `initSound()`, `waveStartPlaying()` 等内联全局函数，它们是对 `SoundInterface::getInstance()->method()` 的简单封装，方便旧代码调用。

### `SoundLib.cpp`

*   **平台特定实现**: 使用条件编译（`#ifdef WIN32`, `#ifdef HAVE_OPENAL`）为不同平台提供具体的音频接口实现：
    *   **Windows (WinMM)**: 如果在Windows平台且未使用OpenAL，则使用Windows Multimedia API (`mmsystem.h`) 实现 `SoundWinMM` 类。
    *   **OpenAL**: 如果定义了 `HAVE_OPENAL`（通常用于Linux, macOS等），则使用OpenAL API 实现 `SoundOpenAL` 类。
*   **单例模式**: `SoundInterface::getInstance()` 静态方法负责根据编译环境创建并返回 `SoundWinMM` 或 `SoundOpenAL` 的单例实例，确保全局只有一个音频接口对象。

## 主要功能与特性

1.  **跨平台音频I/O**: 核心功能是提供一个统一的API来处理不同操作系统下的音频播放和录制。
2.  **PCM数据处理**: 设计用于处理16位单声道PCM音频数据。
3.  **基本播放控制**: 支持开始播放、循环播放、停止播放。
4.  **基本录音控制**: 支持开始录音到缓冲区、停止录音。

## 与 `Acoustic3dSimulation` 的关系

`SoundLib` 对于 `Acoustic3dSimulation` 模块本身（即声学仿真计算过程）不是直接的依赖，但它对于**体验和使用 `Acoustic3dSimulation` 的成果至关重要**：

*   **播放合成结果**: `Acoustic3dSimulation` 的主要输出之一是声道的传递函数或脉冲响应。当这些与声源模型（如 `LfPulse`, `TwoMassModel`）结合生成可听的语音波形后，需要通过 `SoundLib` 提供的播放功能才能让用户听到合成的声音。例如，`Acoustic3dPage` 中的播放功能（如 `OnPlayLongVowel`）会调用 `waveStartPlaying`。
*   **应用级音频交互**: `MainWindow` 中更通用的录音和播放功能（如 `OnRecord`, `OnPlayAll`）也完全依赖 `SoundLib`。

## 结论

`SoundLib.h` 和 `SoundLib.cpp` 构成了应用程序的音频输出（播放）和输入（录音）的底层基础。虽然它不直接参与 `Acoustic3dSimulation` 的核心数值计算，但它是将仿真和合成结果转化为可感知音频的**必要组件**。

在一个以 `Acoustic3dSimulation` 为核心的简化系统中，如果目标仍然包括能够**播放和听到**通过该仿真引擎合成的语音，或者需要任何形式的音频录制/播放功能，那么 **`SoundLib` 模块应当被保留**。

**当前决策**: 暂时保留 `SoundLib` 及其依赖，以确保核心的音频播放功能可用。 