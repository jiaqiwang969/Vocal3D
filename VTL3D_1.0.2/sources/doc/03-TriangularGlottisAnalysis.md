# TriangularGlottis 声门模型分析

本文档分析 `TriangularGlottis` 类（主要基于 `TriangularGlottis.cpp` 和 `TriangularGlottis.h`）在 VocalTractLab3D 项目中的作用和功能。这是项目中实现的另一种声门声源模型。

## 概述

`TriangularGlottis` 类实现了一种**三角声门模型**。与 `TwoMassModel` 类似，它也是一个用于语音合成的声源模型，其目的是模拟声带振动以产生驱动声道声学模型的准周期气流脉冲。虽然同为物理模型，但它在建模细节和复杂度上可能与双质量模型有所区别，通常旨在以可能更简化的方式捕捉声门振动的关键声学特性。

"三角"这个名称可能源于其对声门开口形状在某些振动相位的近似，或者声带边缘的运动轨迹导致了近似三角形的面积函数变化。

## 主要功能与特性

根据 `TriangularGlottis.cpp` 的实现，该类的关键功能与 `TwoMassModel` 有共通之处，但也展现出其自身的参数特点：

1.  **参数化控制 (Control Parameters)**:
    *   提供一系列控制参数以动态调整声源特性，包括：
        *   `f0`: 目标基频 (Hz)。
        *   `Subglottal pressure`: 声门下压 (dPa)。
        *   `Lower rest displacement` / `Upper rest displacement`: 声门上下部分的静止位移/初始开口 (cm)。
        *   `Arytenoid area`: 杓状软骨间隙面积 (cm²)，影响声门后部漏气。
        *   `Aspiration strength`: 送气强度 (dB)，此参数可能在此模型中更为突出，用于精细控制送气噪声的程度，影响声音的气息感。

2.  **静态物理参数 (Static Parameters)**:
    *   定义模型的内在物理属性，代表声带的解剖和生物力学特性。这些参数在加载特定"speaker"配置时设定，例如：
        *   声带长度、上下质量块的静止厚度和质量（尽管模型名未直接称"质量块"，但参数列表类似）。
        *   弹簧系统参数（线性刚度、接触刚度）。
        *   上下质量块的阻尼比。
        *   上下质量块之间的耦合弹簧系数。
        *   声道入口/出口长度（`Inlet length`, `Outlet length`），这可能是模型中对声门上下游短管效应的简化表示。
        *   声门的"固有基频"（Natural F0）及基频随声带张力参数Q（Tension Q）的变化率 (`dF0/dQ`)，用于校准基频控制。

3.  **运动方程与时域仿真 (`incTime` 方法)**:
    *   核心的动力学计算函数，通过数值积分求解描述声带运动的微分方程组，计算在一个时间步长后声门的新状态。
    *   模型基于质量-弹簧-阻尼系统，并考虑气动力、机械力和声带接触力。
    *   特别地，在计算驱动力时，它考虑了作用在开放部分的气动力，以及对声门上下游短管效应（通过 `Inlet length`, `Outlet length` 参数）的简化处理所带来的压力分布影响。
    *   与 `TwoMassModel` 类似，它也通过张力参数 `Q`（与 `f0` 相关）来调整模型的动态特性。

4.  **瞬时几何计算 (`calcGeometry`, `getLengthAndThickness`, `getOpenCloseDimensions` 方法)**:
    *   `calcGeometry`: 根据当前动力学状态（如质量块的相对位移）计算瞬时声门几何参数，包括上下声门开口面积。
    *   `getLengthAndThickness`: 根据张力参数 `Q` 计算声带的有效振动长度和厚度。
    *   `getOpenCloseDimensions`: 此方法是该模型的一个特色，用于计算声门"开放长度"、"接触长度"、"平均开放宽度"和"平均接触位置"。这反映了模型对声门（可能是不对称或部分闭合的）三角形或类三角形开口几何的详细描述，对于更精确地计算声门面积和气流阻力很重要。

5.  **声门面积输出 (`getTubeData` 方法)**:
    *   提供标准接口，输出当前计算得到的瞬时上下声门面积和对应的厚度（作为声学模型中的管段长度）。

6.  **作为 `Glottis` 派生类**: `TriangularGlottis` 继承自通用的 `Glottis` 基类，实现了其标准接口，使得程序可以在不同的声门模型间方便切换。

## 与 `Acoustic3dSimulation` 的关系

`TriangularGlottis` 作为声源模型，其与 `Acoustic3dSimulation`（声道滤波器）的关系和 `TwoMassModel` 类似：

*   **提供激励**: `TriangularGlottis` 模拟声带振动产生声门气流，这个气流是驱动声道（由 `Acoustic3dSimulation` 建模）产生语音的原始激励。
*   **声源-滤波器协同**: 在语音合成中，`Acoustic3dSimulation` 负责计算声道的传递特性，而 `TriangularGlottis` 负责提供输入给这个传递特性的声源信号。

## 结论

`TriangularGlottis` 是 VocalTractLab3D 中的另一种基于物理的声门模型，它通过对声门几何（特别是开放和闭合部分）的特定描述（可能涉及三角形近似）来模拟声带振动。它提供了与 `TwoMassModel` 功能相似但可能有不同声学细节或计算特性的声源选项。

对于一个以 `Acoustic3dSimulation` 为核心的简化系统，如果需要合成语音，保留包括 `TriangularGlottis` 在内的有效声门模型是必要的，以确保有合适的声源来驱动声道模型。 