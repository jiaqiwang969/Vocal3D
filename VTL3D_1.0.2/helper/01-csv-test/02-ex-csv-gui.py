import tkinter as tk
from tkinter import filedialog, messagebox
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk
from matplotlib.path import Path
import numpy as np
import sys
import os
import math
from matplotlib import font_manager  # 新增导入

# --- 字体设置 (全局变量) ---
zh_font_prop = None
try:
    # 假设字体文件在脚本同目录下
    script_dir = os.path.dirname(os.path.abspath(__file__))
    font_path = os.path.join(script_dir, 'SimHei.ttf')
    if os.path.exists(font_path):
        zh_font_prop = font_manager.FontProperties(fname=font_path)
        print(f"已成功加载中文字体: {font_path}")
    else:
        print(f"警告：未在目录 '{script_dir}' 下找到字体文件 'SimHei.ttf'，中文可能无法正确显示。")
except NameError:
    # 如果 __file__ 未定义 (例如在某些打包环境中), 尝试当前工作目录
    script_dir = os.getcwd()
    font_path = os.path.join(script_dir, 'SimHei.ttf')
    if os.path.exists(font_path):
        zh_font_prop = font_manager.FontProperties(fname=font_path)
        print(f"已成功加载中文字体: {font_path}")
    else:
        print(f"警告：未在目录 '{script_dir}' 或 '{os.getcwd()}' 下找到字体文件 'SimHei.ttf'，中文可能无法正确显示。")

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
            # --- 直线情况 --- (不变)
            tangent_x = norm_in_y
            tangent_y = -norm_in_x
            s_out_x = pt_in_x + L_i * tangent_x
            s_out_y = pt_in_y + L_i * tangent_y
            s_out_i = (s_out_x, s_out_y)
        else:
            # --- 曲线情况 (复现 C++ 条件逻辑) ---
            theta = abs(alpha_i) / 2.0
            sin_theta = math.sin(theta)

            # C++ 条件: R 和 R*alpha 符号是否相反 (等价于 R 和 alpha 符号是否不同)
            signs_differ = (np.sign(R_i) * np.sign(alpha_i) < 0) if R_i != 0 and alpha_i != 0 else False

            translation_vec = (0.0, 0.0)

            if signs_differ:
                # 分支1: R 和 alpha 符号不同
                angle_rot1 = math.pi/2.0 - theta
                base_vec1 = (-norm_in_x, -norm_in_y) # 基础向量 -N
                rotated_vec1 = rotate_vector(base_vec1, angle_rot1)
                dist_scalar1 = -2.0 * abs(R_i) * sin_theta
                translation_vec = (dist_scalar1 * rotated_vec1[0], dist_scalar1 * rotated_vec1[1])
            else:
                # 分支2: R 和 alpha 符号相同 (或其一为0)
                angle_rot2 = theta - math.pi/2.0
                base_vec2 = n_vec # 基础向量 N
                rotated_vec2 = rotate_vector(base_vec2, angle_rot2)
                dist_scalar2 = 2.0 * abs(R_i) * sin_theta
                translation_vec = (dist_scalar2 * rotated_vec2[0], dist_scalar2 * rotated_vec2[1])

            # 应用最终的平移向量
            s_out_x = pt_in_x + translation_vec[0]
            s_out_y = pt_in_y + translation_vec[1]
            s_out_i = (s_out_x, s_out_y)

    return s_out_i, n_hat_out_i


