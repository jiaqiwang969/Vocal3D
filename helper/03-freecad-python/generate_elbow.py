#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成弯管模型并导出STL文件 - 基于FreeCAD Python API的无界面脚本
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

def create_elbow(
    outer_radius,
    inner_radius,
    bend_radius,
    angle=45.0,
    output_file="elbow.stl",
    center=FreeCAD.Vector(0, 0, 0),
    start_dir=FreeCAD.Vector(1, 0, 0),
    bend_plane_normal=FreeCAD.Vector(0, 0, 1),
    sections=64,
    taper_ratio=1.0  # 1.0表示不缩小，0.5表示终端半径是起始半径的一半
):
    """
    创建一个弯管模型并导出为STL文件
    
    参数:
        outer_radius: 起始外半径
        inner_radius: 起始内半径
        bend_radius: 弯曲半径
        angle: 弯曲角度(度)
        output_file: 输出STL文件路径
        center: 起始点中心
        start_dir: 起始方向向量
        bend_plane_normal: 弯曲平面法线
        sections: 弯曲分段数
        taper_ratio: 终端/起始半径比例，用于渐缩弯管
    """
    # 验证参数
    if inner_radius >= outer_radius:
        raise ValueError("内半径必须小于外半径")
    
    if outer_radius <= 0 or inner_radius <= 0 or bend_radius <= 0:
        raise ValueError("半径必须为正数")
    
    if bend_radius <= outer_radius * 2:
        print(f"警告: 弯曲半径({bend_radius})可能过小，可能导致模型畸形")
    
    # 标准化向量
    if start_dir.Length == 0 or bend_plane_normal.Length == 0:
        raise ValueError("方向向量不能为零向量")
    
    start_dir = start_dir.normalize()
    bend_plane_normal = bend_plane_normal.normalize()
    
    # 确保方向向量和法线正交
    dot_product = start_dir.dot(bend_plane_normal)
    if abs(dot_product) > 1e-6:
        # 调整法线使其与方向向量正交
        bend_plane_normal = bend_plane_normal - start_dir.multiply(dot_product)
        bend_plane_normal = bend_plane_normal.normalize()
    
    # 计算弯曲平面的第二个方向向量（弯曲方向）
    bend_dir = bend_plane_normal.cross(start_dir)
    bend_dir = bend_dir.normalize()
    
    # 计算弯管中心点
    bend_center = center.add(start_dir.multiply(bend_radius))
    
    # 创建新文档
    doc = FreeCAD.newDocument("Elbow")
    
    # 转换角度为弧度
    angle_rad = math.radians(angle)
    
    # 创建路径（圆弧）
    arc_points = []
    for i in range(sections + 1):
        param = float(i) / sections
        current_angle = angle_rad * param
        
        # 计算当前点的位置
        pos_vec = start_dir.multiply(math.cos(current_angle)).add(
                  bend_dir.multiply(math.sin(current_angle)))
        point = bend_center.sub(pos_vec.multiply(bend_radius))
        arc_points.append(point)
    
    arc = Part.makePolygon(arc_points)
    
    # 创建用于放样的截面轮廓
    profiles = []
    
    # 生成沿路径的截面
    for i in range(sections + 1):
        param = float(i) / sections
        current_angle = angle_rad * param
        
        # 计算当前位置
        pos_vec = start_dir.multiply(math.cos(current_angle)).add(
                  bend_dir.multiply(math.sin(current_angle)))
        point = bend_center.sub(pos_vec.multiply(bend_radius))
        
        # 管道截面的法线（沿路径切线）
        if i < sections:
            tangent = arc_points[i+1].sub(arc_points[i])
        else:
            tangent = arc_points[i].sub(arc_points[i-1])
        tangent = tangent.normalize()
        
        # 计算当前截面的半径（考虑渐缩）
        current_outer_radius = outer_radius * (1.0 - param * (1.0 - taper_ratio))
        current_inner_radius = inner_radius * (1.0 - param * (1.0 - taper_ratio))
        
        # 创建截面圆环
        normal = tangent
        
        # 创建截面的局部坐标系
        u = bend_plane_normal.cross(normal)
        if u.Length < 1e-6:
            # 如果交叉积接近零，选择一个垂直于法线的向量
            if abs(normal.x) < abs(normal.y):
                u = FreeCAD.Vector(1, 0, 0)
            else:
                u = FreeCAD.Vector(0, 1, 0)
            u = u.sub(normal.multiply(u.dot(normal)))
            
        u = u.normalize()
        v = normal.cross(u).normalize()
        
        # 创建内外圆
        outer_circle_pts = []
        inner_circle_pts = []
        
        # 使用24个分段创建圆
        radial_segments = 24
        for j in range(radial_segments):
            angle_j = 2.0 * math.pi * j / radial_segments
            cos_j = math.cos(angle_j)
            sin_j = math.sin(angle_j)
            
            # 计算圆周上的点
            circle_vec = u.multiply(cos_j).add(v.multiply(sin_j))
            
            outer_pt = point.add(circle_vec.multiply(current_outer_radius))
            inner_pt = point.add(circle_vec.multiply(current_inner_radius))
            
            outer_circle_pts.append(outer_pt)
            inner_circle_pts.append(inner_pt)
        
        # 闭合圆
        outer_circle_pts.append(outer_circle_pts[0])
        inner_circle_pts.append(inner_circle_pts[0])
        
        # 创建轮廓线
        outer_wire = Part.makePolygon(outer_circle_pts)
        inner_wire = Part.makePolygon(inner_circle_pts)
        
        # 创建截面轮廓（带内孔的面）
        try:
            # 在新版本中，可以直接创建带孔的面
            face = Part.Face([outer_wire, inner_wire])
        except Exception:
            # 在旧版本中，可能需要不同的方法
            # 尝试使用wires属性
            face = Part.Face(outer_wire)
            face.Wires = [outer_wire, inner_wire]
        
        profiles.append(face)
    
    # 使用放样创建弯管
    try:
        # 尝试使用标准参数
        elbow = Part.makeLoft(profiles, True, False)
    except TypeError:
        # 如果失败，尝试简化参数
        elbow = Part.makeLoft(profiles, True)
    
    # 添加到文档
    elbow_obj = doc.addObject("Part::Feature", "Elbow")
    elbow_obj.Shape = elbow
    
    # 导出STL文件
    try:
        # 尝试新版本的导出方式
        Mesh.export([elbow_obj], output_file)
    except (TypeError, AttributeError):
        # 如果失败，尝试旧版本的导出方式
        try:
            # 尝试使用MeshPart模块
            import MeshPart
            mesh = doc.addObject("Mesh::Feature", "Mesh")
            mesh.Mesh = MeshPart.meshFromShape(Shape=elbow, LinearDeflection=0.1, AngularDeflection=0.1)
            mesh.Mesh.write(output_file)
        except (ImportError, AttributeError):
            # 如果MeshPart不可用，尝试直接转换和导出
            from FreeCAD import Base
            mesh = doc.addObject("Mesh::Feature", "Mesh")
            mesh.Mesh = elbow.tessellate(0.1)
            mesh.Mesh.write(output_file)
    
    # 计算体积和表面积
    volume = elbow.Volume
    surface_area = elbow.Area
    wall_thickness = outer_radius - inner_radius
    end_outer_radius = outer_radius * taper_ratio
    end_inner_radius = inner_radius * taper_ratio
    
    # 打印信息
    print(f"已生成弯管模型并导出到: {output_file}")
    print(f"  起始外半径: {outer_radius}")
    print(f"  起始内半径: {inner_radius}")
    print(f"  结束外半径: {end_outer_radius}")
    print(f"  结束内半径: {end_inner_radius}")
    print(f"  壁厚: {wall_thickness}")
    print(f"  弯曲半径: {bend_radius}")
    print(f"  弯曲角度: {angle}度")
    print(f"  体积: {volume:.2f}立方单位")
    print(f"  表面积: {surface_area:.2f}平方单位")
    
    return doc

