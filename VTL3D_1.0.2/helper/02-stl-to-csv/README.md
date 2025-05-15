# 空心圆柱体STL生成器

这个程序使用CGAL库生成空心圆柱体（管道）的STL文件。不依赖复杂的布尔运算，直接构建中空圆柱体网格。

## 编译

确保系统已安装以下依赖项：
- CMake (版本 >= 3.8)
- CGAL库（基本版本即可）

然后按照以下步骤进行编译：

```bash
mkdir build
cd build
cmake ..
make
```

编译成功后，可执行文件将被放置在当前目录中。

## 使用方法

程序默认生成一个简单的空心圆柱体示例，但你可以通过命令行参数自定义参数：

```bash
./generate_hollow_cylinder [选项]
```

### 命令行选项

- `--inner <value>`: 设置内半径（默认：0.8）
- `--outer <value>`: 设置外半径（默认：1.0）
- `--height <value>`: 设置高度（默认：5.0）
- `--output <filename>`: 设置输出文件名（默认：example_pipe.stl）
- `--sections <value>`: 设置用于近似圆的段数（默认：64）

### 示例

生成一个内半径为1.0，外半径为1.5，高度为10的空心圆柱体：

```bash
./generate_hollow_cylinder --inner 1.0 --outer 1.5 --height 10 --output my_pipe.stl
```

## 实现说明

该程序直接构建空心圆柱体网格，每个部分包括：

1. 底部环形面
2. 顶部环形面
3. 外侧圆柱面
4. 内侧圆柱面

这种方法比使用布尔运算更高效，并且对CGAL库的版本要求较低。 