#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成圆管模型并导出STL文件 - 基于FreeCAD Python API的无界面脚本
"""

import sys
import os
import math
import argparse

# 导入FreeCAD模块
try:
    import FreeCAD
    import Part
    import Mesh
    # 尝试导入可能需要的额外模块
    try:
        import MeshPart
    except ImportError:
        pass
    print(f"成功加载FreeCAD (版本: {FreeCAD.Version})")
except ImportError as e:
    print(f"错误: 无法导入FreeCAD模块 - {e}")
    print("请确保FreeCAD已正确安装，并且环境变量设置正确")
    sys.exit(1)

def create_tube(
    length, 
    outer_radius, 
    inner_radius, 
    output_file="tube.stl",
    center=FreeCAD.Vector(0, 0, 0),
    direction=FreeCAD.Vector(0, 0, 1),
    segments=64
):
    """
    创建一个直圆管模型并导出为STL文件
    
    参数:
        length: 圆管长度
        outer_radius: 外半径
        inner_radius: 内半径
        output_file: 输出STL文件路径
        center: 圆管中心点
        direction: 圆管方向向量
        segments: 圆周分段数
    """
    # 验证参数
    if inner_radius >= outer_radius:
        raise ValueError("内半径必须小于外半径")
    
    if length <= 0 or outer_radius <= 0 or inner_radius <= 0:
        raise ValueError("长度和半径必须为正数")
    
    # 标准化方向向量
    if direction.Length == 0:
        raise ValueError("方向向量不能为零向量")
    
    direction = direction.normalize()
    
    # 创建新文档
    doc = FreeCAD.newDocument("Tube")
    
    # 创建外圆柱
    # 注意: 根据FreeCAD版本不同，makeCylinder可能接受不同数量的参数
    # 在较旧版本中，没有segments参数
    try:
        # 尝试使用6个参数（包括segments）
        outer_cylinder = Part.makeCylinder(
            outer_radius,  # 半径
            length,        # 高度
            center,        # 基点 
            direction,     # 方向
            360,           # 角度
            segments       # 分段数
        )
    except TypeError:
        # 如果失败，使用5个参数（不包括segments）
        outer_cylinder = Part.makeCylinder(
            outer_radius,  # 半径
            length,        # 高度
            center,        # 基点 
            direction,     # 方向
            360            # 角度
        )
    
    # 创建内圆柱
    try:
        # 尝试使用6个参数（包括segments）
        inner_cylinder = Part.makeCylinder(
            inner_radius,  # 半径
            length,        # 高度
            center,        # 基点 
            direction,     # 方向
            360,           # 角度
            segments       # 分段数
        )
    except TypeError:
        # 如果失败，使用5个参数（不包括segments）
        inner_cylinder = Part.makeCylinder(
            inner_radius,  # 半径
            length,        # 高度
            center,        # 基点 
            direction,     # 方向
            360            # 角度
        )
    
    # 执行布尔差运算，创建空心圆管
    hollow_tube = outer_cylinder.cut(inner_cylinder)
    
    # 添加到文档
    tube_obj = doc.addObject("Part::Feature", "Tube")
    tube_obj.Shape = hollow_tube
    
    # 导出STL文件
    try:
        # 尝试新版本的导出方式
        Mesh.export([tube_obj], output_file)
    except TypeError:
        # 如果失败，尝试旧版本的导出方式
        mesh = doc.addObject("Mesh::Feature", "Mesh")
        mesh.Mesh = MeshPart.meshFromShape(Shape=hollow_tube, LinearDeflection=0.1, AngularDeflection=0.1)
        mesh.Mesh.write(output_file)
    
    # 计算体积和表面积
    volume = hollow_tube.Volume
    surface_area = hollow_tube.Area
    wall_thickness = outer_radius - inner_radius
    
    # 打印信息
    print(f"已生成圆管模型并导出到: {output_file}")
    print(f"  外半径: {outer_radius}")
    print(f"  内半径: {inner_radius}")
    print(f"  壁厚: {wall_thickness}")
    print(f"  长度: {length}")
    print(f"  体积: {volume:.2f}立方单位")
    print(f"  表面积: {surface_area:.2f}平方单位")
    
    return doc

def main():
    """命令行入口函数"""
    parser = argparse.ArgumentParser(description="生成圆管模型并导出STL文件")
    parser.add_argument("-l", "--length", type=float, default=10.0, help="圆管长度 (默认: 10.0)")
    parser.add_argument("-o", "--outer-radius", type=float, default=1.0, help="外半径 (默认: 1.0)")
    parser.add_argument("-i", "--inner-radius", type=float, default=0.8, help="内半径 (默认: 0.8)")
    parser.add_argument("-s", "--segments", type=int, default=64, help="圆周分段数 (默认: 64)")
    parser.add_argument("-f", "--output-file", type=str, default="tube.stl", help="输出STL文件路径 (默认: tube.stl)")
    parser.add_argument("-x", "--center-x", type=float, default=0.0, help="中心点X坐标 (默认: 0.0)")
    parser.add_argument("-y", "--center-y", type=float, default=0.0, help="中心点Y坐标 (默认: 0.0)")
    parser.add_argument("-z", "--center-z", type=float, default=0.0, help="中心点Z坐标 (默认: 0.0)")
    parser.add_argument("-dx", "--dir-x", type=float, default=0.0, help="方向向量X分量 (默认: 0.0)")
    parser.add_argument("-dy", "--dir-y", type=float, default=0.0, help="方向向量Y分量 (默认: 0.0)")
    parser.add_argument("-dz", "--dir-z", type=float, default=1.0, help="方向向量Z分量 (默认: 1.0)")
    
    args = parser.parse_args()
    
    try:
        # 创建中心点和方向向量
        center = FreeCAD.Vector(args.center_x, args.center_y, args.center_z)
        direction = FreeCAD.Vector(args.dir_x, args.dir_y, args.dir_z)
        
        # 创建圆管并导出
        create_tube(
            args.length,
            args.outer_radius,
            args.inner_radius,
            args.output_file,
            center,
            direction,
            args.segments
        )
        
        return 0
    except Exception as e:
        print(f"错误: {e}")
        return 1

if __name__ == "__main__":
    sys.exit(main()) 