def main():
    """命令行入口函数"""
    parser = argparse.ArgumentParser(description="生成弯管模型并导出STL文件")
    parser.add_argument("-o", "--outer-radius", type=float, default=1.0, help="起始外半径 (默认: 1.0)")
    parser.add_argument("-i", "--inner-radius", type=float, default=0.8, help="起始内半径 (默认: 0.8)")
    parser.add_argument("-b", "--bend-radius", type=float, default=4.0, help="弯曲半径 (默认: 4.0)")
    parser.add_argument("-a", "--angle", type=float, default=45.0, help="弯曲角度(度) (默认: 45.0)")
    parser.add_argument("-t", "--taper-ratio", type=float, default=1.0, help="终端/起始半径比例 (默认: 1.0，表示不缩小)")
    parser.add_argument("-s", "--sections", type=int, default=64, help="弯曲分段数 (默认: 64)")
    parser.add_argument("-f", "--output-file", type=str, default="elbow.stl", help="输出STL文件路径 (默认: elbow.stl)")
    parser.add_argument("-x", "--center-x", type=float, default=0.0, help="起始点X坐标 (默认: 0.0)")
    parser.add_argument("-y", "--center-y", type=float, default=0.0, help="起始点Y坐标 (默认: 0.0)")
    parser.add_argument("-z", "--center-z", type=float, default=0.0, help="起始点Z坐标 (默认: 0.0)")
    parser.add_argument("-dx", "--dir-x", type=float, default=1.0, help="起始方向X分量 (默认: 1.0)")
    parser.add_argument("-dy", "--dir-y", type=float, default=0.0, help="起始方向Y分量 (默认: 0.0)")
    parser.add_argument("-dz", "--dir-z", type=float, default=0.0, help="起始方向Z分量 (默认: 0.0)")
    parser.add_argument("-nx", "--normal-x", type=float, default=0.0, help="弯曲平面法线X分量 (默认: 0.0)")
    parser.add_argument("-ny", "--normal-y", type=float, default=0.0, help="弯曲平面法线Y分量 (默认: 0.0)")
    parser.add_argument("-nz", "--normal-z", type=float, default=1.0, help="弯曲平面法线Z分量 (默认: 1.0)")
    
    args = parser.parse_args()
    
    try:
        # 创建向量
        center = FreeCAD.Vector(args.center_x, args.center_y, args.center_z)
        start_dir = FreeCAD.Vector(args.dir_x, args.dir_y, args.dir_z)
        bend_normal = FreeCAD.Vector(args.normal_x, args.normal_y, args.normal_z)
        
        # 创建弯管并导出
        create_elbow(
            args.outer_radius,
            args.inner_radius,
            args.bend_radius,
            args.angle,
            args.output_file,
            center,
            start_dir,
            bend_normal,
            args.sections,
            args.taper_ratio
        )
        
        return 0
    except Exception as e:
        print(f"错误: {e}")
        return 1

if __name__ == "__main__":
    sys.exit(main()) 