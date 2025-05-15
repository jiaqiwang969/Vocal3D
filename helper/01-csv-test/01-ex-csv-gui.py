import tkinter as tk
from tkinter import filedialog, messagebox
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk
from matplotlib.path import Path
import numpy as np
import sys
import os
import math

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

# --- GUI Application Class ---
class VocalTractViewerApp:
    def __init__(self, master):
        self.master = master
        master.title("Vocal Tract CSV Viewer")
        # 添加关闭窗口协议处理
        master.protocol("WM_DELETE_WINDOW", self.on_closing)

        self.all_sections_data = []
        self.num_segments = 0
        self.selected_segment_index = -1
        self.segment_lines = []
        self.loaded_csv_basename = ""

        # --- Top Frame for Controls ---
        self.control_frame = tk.Frame(master)
        self.control_frame.pack(side=tk.TOP, fill=tk.X)

        self.btn_load = tk.Button(self.control_frame, text="Load CSV", command=self.load_csv)
        self.btn_load.pack(side=tk.LEFT, padx=5, pady=5)

        # 新增 Save Plot 按钮
        self.btn_save = tk.Button(self.control_frame, text="Save Plot", command=self.save_plot)
        self.btn_save.pack(side=tk.LEFT, padx=5, pady=5)
        self.btn_save["state"] = "disabled" # 初始禁用

        self.status_label = tk.Label(self.control_frame, text="Load a CSV file to begin.")
        self.status_label.pack(side=tk.LEFT, padx=5)

        # --- Matplotlib Figure and Axes ---
        self.fig, (self.ax_left, self.ax_right) = plt.subplots(1, 2, figsize=(12, 6))
        self.fig.suptitle("Vocal Tract Visualization")

        # --- Left Plot (Sagittal) Setup ---
        self.ax_left.set_title('Sagittal View')
        self.ax_left.set_xlabel('Global X')
        self.ax_left.set_ylabel('Global Y')
        self.ax_left.set_aspect('equal', adjustable='datalim')
        self.ax_left.grid(True)

        # --- Right Plot (Cross-section) Setup ---
        self.ax_right.set_title('Cross-section (Select Segment)')
        self.ax_right.set_xlabel('Local Y')
        self.ax_right.set_ylabel('Local Z')
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
        self.selected_segment_index = 0 if self.num_segments > 0 else -1
        self.update_sagittal_plot()
        self.update_cross_section_plot()
        self.status_label.config(text=f"Loaded {len(self.all_sections_data)} sections ({self.num_segments} segments).")
        self.btn_save["state"] = "normal"

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

    def update_sagittal_plot(self):
        """更新左侧矢状图"""
        self.ax_left.clear()
        self.segment_lines = []

        if not self.all_sections_data:
            self.ax_left.set_title('Sagittal View (No data)')
            self.canvas.draw()
            return

        # 绘制所有段及其中心线
        for i in range(self.num_segments):
            data_i = self.all_sections_data[i]
            is_selected = (i == self.selected_segment_index)

            # 绘制段梯形轮廓
            lines = self.draw_segment_sagittal_gui(self.ax_left, data_i, i, is_selected)
            self.segment_lines.append(lines)

            # 绘制该段的中心线
            s_prime_i = data_i["ctrLinePtIn_adj"]
            s_out_i = data_i["ctrLinePtOut"]
            self.ax_left.plot([s_prime_i[0], s_out_i[0]], [s_prime_i[1], s_out_i[1]],
                              color='red', linestyle='--', marker='.', markersize=3,
                              linewidth=1, zorder=10,
                              label='_nolegend_' if i > 0 else 'Centerline') # 只为第一个段添加图例

        self.ax_left.set_title('Sagittal View')
        self.ax_left.set_xlabel('Global X')
        self.ax_left.set_ylabel('Global Y')
        self.ax_left.set_aspect('equal', adjustable='datalim')
        self.ax_left.grid(True)
        # 修正图例，确保 Centerline 显示
        if self.num_segments > 0:
             self.ax_left.plot([],[], color='red', linestyle='--', marker='.', label='Centerline')
        self.ax_left.plot([],[], color='gray', label='Segment Boundary (Deselected)')
        self.ax_left.plot([],[], color='red', label='Segment Boundary (Selected)')
        self.ax_left.legend(fontsize='small')
        self.canvas.draw()

    def update_cross_section_plot(self):
        self.ax_right.clear()
        if self.selected_segment_index < 0 or self.selected_segment_index >= len(self.all_sections_data):
             self.ax_right.set_title('Cross-section (Select Segment)')
             self.canvas.draw()
             return
        section_data = self.all_sections_data[self.selected_segment_index]
        if section_data.get("original_contourY_plot"):
             self.ax_right.plot(section_data["original_contourY_plot"], section_data["original_contourZ_plot"], marker='.', markersize=3, linestyle='--', color='lightcoral', label='Original')
        if section_data.get("contourY_plot_adj"):
             self.ax_right.plot(section_data["contourY_plot_adj"], section_data["contourZ_plot_adj"], marker='o', markersize=4, linestyle='-', color='darkblue', label='Centered')
        self.ax_right.scatter(0, 0, color='black', s=50, zorder=5, label='Origin/Centered Z')
        z_c = section_data.get("z_c_local")
        if z_c is not None:
             self.ax_right.scatter(0, z_c, color='purple', s=100, marker='*', zorder=6, label=f'Original Z-Center ({z_c:.2f})')
        nx, ny = section_data["normalIn_adj"]
        self.ax_right.quiver(0, 0, nx, ny, angles='xy', scale_units='xy', scale=1, color='green', label=f'Normal Dir ({nx:.2f},{ny:.2f})')
        self.ax_right.set_title(f'Cross-section {self.selected_segment_index} (Z-Centering)')
        self.ax_right.set_xlabel('Local Y')
        self.ax_right.set_ylabel('Local Z')
        self.ax_right.set_aspect('equal', adjustable='datalim')
        self.ax_right.grid(True)
        self.ax_right.legend(fontsize='small')
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
        self.canvas.draw()

    def on_click(self, event):
        if event.inaxes != self.ax_left or not self.all_sections_data or self.num_segments == 0:
            return
        click_x, click_y = event.xdata, event.ydata
        clicked_segment = -1
        for i in range(self.num_segments):
            data_i = self.all_sections_data[i]
            pts = get_segment_points(data_i)
            poly_path = Path([pts[0], pts[1], pts[3], pts[2], pts[0]])
            if poly_path.contains_point((click_x, click_y)):
                clicked_segment = i
                break
        if clicked_segment != -1 and clicked_segment != self.selected_segment_index:
            print(f"Segment {clicked_segment} clicked.")
            self.selected_segment_index = clicked_segment
            self.update_sagittal_plot()
            self.update_cross_section_plot()


# --- Main Execution ---
if __name__ == "__main__":
    root = tk.Tk()
    app = VocalTractViewerApp(root)
    root.mainloop()
