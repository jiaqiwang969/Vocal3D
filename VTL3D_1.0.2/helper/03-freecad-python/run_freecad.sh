#!/bin/bash
# 命令行工具：运行FreeCAD Python脚本而不启动界面

# FreeCAD可执行文件路径
FREECAD_BIN="/etc/profiles/per-user/jqwang/bin/freecad"

# 检查FreeCAD是否可用
if [ ! -x "$FREECAD_BIN" ]; then
    # 尝试使用系统默认路径
    FREECAD_BIN=$(which freecad 2>/dev/null)
    if [ ! -x "$FREECAD_BIN" ]; then
        echo "错误: 找不到FreeCAD可执行文件"
        echo "请确保FreeCAD已正确安装"
        exit 1
    fi
fi

# 显示帮助信息
show_help() {
    echo "使用方法: $0 [选项] <python脚本> [脚本参数...]"
    echo ""
    echo "选项:"
    echo "  -h, --help      显示此帮助信息"
    echo "  -v, --verbose   显示详细输出"
    echo ""
    echo "示例:"
    echo "  $0 generate_tube.py --length 10 --outer-radius 1.0 --inner-radius 0.8"
    echo "  $0 generate_elbow.py --angle 45 --taper-ratio 0.5"
    echo ""
}

# 解析命令行参数
VERBOSE=0

while [ "$1" != "" ]; do
    case $1 in
        -h | --help )
            show_help
            exit 0
            ;;
        -v | --verbose )
            VERBOSE=1
            shift
            ;;
        * )
            break
            ;;
    esac
done

if [ $# -lt 1 ]; then
    echo "错误: 未指定Python脚本"
    show_help
    exit 1
fi

SCRIPT=$1
shift

if [ ! -f "$SCRIPT" ]; then
    echo "错误: 找不到脚本文件 '$SCRIPT'"
    exit 1
fi

# 获取脚本的绝对路径
SCRIPT_PATH=$(readlink -f "$SCRIPT")

if [ $VERBOSE -eq 1 ]; then
    echo "运行: $FREECAD_BIN --console --run $SCRIPT_PATH $*"
fi

# 创建一个临时执行脚本
TEMP_SCRIPT=$(mktemp)
echo "import sys" > $TEMP_SCRIPT
echo "sys.argv = ['$SCRIPT_PATH'" >> $TEMP_SCRIPT
for arg in "$@"; do
    echo ", '$arg'" >> $TEMP_SCRIPT
done
echo "]" >> $TEMP_SCRIPT
echo "exec(open('$SCRIPT_PATH').read())" >> $TEMP_SCRIPT
echo "sys.exit(0)" >> $TEMP_SCRIPT

if [ $VERBOSE -eq 1 ]; then
    echo "执行临时脚本:"
    cat $TEMP_SCRIPT
fi

# 运行FreeCAD脚本
"$FREECAD_BIN" -c "exec(open('$TEMP_SCRIPT').read())"

# 获取脚本退出状态
EXIT_CODE=$?

# 清理临时文件
rm -f $TEMP_SCRIPT

if [ $EXIT_CODE -ne 0 ]; then
    echo "脚本运行失败，退出代码: $EXIT_CODE"
else
    if [ $VERBOSE -eq 1 ]; then
        echo "脚本执行成功"
    fi
fi

exit $EXIT_CODE 