   #!/bin/bash
   # 运行FreeCAD Python脚本的包装器
   
   # 检查输入参数
   if [ $# -lt 1 ]; then
     echo "使用方法: $0 <python脚本> [参数...]"
     exit 1
   fi
   
   SCRIPT=$1
   shift
   
   # 使用FreeCAD的Python解释器运行脚本
   freecad -c "$SCRIPT $*"
