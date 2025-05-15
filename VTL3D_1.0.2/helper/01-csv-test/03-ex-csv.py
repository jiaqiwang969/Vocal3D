import matplotlib.pyplot as plt
import numpy as np
import sys
import os
import math
from matplotlib import font_manager

# --- 字体设置 ---
# 获取脚本所在目录
# 注意: __file__ 在某些环境 (如交互式解释器) 中可能未定义
# 这里假设脚本是直接运行的
try:
    script_dir = os.path.dirname(os.path.abspath(__file__))
except NameError:
    script_dir = os.getcwd() # 如果 __file__ 未定义，则使用当前工作目录

font_path = os.path.join(script_dir, 'SimHei.ttf')
zh_font_prop = None
if os.path.exists(font_path):
    zh_font_prop = font_manager.FontProperties(fname=font_path)
else:
    print(f"警告：未在目录 '{script_dir}' 下找到字体文件 'SimHei.ttf'，中文可能无法正确显示。")

# --- Constants ---
MINIMAL_DISTANCE = 1e-6 # 用于比较浮点数或角度是否接近零

# --- Helper Functions ---

def normalize_vector(vx, vy):
    """归一化向量"""
    norm = np.sqrt(vx**2 + vy**2)
    if norm > MINIMAL_DISTANCE:
        return vx / norm, vy / norm
    else:
        return 1.0, 0.0 # 返回默认值，例如 (1, 0)

def normalize_angle(angle):
    """将角度标准化到 (-pi, pi] 区间"""
    while angle <= -np.pi:
        angle += 2 * np.pi
    while angle > np.pi:
        angle -= 2 * np.pi
    return angle

def calculate_curvature(p1, n1, p2, n2):
    """
    模拟 getCurvatureAngleShift 计算曲率半径和角度。
    输入:
        p1 (tuple): 截面1中心点 (x1, y1)
        n1 (tuple): 截面1单位法线 (nx1, ny1)
        p2 (tuple): 截面2中心点 (x2, y2)
        n2 (tuple): 截面2单位法线 (nx2, ny2)
    输出:
        radius (float): 曲率半径 R (可能为负，表示方向)
        angle (float): 法线夹角 alpha (弧度, 标准化到 (-pi, pi])
    """
    x1, y1 = p1
    nx1, ny1 = n1
    x2, y2 = p2
    nx2, ny2 = n2
    cross_p_n2 = (x2 - x1) * ny2 - (y2 - y1) * nx2
    cross_n2_n1 = nx2 * ny1 - ny2 * nx1
    radius = 0.0
    if abs(cross_n2_n1) > MINIMAL_DISTANCE:
        radius = -cross_p_n2 / cross_n2_n1
    else:
        radius = float('inf')
    angle1 = math.atan2(ny1, nx1)
    angle2 = math.atan2(ny2, nx2)
    angle_diff = angle2 - angle1
    angle = normalize_angle(angle_diff)
    return radius, angle

def rotate_vector(vector, angle_rad):
    """将二维向量旋转指定角度 (弧度)"""
    vx, vy = vector
    cos_a = math.cos(angle_rad)
    sin_a = math.sin(angle_rad)
    vx_new = cos_a * vx - sin_a * vy
    vy_new = sin_a * vx + cos_a * vy
    return vx_new, vy_new