# --- 数据加载与准备 ---
def load_and_prepare_section_data(line_odd, line_even):
    """
    解析CSV行对, 模拟 C++ 中的数据加载、几何中心调整和必要的数据准备。
    返回包含原始和调整后数据的字典。
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
    except Exception as e:
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

    # 保存原始局部轮廓用于绘图
    original_contour_y = list(local_contour_y)
    original_contour_z = list(local_contour_z)
    original_contourY_plot = list(original_contour_y)
    original_contourZ_plot = list(original_contour_z)
    if original_contourY_plot and original_contourZ_plot:
        original_contourY_plot.append(original_contourY_plot[0])
        original_contourZ_plot.append(original_contourZ_plot[0])

    nx_norm, ny_norm = normalize_vector(normal_x, normal_y)

    z_min_adj, z_max_adj = 0.0, 0.0
    z_c_local = 0.0
    adjusted_contour_z = []
    if local_contour_z:
        z_min_local = min(local_contour_z)
        z_max_local = max(local_contour_z)
        z_c_local = (z_min_local + z_max_local) / 2.0
        adjusted_contour_z = [z - z_c_local for z in local_contour_z]
        if adjusted_contour_z:
             z_min_adj = min(adjusted_contour_z)
             z_max_adj = max(adjusted_contour_z)

    adjusted_contour_y = list(local_contour_y)

    adjusted_center_x = center_x + z_c_local * nx_norm
    adjusted_center_y = center_y + z_c_local * ny_norm
    ctrLinePtIn_adj = (adjusted_center_x, adjusted_center_y)

    contour_y_plot_adj = list(adjusted_contour_y)
    contour_z_plot_adj = list(adjusted_contour_z)
    if contour_y_plot_adj and contour_z_plot_adj:
        contour_y_plot_adj.append(contour_y_plot_adj[0])
        contour_z_plot_adj.append(contour_z_plot_adj[0])

    return {
        "ctrLinePtIn_adj": ctrLinePtIn_adj,
        "normalIn_adj": (nx_norm, ny_norm),
        "scaleIn": scale_in,
        "scaleOut": scale_out,
        "contourY_local_adj": adjusted_contour_y,
        "contourZ_local_adj": adjusted_contour_z,
        "contourY_plot_adj": contour_y_plot_adj,
        "contourZ_plot_adj": contour_z_plot_adj,
        "original_contourY_plot": original_contourY_plot,
        "original_contourZ_plot": original_contourZ_plot,
        "z_c_local": z_c_local,
        "zMinAdj_local": z_min_adj,
        "zMaxAdj_local": z_max_adj,
        "original_center": (center_x, center_y),
        "zMinLocal_orig": min(local_contour_z) if local_contour_z else 0.0,
        "zMaxLocal_orig": max(local_contour_z) if local_contour_z else 0.0,
        "ctrLinePtOut_orig": (center_x, center_y),
        "length": 0.0,
        "curvatureRadius": float('inf'),
        "curvatureAngle": 0.0,
        "ctrLinePtOut": ctrLinePtIn_adj,
        "normalOut": (nx_norm, ny_norm),
    }

# --- 角点计算 ---
def get_segment_points(section_data_i):
    """
    使用段 i 的完整几何数据计算四个角点的全局坐标。
    """
    ptIn = section_data_i["ctrLinePtIn_adj"]
    normalIn = section_data_i["normalIn_adj"]
    scaleIn = section_data_i["scaleIn"]
    ymin_local = section_data_i["zMinAdj_local"]
    ymax_local = section_data_i["zMaxAdj_local"]
    ptOut = section_data_i["ctrLinePtOut"]
    normalOut = section_data_i["normalOut"]
    scaleOut = section_data_i["scaleOut"]
    ptInMin = (ptIn[0] + normalIn[0] * ymin_local * scaleIn,
               ptIn[1] + normalIn[1] * ymin_local * scaleIn)
    ptInMax = (ptIn[0] + normalIn[0] * ymax_local * scaleIn,
               ptIn[1] + normalIn[1] * ymax_local * scaleIn)
    ptOutMin = (ptOut[0] + normalOut[0] * ymin_local * scaleOut,
                ptOut[1] + normalOut[1] * ymin_local * scaleOut)
    ptOutMax = (ptOut[0] + normalOut[0] * ymax_local * scaleOut,
                ptOut[1] + normalOut[1] * ymax_local * scaleOut)
    return ptInMin, ptInMax, ptOutMin, ptOutMax

# --- 原始角点计算 (新增) ---
def get_segment_points_original(section_data_i):
    """
    使用段 i 的 *原始* 几何数据计算四个角点的全局坐标。
    """
    ptIn = section_data_i["original_center"]
    normalIn = section_data_i["normalIn_adj"] # 使用归一化法线
    scaleIn = section_data_i["scaleIn"]
    ymin_local = section_data_i["zMinLocal_orig"]
    ymax_local = section_data_i["zMaxLocal_orig"]
    ptOut = section_data_i["ctrLinePtOut_orig"]
    normalOut = section_data_i["normalOut_orig"]
    scaleOut = section_data_i["scaleOut"]
    ptInMin = (ptIn[0] + normalIn[0] * ymin_local * scaleIn,
               ptIn[1] + normalIn[1] * ymin_local * scaleIn)
    ptInMax = (ptIn[0] + normalIn[0] * ymax_local * scaleIn,
               ptIn[1] + normalIn[1] * ymax_local * scaleIn)
    ptOutMin = (ptOut[0] + normalOut[0] * ymin_local * scaleOut,
                ptOut[1] + normalOut[1] * ymin_local * scaleOut)
    ptOutMax = (ptOut[0] + normalOut[0] * ymax_local * scaleOut,
                ptOut[1] + normalOut[1] * ymax_local * scaleOut)
    return ptInMin, ptInMax, ptOutMin, ptOutMax


# --- GUI Application Class ---
class VocalTractViewerApp:
    def __init__(self, master, font_prop=None):
        self.master = master
        self.font_prop = font_prop
        master.title("声道 CSV 查看器")
        if self.font_prop: master.option_add('*Font', self.font_prop)
        master.protocol("WM_DELETE_WINDOW", self.on_closing)

        self.all_sections_data = []
        self.num_segments = 0
        self.selected_segment_index = -1
        self.adjusted_segment_lines = []
        self.original_segment_lines = [] # 新增
        self.loaded_csv_basename = ""

        # --- Top Frame for Controls ---
        self.control_frame = tk.Frame(master)
        self.control_frame.pack(side=tk.TOP, fill=tk.X)

        # Load 按钮
        self.btn_load = tk.Button(self.control_frame, text="加载 CSV", command=self.load_csv)
        if self.font_prop: self.btn_load.config(font=self.font_prop)
        self.btn_load.pack(side=tk.LEFT, padx=5, pady=5)

        # Save Plot 按钮
        self.btn_save = tk.Button(self.control_frame, text="保存绘图", command=self.save_plot)
        if self.font_prop: self.btn_save.config(font=self.font_prop)
        self.btn_save.pack(side=tk.LEFT, padx=5, pady=5)
        self.btn_save["state"] = "disabled"

        # --- Navigation Buttons (新增) ---
        self.btn_prev = tk.Button(self.control_frame, text="< 上一段", command=self.select_prev_segment)
        if self.font_prop: self.btn_prev.config(font=self.font_prop)
        self.btn_prev.pack(side=tk.LEFT, padx=5, pady=5)
        self.btn_prev["state"] = "disabled"

        self.btn_next = tk.Button(self.control_frame, text="下一段 >", command=self.select_next_segment)
        if self.font_prop: self.btn_next.config(font=self.font_prop)
        self.btn_next.pack(side=tk.LEFT, padx=5, pady=5)
        self.btn_next["state"] = "disabled"

        # Status Label
        self.status_label = tk.Label(self.control_frame, text="请加载 CSV 文件开始")
        if self.font_prop: self.status_label.config(font=self.font_prop)
        self.status_label.pack(side=tk.LEFT, expand=True, fill=tk.X, padx=5)

        # --- Matplotlib Figure and Axes (改为 1x3) ---
        self.fig, axes = plt.subplots(1, 3, figsize=(18, 6))
        self.ax_left, self.ax_mid, self.ax_right = axes
        self.fig.suptitle("声道可视化", fontproperties=self.font_prop)

        # --- Left Plot (Adjusted Sagittal) Setup ---
        self.ax_left.set_title('调整后矢状面视图', fontproperties=self.font_prop)
        self.ax_left.set_xlabel('全局 X', fontproperties=self.font_prop)
        self.ax_left.set_ylabel('全局 Y', fontproperties=self.font_prop)
        self.ax_left.set_aspect('equal', adjustable='datalim')
        self.ax_left.grid(True)

        # --- Middle Plot (Original Sagittal) Setup (新增) ---
        self.ax_mid.set_title('原始矢状面视图', fontproperties=self.font_prop)
        self.ax_mid.set_xlabel('全局 X', fontproperties=self.font_prop)
        self.ax_mid.set_ylabel('全局 Y', fontproperties=self.font_prop)
        self.ax_mid.set_aspect('equal', adjustable='datalim')
        self.ax_mid.grid(True)

        # --- Right Plot (Cross-section) Setup ---
        self.ax_right.set_title('截面视图 (请选择分段)', fontproperties=self.font_prop)
        self.ax_right.set_xlabel('局部 Y', fontproperties=self.font_prop)
        self.ax_right.set_ylabel('局部 Z', fontproperties=self.font_prop)
        self.ax_right.set_aspect('equal', adjustable='datalim')
        self.ax_right.grid(True)

        # --- Embed Matplotlib in Tkinter ---
        self.canvas = FigureCanvasTkAgg(self.fig, master=master)
        self.canvas_widget = self.canvas.get_tk_widget()
        self.canvas_widget.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        # --- Matplotlib Toolbar ---
        self.toolbar = NavigationToolbar2Tk(self.canvas, master)
        self.toolbar.update()
        self.canvas_widget.pack(side=tk.TOP, fill=tk.X)

        # --- Connect Click Event ---
        self.fig.canvas.mpl_connect('button_press_event', self.on_click)

    def on_closing(self):
        """处理窗口关闭事件"""
        # 可以选择在这里添加确认对话框
        # if messagebox.askokcancel("Quit", "Do you want to quit?"):
        print("Closing application...")
        # 清理 matplotlib 图形，防止内存泄漏 (可选但推荐)
        plt.close(self.fig)
        self.master.destroy()

    def load_csv(self):
        filepath = filedialog.askopenfilename(
            title="Select CSV File",
            filetypes=(("CSV files", "*.csv"), ("All files", "*.*"))
        )
        if not filepath:
            return
        self.loaded_csv_basename = os.path.splitext(os.path.basename(filepath))[0]
        self.status_label.config(text=f"Loading: {os.path.basename(filepath)}...")
        self.master.update_idletasks()
        self.all_sections_data = []
        try:
            with open(filepath, 'r') as f:
                lines = f.readlines()
            if len(lines) % 2 != 0: print("Warning: Odd number of lines in CSV.")
            for i in range(0, len(lines), 2):
                if i + 1 < len(lines):
                    section = load_and_prepare_section_data(lines[i], lines[i+1])
                    if section: self.all_sections_data.append(section)
        except Exception as e:
            messagebox.showerror("Error", f"Failed to load or process file:\n{e}")
            self.status_label.config(text="Error loading file.")
            self.loaded_csv_basename = ""
            self.btn_save["state"] = "disabled"
            return
        if not self.all_sections_data:
            messagebox.showwarning("Warning", "No valid section data found in the file.")
            self.status_label.config(text="No data found.")
            self.loaded_csv_basename = ""
            self.btn_save["state"] = "disabled"
            return
        self.num_segments = len(self.all_sections_data) - 1
        if self.num_segments < 0: self.num_segments = 0
        for i in range(self.num_segments):
            data_i = self.all_sections_data[i]; data_i_plus_1 = self.all_sections_data[i+1]
            s_prime_i = data_i["ctrLinePtIn_adj"]; s_prime_i_plus_1 = data_i_plus_1["ctrLinePtIn_adj"]
            n_hat_i = data_i["normalIn_adj"]; n_hat_i_plus_1 = data_i_plus_1["normalIn_adj"]
            length = np.sqrt((s_prime_i_plus_1[0] - s_prime_i[0])**2 + (s_prime_i_plus_1[1] - s_prime_i[1])**2)
            data_i["length"] = length
            radius, angle = calculate_curvature(s_prime_i, n_hat_i, s_prime_i_plus_1, n_hat_i_plus_1)
            data_i["curvatureRadius"] = radius; data_i["curvatureAngle"] = angle
        for i in range(self.num_segments):
            data_i = self.all_sections_data[i]
            s_out_i, n_hat_out_i = calculate_outlet_geometry(
                data_i["ctrLinePtIn_adj"], data_i["normalIn_adj"], data_i["length"],
                data_i["curvatureRadius"], data_i["curvatureAngle"]
            )
            data_i["ctrLinePtOut"] = s_out_i; data_i["normalOut"] = n_hat_out_i
            data_i["normalOut_orig"] = n_hat_out_i
            
            # 添加原始出口点计算
            original_center = data_i["original_center"]
            s_out_i_orig, n_hat_out_i_orig = calculate_outlet_geometry(
                original_center, data_i["normalIn_adj"], data_i["length"],
                data_i["curvatureRadius"], data_i["curvatureAngle"]
            )
            data_i["ctrLinePtOut_orig"] = s_out_i_orig

        self.selected_segment_index = 0 if self.num_segments > 0 else -1
        self.update_plots() # 调用统一更新函数
        self.status_label.config(text=f"已加载 {len(self.all_sections_data)} 个截面 ({self.num_segments} 个分段).")
        self.btn_save["state"] = "normal"
        if self.num_segments > 0:
            self.btn_prev["state"] = "normal"
            self.btn_next["state"] = "normal"
        else:
            self.btn_prev["state"] = "disabled"
            self.btn_next["state"] = "disabled"

    def save_plot(self):
        """保存当前图形到文件"""
        if not self.all_sections_data:
            messagebox.showwarning("Save Plot", "No data loaded to save.")
            return
        default_filename = f"{self.loaded_csv_basename}_plot"
        if self.selected_segment_index >= 0:
            default_filename += f"_seg{self.selected_segment_index}"
        default_filename += ".png"
        output_filepath = filedialog.asksaveasfilename(
            title="Save Plot As",
            initialfile=default_filename,
            defaultextension=".png",
            filetypes=[
                ("PNG files", "*.png"),
                ("JPEG files", "*.jpg"),
                ("PDF files", "*.pdf"),
                ("SVG files", "*.svg"),
                ("All files", "*.*"),
            ]
        )
        if output_filepath:
            try:
                self.fig.savefig(output_filepath, dpi=300, bbox_inches='tight')
                self.status_label.config(text=f"Plot saved to: {os.path.basename(output_filepath)}")
                print(f"Plot saved successfully to {output_filepath}")
            except Exception as e:
                messagebox.showerror("Save Error", f"Failed to save plot:\n{e}")
                self.status_label.config(text="Error saving plot.")

    def draw_segment_sagittal_gui(self, ax, section_data_i, segment_index, is_selected):
        ptInMin, ptInMax, ptOutMin, ptOutMax = get_segment_points(section_data_i)
        color_in = 'red' if is_selected else 'gray'
        color_out = 'red' if is_selected else 'darkgray'
        color_upper = 'red' if is_selected else 'blue'
        color_lower = 'red' if is_selected else 'green'
        linewidth = 2.0 if is_selected else 1.0
        zorder = 15 if is_selected else 5
        lines = []
        lines.extend(ax.plot([ptInMin[0], ptInMax[0]], [ptInMin[1], ptInMax[1]], color=color_in, linestyle='-', linewidth=linewidth, zorder=zorder))
        lines.extend(ax.plot([ptOutMin[0], ptOutMax[0]], [ptOutMin[1], ptOutMax[1]], color=color_out, linestyle='-', linewidth=linewidth, zorder=zorder))
        lines.extend(ax.plot([ptInMin[0], ptOutMin[0]], [ptInMin[1], ptOutMin[1]], color=color_lower, linestyle='-', linewidth=linewidth, zorder=zorder))
        lines.extend(ax.plot([ptInMax[0], ptOutMax[0]], [ptInMax[1], ptOutMax[1]], color=color_upper, linestyle='-', linewidth=linewidth, zorder=zorder))
        return lines
        
    def draw_segment_sagittal_original_gui(self, ax, section_data_i, segment_index, is_selected):
        """绘制原始矢状图的单个分段 (高亮可选)"""
        ptInMin, ptInMax, ptOutMin, ptOutMax = get_segment_points_original(section_data_i)
        # 使用不同颜色/线型区分
        color_in = 'magenta' if is_selected else 'lightgray'
        color_out = 'magenta' if is_selected else 'silver'
        color_upper = 'magenta' if is_selected else 'lightblue'
        color_lower = 'magenta' if is_selected else 'lightgreen'
        linestyle = '-' # 使用实线
        linewidth = 2.0 if is_selected else 1.0
        zorder = 15 if is_selected else 5
        lines = []
        lines.extend(ax.plot([ptInMin[0], ptInMax[0]], [ptInMin[1], ptInMax[1]], color=color_in, linestyle=linestyle, linewidth=linewidth, zorder=zorder))
        lines.extend(ax.plot([ptOutMin[0], ptOutMax[0]], [ptOutMin[1], ptOutMax[1]], color=color_out, linestyle=linestyle, linewidth=linewidth, zorder=zorder))
        lines.extend(ax.plot([ptInMin[0], ptOutMin[0]], [ptInMin[1], ptOutMin[1]], color=color_lower, linestyle=linestyle, linewidth=linewidth, zorder=zorder))
        lines.extend(ax.plot([ptInMax[0], ptOutMax[0]], [ptInMax[1], ptOutMax[1]], color=color_upper, linestyle=linestyle, linewidth=linewidth, zorder=zorder))
        return lines

    def update_adjusted_sagittal_plot(self):
        """更新左侧调整后矢状图"""
        self.ax_left.clear()
        self.adjusted_segment_lines = []

        if not self.all_sections_data:
            self.ax_left.set_title('调整后矢状面视图 (无数据)', fontproperties=self.font_prop)
            return

        for i in range(self.num_segments):
            data_i = self.all_sections_data[i]
            is_selected = (i == self.selected_segment_index)
            lines = self.draw_segment_sagittal_gui(self.ax_left, data_i, i, is_selected)
            self.adjusted_segment_lines.append(lines)
            s_prime_i = data_i["ctrLinePtIn_adj"]
            s_out_i = data_i["ctrLinePtOut"]
            self.ax_left.plot([s_prime_i[0], s_out_i[0]], [s_prime_i[1], s_out_i[1]],
                              color='red', linestyle='--', marker='.', markersize=3,
                              linewidth=1, zorder=10,
                              label='_nolegend_' if i > 0 else '计算中心线')

        self.ax_left.set_title('调整后矢状面视图', fontproperties=self.font_prop)
        self.ax_left.set_xlabel('全局 X', fontproperties=self.font_prop)
        self.ax_left.set_ylabel('全局 Y', fontproperties=self.font_prop)
        self.ax_left.set_aspect('equal', adjustable='datalim')
        self.ax_left.grid(True)
        handles, labels = self.ax_left.get_legend_handles_labels()
        if self.num_segments > 0:
            # 用 Line2D 创建图例条目，避免重复绘制
            handles.append(plt.Line2D([0], [0], color='gray', lw=1, label='分段边界 (未选)'))
            handles.append(plt.Line2D([0], [0], color='red', lw=2, label='分段边界 (选中)'))
        # 去重图例句柄和标签
        by_label = dict(zip(labels, handles))
        self.ax_left.legend(by_label.values(), by_label.keys(), fontsize='small', prop=self.font_prop)

    def update_original_sagittal_plot(self):
        """更新中间原始矢状图"""
        self.ax_mid.clear()
        self.original_segment_lines = []

        if not self.all_sections_data:
            self.ax_mid.set_title('原始矢状面视图 (无数据)', fontproperties=self.font_prop)
            return

        for i in range(self.num_segments):
            data_i = self.all_sections_data[i]
            is_selected = (i == self.selected_segment_index)
            lines = self.draw_segment_sagittal_original_gui(self.ax_mid, data_i, i, is_selected)
            self.original_segment_lines.append(lines)
            s_prime_i = data_i["original_center"]
            s_out_i = data_i["ctrLinePtOut_orig"]
            self.ax_mid.plot([s_prime_i[0], s_out_i[0]], [s_prime_i[1], s_out_i[1]],
                             color='orange', linestyle=':', marker='x', markersize=3,
                             linewidth=1, zorder=10,
                             label='_nolegend_' if i > 0 else '原始中心线')

        self.ax_mid.set_title('原始矢状面视图', fontproperties=self.font_prop)
        self.ax_mid.set_xlabel('全局 X', fontproperties=self.font_prop)
        self.ax_mid.set_ylabel('全局 Y', fontproperties=self.font_prop)
        self.ax_mid.set_aspect('equal', adjustable='datalim')
        self.ax_mid.grid(True)
        handles, labels = self.ax_mid.get_legend_handles_labels()
        if self.num_segments > 0:
            handles.append(plt.Line2D([0], [0], color='lightgray', lw=1, label='原始边界 (未选)'))
            handles.append(plt.Line2D([0], [0], color='magenta', lw=2, label='原始边界 (选中)'))
        by_label = dict(zip(labels, handles))
        self.ax_mid.legend(by_label.values(), by_label.keys(), fontsize='small', prop=self.font_prop)

    def update_cross_section_plot(self):
        self.ax_right.clear()
        # 更新: 即使没有选中，也显示截面0作为默认值
        plot_index = self.selected_segment_index
        if plot_index < 0 and len(self.all_sections_data) > 0:
            plot_index = 0 # 默认显示第一个截面
        elif plot_index < 0:
            self.ax_right.set_title('截面视图 (无数据)', fontproperties=self.font_prop)
            return

        if plot_index >= len(self.all_sections_data):
             self.ax_right.set_title(f'截面视图 (索引 {plot_index} 无效)', fontproperties=self.font_prop)
             return

        section_data = self.all_sections_data[plot_index]
        title_suffix = f" (选中分段: {self.selected_segment_index})" if self.selected_segment_index == plot_index else " (默认)"
        current_title = f'截面 {plot_index}{title_suffix}'

        if section_data.get("original_contourY_plot"):
             self.ax_right.plot(section_data["original_contourY_plot"], section_data["original_contourZ_plot"], marker='.', markersize=3, linestyle='--', color='lightcoral', label='原始轮廓')
        if section_data.get("contourY_plot_adj"):
             self.ax_right.plot(section_data["contourY_plot_adj"], section_data["contourZ_plot_adj"], marker='o', markersize=4, linestyle='-', color='darkblue', label='居中轮廓')
        self.ax_right.axhline(y=0, color='black', linestyle='--', zorder=5, label='局部原点/居中Z')
        z_c = section_data.get("z_c_local")
        if z_c is not None:
             self.ax_right.axhline(y=z_c, color='purple', linestyle='--', zorder=6, label=f'原始Z中心 ({z_c:.2f})')

        self.ax_right.set_title(current_title, fontproperties=self.font_prop)
        self.ax_right.set_xlabel('局部 Y', fontproperties=self.font_prop)
        self.ax_right.set_ylabel('局部 Z', fontproperties=self.font_prop)
        self.ax_right.set_aspect('equal', adjustable='datalim')
        self.ax_right.grid(True)
        # 更新: 图例去重
        handles, labels = self.ax_right.get_legend_handles_labels()
        by_label = dict(zip(labels, handles))
        self.ax_right.legend(by_label.values(), by_label.keys(), fontsize='small', prop=self.font_prop)

        # 坐标轴范围调整
        all_x, all_y = [0], [0]
        if section_data.get("original_contourY_plot"): all_x.extend(section_data["original_contourY_plot"])
        if section_data.get("original_contourZ_plot"): all_y.extend(section_data["original_contourZ_plot"])
        if section_data.get("contourY_plot_adj"): all_x.extend(section_data["contourY_plot_adj"])
        if section_data.get("contourZ_plot_adj"): all_y.extend(section_data["contourZ_plot_adj"])
        if z_c is not None: all_y.append(z_c)
        if len(all_x) > 1:
            min_x, max_x = min(all_x), max(all_x); range_x = max_x - min_x or 1.0
            pad_x = 0.1 * range_x + 0.5; self.ax_right.set_xlim(min_x - pad_x, max_x + pad_x)
        if len(all_y) > 1:
            min_y, max_y = min(all_y), max(all_y); range_y = max_y - min_y or 1.0
            pad_y = 0.1 * range_y + 0.5; self.ax_right.set_ylim(min_y - pad_y, max_y + pad_y)

    # 新增: 统一的绘图更新函数
    def update_plots(self):
        """更新所有三个绘图区域"""
        self.update_adjusted_sagittal_plot()
        self.update_original_sagittal_plot()
        self.update_cross_section_plot()
        self.fig.tight_layout(rect=[0, 0.03, 1, 0.95]) # 调整布局防止重叠
        self.canvas.draw()

    def on_click(self, event):
        # 修改: 检查点击是否在左侧或中间的轴内
        if event.inaxes not in [self.ax_left, self.ax_mid] or not self.all_sections_data or self.num_segments == 0:
            return
        click_x, click_y = event.xdata, event.ydata
        if click_x is None or click_y is None: return # 避免无效点击
        clicked_segment = -1

        # 根据点击的轴选择使用哪个几何数据进行碰撞检测
        if event.inaxes == self.ax_left:
            for i in range(self.num_segments):
                data_i = self.all_sections_data[i]
                pts = get_segment_points(data_i)
                poly_path = Path([pts[0], pts[1], pts[3], pts[2], pts[0]])
                if poly_path.contains_point((click_x, click_y)):
                    clicked_segment = i
                    break
        elif event.inaxes == self.ax_mid:
            for i in range(self.num_segments):
                data_i = self.all_sections_data[i]
                pts = get_segment_points_original(data_i)
                poly_path = Path([pts[0], pts[1], pts[3], pts[2], pts[0]])
                if poly_path.contains_point((click_x, click_y)):
                    clicked_segment = i
                    break

        if clicked_segment != -1 and clicked_segment != self.selected_segment_index:
            self.selected_segment_index = clicked_segment
            self.update_plots() # 调用统一更新
            self.status_label.config(text=f"选中分段: {self.selected_segment_index}")
        elif clicked_segment != -1:
             # 如果重复点击已选中的段，可以不做任何事或给出提示
             # self.status_label.config(text=f"分段 {self.selected_segment_index} 已选中")
             pass

    # --- Navigation Button Handlers (新增) ---
    def select_prev_segment(self):
        """选择上一个分段"""
        if not self.all_sections_data or self.num_segments <= 0:
            return
        self.selected_segment_index -= 1
        if self.selected_segment_index < 0:
            self.selected_segment_index = self.num_segments - 1 # 循环
        self.update_plots()
        self.status_label.config(text=f"选中分段: {self.selected_segment_index}")

    def select_next_segment(self):
        """选择下一个分段"""
        if not self.all_sections_data or self.num_segments <= 0:
            return
        self.selected_segment_index += 1
        if self.selected_segment_index >= self.num_segments:
            self.selected_segment_index = 0 # 循环
        self.update_plots()
        self.status_label.config(text=f"选中分段: {self.selected_segment_index}")

# --- Main Execution ---
if __name__ == "__main__":
    root = tk.Tk()
    app = VocalTractViewerApp(root, font_prop=zh_font_prop)
    root.mainloop()
