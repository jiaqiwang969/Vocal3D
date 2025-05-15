# FreeCAD STL生成工具

这个工具集使用FreeCAD的Python API来生成各种管道形状的STL文件，无需启动FreeCAD图形界面。

## 安装要求

- FreeCAD (已测试版本: 0.19或更高)
- Python 3.x

## 工具脚本

### 1. 直圆管生成器 (generate_tube.py)

生成带有指定壁厚的直圆管STL模型。

```bash
./run_freecad.sh generate_tube.py [参数选项]
```

参数:
- `-l, --length <值>`: 圆管长度 (默认: 10.0)
- `-o, --outer-radius <值>`: 外半径 (默认: 1.0)
- `-i, --inner-radius <值>`: 内半径 (默认: 0.8)
- `-s, --segments <值>`: 圆周分段数 (默认: 64)
- `-f, --output-file <文件名>`: 输出STL文件路径 (默认: tube.stl)
- `-x, --center-x <值>`: 中心点X坐标 (默认: 0.0)
- `-y, --center-y <值>`: 中心点Y坐标 (默认: 0.0)
- `-z, --center-z <值>`: 中心点Z坐标 (默认: 0.0)
- `-dx, --dir-x <值>`: 方向向量X分量 (默认: 0.0)
- `-dy, --dir-y <值>`: 方向向量Y分量 (默认: 0.0)
- `-dz, --dir-z <值>`: 方向向量Z分量 (默认: 1.0)

示例:
```bash
./run_freecad.sh generate_tube.py --length 20 --outer-radius 1.5 --inner-radius 1.2
```

### 2. 弯管生成器 (generate_elbow.py)

生成带有指定弯曲角度和壁厚的弯管STL模型，支持渐缩（管径从一端到另一端逐渐变小）。

```bash
./run_freecad.sh generate_elbow.py [参数选项]
```

参数:
- `-o, --outer-radius <值>`: 起始外半径 (默认: 1.0)
- `-i, --inner-radius <值>`: 起始内半径 (默认: 0.8)
- `-b, --bend-radius <值>`: 弯曲半径 (默认: 4.0)
- `-a, --angle <值>`: 弯曲角度(度) (默认: 45.0)
- `-t, --taper-ratio <值>`: 终端/起始半径比例 (默认: 1.0，表示不缩小)
- `-s, --sections <值>`: 弯曲分段数 (默认: 64)
- `-f, --output-file <文件名>`: 输出STL文件路径 (默认: elbow.stl)
- `-x, --center-x <值>`: 起始点X坐标 (默认: 0.0)
- `-y, --center-y <值>`: 起始点Y坐标 (默认: 0.0)
- `-z, --center-z <值>`: 起始点Z坐标 (默认: 0.0)
- `-dx, --dir-x <值>`: 起始方向X分量 (默认: 1.0)
- `-dy, --dir-y <值>`: 起始方向Y分量 (默认: 0.0)
- `-dz, --dir-z <值>`: 起始方向Z分量 (默认: 0.0)
- `-nx, --normal-x <值>`: 弯曲平面法线X分量 (默认: 0.0)
- `-ny, --normal-y <值>`: 弯曲平面法线Y分量 (默认: 0.0)
- `-nz, --normal-z <值>`: 弯曲平面法线Z分量 (默认: 1.0)

示例 - 创建45度渐缩弯管:
```bash
./run_freecad.sh generate_elbow.py --outer-radius 1.0 --inner-radius 0.8 --taper-ratio 0.6
```

## 故障排除

1. 如果出现"找不到FreeCAD可执行文件"错误:
   - 确保FreeCAD已正确安装
   - 编辑`run_freecad.sh`脚本中的`FREECAD_BIN`变量，设置为您系统中FreeCAD可执行文件的路径

2. 如果脚本无法导入FreeCAD模块:
   - 确保您使用的是通过`run_freecad.sh`运行脚本，而不是直接通过Python运行

3. 生成的弯管模型畸形:
   - 增加弯曲半径（建议至少为外径的3倍）
   - 增加分段数以获得更平滑的效果 