def calculate_outlet_geometry(s_prime_i, n_hat_i, L_i, R_i, alpha_i):
    """
    模拟 Acoustic3dSimulation::ctrLinePtOut 和出口法线的计算。
    严格遵循 C++ 的条件判断逻辑来计算曲线出口点。
    """
    pt_in_x, pt_in_y = s_prime_i
    norm_in_x, norm_in_y = n_hat_i
    n_vec = (norm_in_x, norm_in_y) # 入口法线向量 N

    # --- 1. 计算出口法线 --- (不变)
    cos_a = math.cos(alpha_i)
    sin_a = math.sin(alpha_i)
    norm_out_x = cos_a * norm_in_x - sin_a * norm_in_y
    norm_out_y = sin_a * norm_in_x + cos_a * norm_in_y
    n_hat_out_i = normalize_vector(norm_out_x, norm_out_y)

    # --- 2. 计算出口中心点 ---
    s_out_i = s_prime_i # 默认等于入口点

    if L_i > MINIMAL_DISTANCE:
        if abs(alpha_i) < MINIMAL_DISTANCE or not math.isfinite(R_i):
            # --- 直线情况 ---
            tangent_x = norm_in_y
            tangent_y = -norm_in_x
            s_out_x = pt_in_x + L_i * tangent_x
            s_out_y = pt_in_y + L_i * tangent_y
            s_out_i = (s_out_x, s_out_y)
        else:
            # --- 曲线情况 (复现 C++ 条件逻辑) ---
            theta = abs(alpha_i) / 2.0
            sin_theta = math.sin(theta)
            signs_differ = (np.sign(R_i) * np.sign(alpha_i) < 0) if R_i != 0 and alpha_i != 0 else False
            translation_vec = (0.0, 0.0)
            if signs_differ:
                angle_rot1 = math.pi/2.0 - theta
                base_vec1 = (-norm_in_x, -norm_in_y)
                rotated_vec1 = rotate_vector(base_vec1, angle_rot1)
                dist_scalar1 = -2.0 * abs(R_i) * sin_theta
                translation_vec = (dist_scalar1 * rotated_vec1[0], dist_scalar1 * rotated_vec1[1])
            else:
                angle_rot2 = theta - math.pi/2.0
                base_vec2 = n_vec
                rotated_vec2 = rotate_vector(base_vec2, angle_rot2)
                dist_scalar2 = 2.0 * abs(R_i) * sin_theta
                translation_vec = (dist_scalar2 * rotated_vec2[0], dist_scalar2 * rotated_vec2[1])
            s_out_x = pt_in_x + translation_vec[0]
            s_out_y = pt_in_y + translation_vec[1]
            s_out_i = (s_out_x, s_out_y)

    return s_out_i, n_hat_out_i


# --- 数据加载与准备 (修改后) ---
def load_and_prepare_section_data(line_odd, line_even):
    """
    解析CSV行对, 模拟 C++ 中的数据加载、几何中心调整和必要的数据准备。
    返回包含原始数据和调整后数据的字典。
    """
    local_contour_y = []
    local_contour_z = []
    center_x, center_y = 0.0, 0.0
    normal_x, normal_y = 1.0, 0.0
    scale_in, scale_out = 1.0, 1.0
    try:
        parts_odd = line_odd.strip().split(';')
        if len(parts_odd) < 4: raise ValueError("奇数行字段不足")
        center_x = float(parts_odd[0])
        normal_x = float(parts_odd[1])
        scale_in = float(parts_odd[2])
        local_contour_y = [float(p) for p in parts_odd[3:] if p.strip()]
    except (IndexError, ValueError) as e:
        print(f"错误: 解析奇数行失败: {e}\n行: {line_odd.strip()}")
        return None
    try:
        parts_even = line_even.strip().split(';')
        if len(parts_even) < 4: raise ValueError("偶数行字段不足")
        center_y = float(parts_even[0])
        normal_y = float(parts_even[1])
        scale_out = float(parts_even[2])
        local_contour_z = [float(p) for p in parts_even[3:] if p.strip()]
    except (IndexError, ValueError) as e:
        print(f"错误: 解析偶数行失败: {e}\n行: {line_even.strip()}")
        return None
    if len(local_contour_y) != len(local_contour_z):
        print(f"错误: 轮廓点数量不匹配 ({len(local_contour_y)} vs {len(local_contour_z)})" )
        return None
    if not local_contour_y:
        print("警告: 未找到轮廓点")

    # 保存原始数据
    original_center = (center_x, center_y)
    original_normal = (normal_x, normal_y)
    original_contour_y = list(local_contour_y)
    original_contour_z = list(local_contour_z)
    z_min_orig, z_max_orig = (min(original_contour_z), max(original_contour_z)) if original_contour_z else (0.0, 0.0)

    # 准备绘图用的闭合原始轮廓
    original_contourY_plot = list(original_contour_y)
    original_contourZ_plot = list(original_contour_z)
    if original_contourY_plot and original_contourZ_plot:
        original_contourY_plot.append(original_contourY_plot[0])
        original_contourZ_plot.append(original_contourZ_plot[0])

    # --- 执行 Z 轴居中和中心点补偿 ---
    nx_norm, ny_norm = normalize_vector(normal_x, normal_y)
    normal_adj = (nx_norm, ny_norm)

    z_c_local = 0.0
    adjusted_contour_z = list(original_contour_z) # 默认等于原始
    z_min_adj, z_max_adj = z_min_orig, z_max_orig # 默认等于原始

    if local_contour_z:
        z_min_local = min(local_contour_z)
        z_max_local = max(local_contour_z)
        z_c_local = (z_min_local + z_max_local) / 2.0
        adjusted_contour_z = [z - z_c_local for z in local_contour_z]
        if adjusted_contour_z:
             z_min_adj = min(adjusted_contour_z)
             z_max_adj = max(adjusted_contour_z)

    adjusted_contour_y = list(local_contour_y) # Y 坐标不变

    # 计算补偿后的中心点
    adjusted_center_x = center_x + z_c_local * nx_norm
    adjusted_center_y = center_y + z_c_local * ny_norm
    center_adj = (adjusted_center_x, adjusted_center_y)

    # 准备绘图用的闭合调整后轮廓
    contour_y_plot_adj = list(adjusted_contour_y)
    contour_z_plot_adj = list(adjusted_contour_z)
    if contour_y_plot_adj and contour_z_plot_adj:
        contour_y_plot_adj.append(contour_y_plot_adj[0])
        contour_z_plot_adj.append(contour_z_plot_adj[0])

    # --- Debug: 打印 z_c_local 和中心点 ---
    print(f"--- Section Data ---")
    print(f"  z_c_local (Z offset): {z_c_local:.6f}")
    print(f"  Original Center (x, y): ({original_center[0]:.6f}, {original_center[1]:.6f})")
    print(f"  Adjusted Center (x, y): ({center_adj[0]:.6f}, {center_adj[1]:.6f})")
    # --- End Debug ---

    return {
        # --- 原始数据 ---
        "ctrLinePtIn_orig": original_center,
        "normalIn_orig": normalize_vector(original_normal[0], original_normal[1]), # 归一化原始法线
        "scaleIn_orig": scale_in,
        "scaleOut_orig": scale_out,
        "contourY_local_orig": original_contour_y,
        "contourZ_local_orig": original_contour_z,
        "zMin_orig": z_min_orig,
        "zMax_orig": z_max_orig,
        "original_contourY_plot": original_contourY_plot, # 绘图用原始闭合轮廓
        "original_contourZ_plot": original_contourZ_plot, # 绘图用原始闭合轮廓

        # --- 调整/补偿后数据 ---
        "ctrLinePtIn_adj": center_adj,
        "normalIn_adj": normal_adj,
        "scaleIn_adj": scale_in, # scale 不变
        "scaleOut_adj": scale_out, # scale 不变
        "contourY_local_adj": adjusted_contour_y,
        "contourZ_local_adj": adjusted_contour_z,
        "zMin_adj": z_min_adj,
        "zMax_adj": z_max_adj,
        "contourY_plot_adj": contour_y_plot_adj, # 绘图用调整后闭合轮廓
        "contourZ_plot_adj": contour_z_plot_adj, # 绘图用调整后闭合轮廓

        # --- 其他信息 ---
        "z_c_local": z_c_local, # Z轴中心偏移量

        # --- 用于存储计算结果的占位符 ---
        "results_adj": {},
        "results_orig": {}
    }

# --- 角点计算 (通用化) ---
def get_segment_points(ptIn, normalIn, scaleIn, ymin_local, ymax_local,
                       ptOut, normalOut, scaleOut):
    """
    使用段的入口/出口几何数据计算四个角点的全局坐标。
    参数为通用名称，可传入 _adj 或 _orig 数据。
    """
    ptInMin = (ptIn[0] + normalIn[0] * ymin_local * scaleIn,
               ptIn[1] + normalIn[1] * ymin_local * scaleIn)
    ptInMax = (ptIn[0] + normalIn[0] * ymax_local * scaleIn,
               ptIn[1] + normalIn[1] * ymax_local * scaleIn)
    ptOutMin = (ptOut[0] + normalOut[0] * ymin_local * scaleOut,
                ptOut[1] + normalOut[1] * ymin_local * scaleOut)
    ptOutMax = (ptOut[0] + normalOut[0] * ymax_local * scaleOut,
                ptOut[1] + normalOut[1] * ymax_local * scaleOut)
    return ptInMin, ptInMax, ptOutMin, ptOutMax

# --- 矢状图绘制 (修改后, 用于对比) ---
def draw_segment_sagittal_comparison(ax, section_data_i, segment_index):
    """
    在指定的 axes 上同时绘制补偿后和原始几何的声道段梯形轮廓。
    """
    # 补偿后结果 (实线, 标准色)
    res_adj = section_data_i["results_adj"]
    ptInMin_adj, ptInMax_adj, ptOutMin_adj, ptOutMax_adj = res_adj["corners"]
    label_seg_adj = '_nolegend_' if segment_index > 0 else '补偿后边界'
    label_upper_adj = '_nolegend_' if segment_index > 0 else '补偿后上轮廓'
    label_lower_adj = '_nolegend_' if segment_index > 0 else '补偿后下轮廓'
    ax.plot([ptInMin_adj[0], ptInMax_adj[0]], [ptInMin_adj[1], ptInMax_adj[1]], color='gray', linestyle='-', linewidth=1, label=label_seg_adj, zorder=5)
    ax.plot([ptOutMin_adj[0], ptOutMax_adj[0]], [ptOutMin_adj[1], ptOutMax_adj[1]], color='darkgray', linestyle='-', linewidth=1, zorder=5)
    ax.plot([ptInMin_adj[0], ptOutMin_adj[0]], [ptInMin_adj[1], ptOutMin_adj[1]], color='green', linestyle='-', linewidth=1, label=label_lower_adj, zorder=5)
    ax.plot([ptInMax_adj[0], ptOutMax_adj[0]], [ptInMax_adj[1], ptOutMax_adj[1]], color='blue', linestyle='-', linewidth=1, label=label_upper_adj, zorder=5)

    # 原始结果 (虚线, 浅色)
    res_orig = section_data_i["results_orig"]
    if res_orig: # 确保原始结果已计算
        ptInMin_orig, ptInMax_orig, ptOutMin_orig, ptOutMax_orig = res_orig["corners"]
        label_seg_orig = '_nolegend_' if segment_index > 0 else '原始边界'
        label_upper_orig = '_nolegend_' if segment_index > 0 else '原始上轮廓'
        label_lower_orig = '_nolegend_' if segment_index > 0 else '原始下轮廓'
        ax.plot([ptInMin_orig[0], ptInMax_orig[0]], [ptInMin_orig[1], ptInMax_orig[1]], color='lightgrey', linestyle='--', linewidth=1, label=label_seg_orig, zorder=4)
        ax.plot([ptOutMin_orig[0], ptOutMax_orig[0]], [ptOutMin_orig[1], ptOutMax_orig[1]], color='silver', linestyle='--', linewidth=1, zorder=4)
        ax.plot([ptInMin_orig[0], ptOutMin_orig[0]], [ptInMin_orig[1], ptOutMin_orig[1]], color='lightgreen', linestyle='--', linewidth=1, label=label_lower_orig, zorder=4)
        ax.plot([ptInMax_orig[0], ptOutMax_orig[0]], [ptInMax_orig[1], ptOutMax_orig[1]], color='lightblue', linestyle='--', linewidth=1, label=label_upper_orig, zorder=4)


# --- 主程序 ---
if len(sys.argv) > 1:
    csv_filename = sys.argv[1]
else:
    print("错误：请提供 CSV 文件名作为命令行参数。")
    print("用法: python 03-ex-csv.py <文件名.csv>") # 更新脚本名
    sys.exit(1)
if not os.path.exists(csv_filename):
    print(f"错误：文件 '{csv_filename}' 不存在。")
    sys.exit(1)

all_sections_data = []
try:
    with open(csv_filename, 'r') as f:
        lines = f.readlines()
        if len(lines) % 2 != 0:
            print(f"警告：CSV 文件 '{csv_filename}' 的行数不是偶数，可能数据不完整。")
        for i in range(0, len(lines), 2):
            if i + 1 < len(lines):
                section = load_and_prepare_section_data(lines[i], lines[i+1])
                if section:
                    all_sections_data.append(section)
except Exception as e:
    print(f"错误：读取或处理文件 '{csv_filename}' 时出错: {e}")
    sys.exit(1)

if not all_sections_data:
    print("错误：未能从 CSV 文件中成功解析任何截面数据。")
    sys.exit(1)

num_segments = len(all_sections_data) - 1

# --- 计算几何参数 (两次：补偿后 vs 原始) ---
for i in range(num_segments):
    data_i = all_sections_data[i]
    data_i_plus_1 = all_sections_data[i+1]

    # 1. 使用补偿/调整后的数据计算
    s_prime_i_adj = data_i["ctrLinePtIn_adj"]
    s_prime_i_plus_1_adj = data_i_plus_1["ctrLinePtIn_adj"]
    n_hat_i_adj = data_i["normalIn_adj"]
    n_hat_i_plus_1_adj = data_i_plus_1["normalIn_adj"]
    length_adj = np.sqrt((s_prime_i_plus_1_adj[0] - s_prime_i_adj[0])**2 + (s_prime_i_plus_1_adj[1] - s_prime_i_adj[1])**2)
    radius_adj, angle_adj = calculate_curvature(s_prime_i_adj, n_hat_i_adj, s_prime_i_plus_1_adj, n_hat_i_plus_1_adj)
    s_out_i_adj, n_hat_out_i_adj = calculate_outlet_geometry(
        s_prime_i_adj, n_hat_i_adj, length_adj, radius_adj, angle_adj
    )
    corners_adj = get_segment_points(
        s_prime_i_adj, n_hat_i_adj, data_i["scaleIn_adj"], data_i["zMin_adj"], data_i["zMax_adj"],
        s_out_i_adj, n_hat_out_i_adj, data_i["scaleOut_adj"]
    )
    data_i["results_adj"] = {
        "length": length_adj, "curvatureRadius": radius_adj, "curvatureAngle": angle_adj,
        "ctrLinePtOut": s_out_i_adj, "normalOut": n_hat_out_i_adj, "corners": corners_adj
    }

    # --- Debug: 打印角点坐标 (补偿后) ---
    print(f"-- Segment {i} (Adjusted/Compensated) --")
    print(f"  Corners Adj (InMin, InMax, OutMin, OutMax):")
    print(f"    InMin:  ({corners_adj[0][0]:.4f}, {corners_adj[0][1]:.4f}) | InMax:  ({corners_adj[1][0]:.4f}, {corners_adj[1][1]:.4f})")
    print(f"    OutMin: ({corners_adj[2][0]:.4f}, {corners_adj[2][1]:.4f}) | OutMax: ({corners_adj[3][0]:.4f}, {corners_adj[3][1]:.4f})")
    # --- End Debug ---

    # 2. 使用原始数据计算
    s_prime_i_orig = data_i["ctrLinePtIn_orig"]
    s_prime_i_plus_1_orig = data_i_plus_1["ctrLinePtIn_orig"]
    n_hat_i_orig = data_i["normalIn_orig"]
    n_hat_i_plus_1_orig = data_i_plus_1["normalIn_orig"]
    length_orig = np.sqrt((s_prime_i_plus_1_orig[0] - s_prime_i_orig[0])**2 + (s_prime_i_plus_1_orig[1] - s_prime_i_orig[1])**2)
    radius_orig, angle_orig = calculate_curvature(s_prime_i_orig, n_hat_i_orig, s_prime_i_plus_1_orig, n_hat_i_plus_1_orig)
    s_out_i_orig, n_hat_out_i_orig = calculate_outlet_geometry(
        s_prime_i_orig, n_hat_i_orig, length_orig, radius_orig, angle_orig
    )
    corners_orig = get_segment_points(
        s_prime_i_orig, n_hat_i_orig, data_i["scaleIn_orig"], data_i["zMin_orig"], data_i["zMax_orig"],
        s_out_i_orig, n_hat_out_i_orig, data_i["scaleOut_orig"]
    )
    data_i["results_orig"] = {
        "length": length_orig, "curvatureRadius": radius_orig, "curvatureAngle": angle_orig,
        "ctrLinePtOut": s_out_i_orig, "normalOut": n_hat_out_i_orig, "corners": corners_orig
    }

    # --- Debug: 打印角点坐标 (原始) ---
    print(f"-- Segment {i} (Original/Raw) --")
    print(f"  Corners Orig (InMin, InMax, OutMin, OutMax):")
    print(f"    InMin:  ({corners_orig[0][0]:.4f}, {corners_orig[0][1]:.4f}) | InMax:  ({corners_orig[1][0]:.4f}, {corners_orig[1][1]:.4f})")
    print(f"    OutMin: ({corners_orig[2][0]:.4f}, {corners_orig[2][1]:.4f}) | OutMax: ({corners_orig[3][0]:.4f}, {corners_orig[3][1]:.4f})")
    print(f"---------------------------------------")
    # --- End Debug ---


# --- 绘图 ---
fig, axs = plt.subplots(1, 2, figsize=(14, 7)) # 稍微增大画布
# script_version = "v24" # 更新版本号
script_version = "v24_comparison" # 新版本号
fig.suptitle(f'文件 {os.path.basename(csv_filename)} 可视化对比 (补偿 vs 原始 - {script_version})', fontproperties=zh_font_prop, fontsize=14)

# --- 左图: 矢状面视图对比 ---
ax_left = axs[0]
for i in range(num_segments):
    data_i = all_sections_data[i]
    # 绘制补偿后和原始的边界
    draw_segment_sagittal_comparison(ax_left, data_i, i)

    # 绘制补偿后的中心线 (红色实线)
    s_prime_i_adj = data_i["ctrLinePtIn_adj"]
    s_out_i_adj = data_i["results_adj"]["ctrLinePtOut"]
    ax_left.plot([s_prime_i_adj[0], s_out_i_adj[0]], [s_prime_i_adj[1], s_out_i_adj[1]],
                 color='red', linestyle='-', marker='.', markersize=4, linewidth=1.5, zorder=10,
                 label='_nolegend_' if i > 0 else '补偿后中心线')

    # 绘制原始中心线 (橙色虚线)
    s_prime_i_orig = data_i["ctrLinePtIn_orig"]
    s_out_i_orig = data_i["results_orig"]["ctrLinePtOut"]
    ax_left.plot([s_prime_i_orig[0], s_out_i_orig[0]], [s_prime_i_orig[1], s_out_i_orig[1]],
                 color='orange', linestyle='--', marker='x', markersize=3, linewidth=1, zorder=9,
                 label='_nolegend_' if i > 0 else '原始中心线')

# 添加图例元素 (如果 num_segments == 0)
if num_segments == 0:
     ax_left.plot([],[], color='red', linestyle='-', marker='.', label='补偿后中心线')
     ax_left.plot([],[], color='orange', linestyle='--', marker='x', label='原始中心线')
     # 只需添加一组边界图例即可
     ax_left.plot([],[], color='gray', linestyle='-', label='补偿后边界')
     ax_left.plot([],[], color='blue', linestyle='-', label='补偿后上轮廓')
     ax_left.plot([],[], color='green', linestyle='-', label='补偿后下轮廓')
     ax_left.plot([],[], color='lightgrey', linestyle='--', label='原始边界')
     ax_left.plot([],[], color='lightblue', linestyle='--', label='原始上轮廓')
     ax_left.plot([],[], color='lightgreen', linestyle='--', label='原始下轮廓')


ax_left.set_title(f'矢状面视图 (补偿后 vs. 原始 - {script_version})', fontproperties=zh_font_prop)
ax_left.set_xlabel('全局 X 坐标', fontproperties=zh_font_prop)
ax_left.set_ylabel('全局 Y 坐标', fontproperties=zh_font_prop)
ax_left.set_aspect('equal', adjustable='box')
ax_left.grid(True)
# 调整图例位置和大小
ax_left.legend(fontsize='x-small', prop=zh_font_prop, loc='best')


# --- 右图: 截面 0 Z轴居中效果 ---
ax_right = axs[1]
if all_sections_data: # 确保至少有一个截面数据
    first_section_data = all_sections_data[0]

    # 绘制原始局部轮廓 (虚线, 浅红色) - plot在后，zorder低
    if first_section_data.get("original_contourY_plot") and first_section_data.get("original_contourZ_plot"):
        ax_right.plot(first_section_data["original_contourY_plot"],
                        first_section_data["original_contourZ_plot"],
                        marker='.', markersize=3, linestyle='--', color='lightcoral', label='从CSV读取的原始轮廓', zorder=1)

    # 绘制居中后的轮廓 (实线, 深蓝色) - plot在前，zorder高
    if first_section_data.get("contourY_plot_adj") and first_section_data.get("contourZ_plot_adj"):
        ax_right.plot(first_section_data["contourY_plot_adj"],
                        first_section_data["contourZ_plot_adj"],
                        marker='o', markersize=4, linestyle='-', color='darkblue', label='Z轴居中后的轮廓', zorder=2)

    # 绘制局部坐标系的原点 (0,0) / Z轴居中位置
    ax_right.axhline(y=0, color='black', linestyle='-.', linewidth=1, zorder=5, label='局部 Z=0 (居中后)')

    # 绘制原始 Z 轴中心点
    z_c = first_section_data.get("z_c_local", None)
    if z_c is not None and abs(z_c) > MINIMAL_DISTANCE : # 仅当中心确实偏移时绘制
        ax_right.axhline(y=z_c, color='purple', linestyle=':', linewidth=1, zorder=6, label=f'原始 Z 轴中心 ({z_c:.2f})')

    ax_right.set_title('截面 0 (Z轴居中效果)', fontproperties=zh_font_prop)
    ax_right.set_xlabel('局部 Y 坐标', fontproperties=zh_font_prop)
    ax_right.set_ylabel('局部 Z 坐标', fontproperties=zh_font_prop)
    ax_right.set_aspect('equal', adjustable='box')
    ax_right.grid(True)
    ax_right.legend(fontsize='small', prop=zh_font_prop, loc='best')

    # 动态设置坐标轴范围 (基于原始和居中后的局部坐标)
    all_x_coords = [0]
    all_y_coords = [0]
    if first_section_data.get("original_contourY_plot"):
        all_x_coords.extend(first_section_data["original_contourY_plot"])
    if first_section_data.get("original_contourZ_plot"):
        all_y_coords.extend(first_section_data["original_contourZ_plot"])
    if first_section_data.get("contourY_plot_adj"): # 同时考虑居中后范围
        all_x_coords.extend(first_section_data["contourY_plot_adj"])
    if first_section_data.get("contourZ_plot_adj"): # 同时考虑居中后范围
        all_y_coords.extend(first_section_data["contourZ_plot_adj"])
    if z_c is not None:
        all_y_coords.append(z_c) # 包含原始中心线

    if len(all_x_coords) > 1 and len(all_y_coords) > 1:
        min_x, max_x = min(all_x_coords), max(all_x_coords)
        min_y, max_y = min(all_y_coords), max(all_y_coords)
        range_x = max_x - min_x if max_x > min_x else 1.0
        range_y = max_y - min_y if max_y > min_y else 1.0
        pad_x = 0.1 * range_x + 0.5
        pad_y = 0.1 * range_y + 0.5
        ax_right.set_xlim(min_x - pad_x, max_x + pad_x)
        ax_right.set_ylim(min_y - pad_y, max_y + pad_y)
    else:
         ax_right.set_xlim(-1.5, 1.5)
         ax_right.set_ylim(-1.5, 1.5)

else: # 如果没有截面数据
    ax_right.text(0.5, 0.5, '无截面数据', horizontalalignment='center', verticalalignment='center', transform=ax_right.transAxes, fontproperties=zh_font_prop)
    ax_right.set_title('截面 0', fontproperties=zh_font_prop)

# 调整布局并保存
plt.tight_layout(rect=[0, 0.03, 1, 0.93]) # 调整顶部空间以适应更长的标题
output_filename = f"{os.path.splitext(os.path.basename(csv_filename))[0]}_comparison_visualization_{script_version}.png" # 修改: 输出文件名
plt.savefig(output_filename, dpi=300, bbox_inches='tight')
print(f"对比可视化结果已保存到: {output_filename}")

