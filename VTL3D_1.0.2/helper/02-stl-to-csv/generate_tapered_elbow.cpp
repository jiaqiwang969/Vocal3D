#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Surface_mesh.h>
#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <fstream>

typedef CGAL::Exact_predicates_inexact_constructions_kernel Kernel;
typedef Kernel::Point_3 Point;
typedef Kernel::Vector_3 Vector;
typedef CGAL::Surface_mesh<Point> Mesh;

/**
 * 计算三个点定义的三角形的法向量
 * 
 * @param p1 第一个点
 * @param p2 第二个点
 * @param p3 第三个点
 * @return 法向量（已标准化）
 */
Vector compute_normal(const Point& p1, const Point& p2, const Point& p3) {
    // 计算两个边向量
    Vector v1(p2.x() - p1.x(), p2.y() - p1.y(), p2.z() - p1.z());
    Vector v2(p3.x() - p1.x(), p3.y() - p1.y(), p3.z() - p1.z());
    
    // 计算叉积
    Vector normal(
        v1.y() * v2.z() - v1.z() * v2.y(),
        v1.z() * v2.x() - v1.x() * v2.z(),
        v1.x() * v2.y() - v1.y() * v2.x()
    );
    
    // 标准化法向量
    double length = std::sqrt(normal.squared_length());
    if (length > 0) {
        normal = normal / length;
    }
    
    return normal;
}

/**
 * 创建一个45度渐缩弯管网格
 * 
 * @param start_inner_radius 起始点的内半径
 * @param start_outer_radius 起始点的外半径
 * @param end_inner_radius 结束点的内半径
 * @param end_outer_radius 结束点的外半径
 * @param bend_radius 弯管中心线的弯曲半径
 * @param sections 沿着圆弧方向的分段数
 * @param radial_sections 沿着径向的分段数
 * @param center 弯管中心点
 * @param start_direction 弯管起始方向
 * @param bend_plane_normal 弯管平面法线
 * @return 生成的弯管网格
 */
Mesh create_tapered_elbow(
    double start_inner_radius,
    double start_outer_radius,
    double end_inner_radius,
    double end_outer_radius,
    double bend_radius,
    int sections,
    int radial_sections,
    const Point& center = Point(0, 0, 0),
    const Vector& start_direction = Vector(1, 0, 0),
    const Vector& bend_plane_normal = Vector(0, 0, 1)
) {
    Mesh elbow;
    
    // 计算管壁厚度（应该在整个弯管中保持不变）
    double wall_thickness = start_outer_radius - start_inner_radius;
    double end_wall_thickness = end_outer_radius - end_inner_radius;
    
    // 如果起始和结束的壁厚不一致，输出警告并使用起始壁厚
    if (std::abs(wall_thickness - end_wall_thickness) > 1e-6) {
        std::cerr << "警告：起始壁厚(" << wall_thickness << ")与结束壁厚(" 
                  << end_wall_thickness << ")不一致，将使用起始壁厚。" << std::endl;
        // 重新计算结束外半径，使壁厚保持一致
        end_outer_radius = end_inner_radius + wall_thickness;
    }
    
    // 标准化向量
    Vector norm_start_dir = start_direction / std::sqrt(start_direction.squared_length());
    Vector norm_plane_normal = bend_plane_normal / std::sqrt(bend_plane_normal.squared_length());
    
    // 确保平面法线与起始方向垂直（如果它们不是正交的）
    double dot_product = norm_start_dir * norm_plane_normal;
    if (std::abs(dot_product) > 1e-6) {
        // 从平面法线中减去起始方向的分量，使它们正交
        norm_plane_normal = norm_plane_normal - norm_start_dir * dot_product;
        norm_plane_normal = norm_plane_normal / std::sqrt(norm_plane_normal.squared_length());
    }
    
    // 计算弯管平面的第二个方向向量（垂直于起始方向和平面法线）
    Vector bend_dir = CGAL::cross_product(norm_plane_normal, norm_start_dir);
    bend_dir = bend_dir / std::sqrt(bend_dir.squared_length());
    
    // 计算弯管的控制点（圆心）
    Point bend_center = center + norm_start_dir * bend_radius;
    
    // 创建弯管截面上的顶点数组
    std::vector<std::vector<Mesh::Vertex_index>> inner_vertices;
    std::vector<std::vector<Mesh::Vertex_index>> outer_vertices;
    
    // 弯曲角度45度，以弧度表示
    double total_angle = M_PI / 4.0;
    
    // 生成沿45度弯管的顶点
    for (int i = 0; i <= sections; ++i) {
        // 计算当前角度和进度比例
        double angle_ratio = static_cast<double>(i) / sections;
        double angle = total_angle * angle_ratio;
        
        // 计算内外半径，基于进度比例
        double inner_radius = start_inner_radius + (end_inner_radius - start_inner_radius) * angle_ratio;
        double outer_radius = inner_radius + wall_thickness;
        
        // 计算当前截面中心点在弯管路径上的位置
        // 在XY平面上，从X轴正方向开始，逆时针旋转到45度
        Vector path_direction = norm_start_dir * std::cos(angle) + bend_dir * std::sin(angle);
        Point section_center = bend_center - path_direction * bend_radius;
        
        // 截面法线应该沿着路径方向（指向弯管中心）
        Vector section_normal = path_direction;
        
        // 创建截面的局部坐标系
        // 第一个向量在截面内，垂直于平面法线和截面法线
        Vector u = CGAL::cross_product(norm_plane_normal, section_normal);
        u = u / std::sqrt(u.squared_length());
        
        // 第二个向量完成右手坐标系
        Vector v = CGAL::cross_product(section_normal, u);
        v = v / std::sqrt(v.squared_length());
        
        // 为当前截面创建顶点数组
        std::vector<Mesh::Vertex_index> inner_ring;
        std::vector<Mesh::Vertex_index> outer_ring;
        
        // 在截面上沿圆周生成顶点
        for (int j = 0; j < radial_sections; ++j) {
            double theta = 2.0 * M_PI * j / radial_sections;
            
            // 计算在截面局部坐标系中的方向向量
            Vector circle_vec = u * std::cos(theta) + v * std::sin(theta);
            
            // 生成内外圆环上的点
            Point inner_point = section_center + circle_vec * inner_radius;
            Point outer_point = section_center + circle_vec * outer_radius;
            
            // 添加到网格中
            inner_ring.push_back(elbow.add_vertex(inner_point));
            outer_ring.push_back(elbow.add_vertex(outer_point));
        }
        
        inner_vertices.push_back(inner_ring);
        outer_vertices.push_back(outer_ring);
    }
    
    // 创建面：连接相邻截面之间的顶点形成四边形
    for (int i = 0; i < sections; ++i) {
        for (int j = 0; j < radial_sections; ++j) {
            int next_j = (j + 1) % radial_sections;
            
            // 创建内部圆环面
            elbow.add_face(
                inner_vertices[i][j],
                inner_vertices[i][next_j],
                inner_vertices[i+1][next_j],
                inner_vertices[i+1][j]
            );
            
            // 创建外部圆环面
            elbow.add_face(
                outer_vertices[i][j],
                outer_vertices[i+1][j],
                outer_vertices[i+1][next_j],
                outer_vertices[i][next_j]
            );
            
            // 创建起始端口面
            if (i == 0) {
                elbow.add_face(
                    inner_vertices[0][j],
                    inner_vertices[0][next_j],
                    outer_vertices[0][next_j],
                    outer_vertices[0][j]
                );
            }
            
            // 创建结束端口面
            if (i == sections - 1) {
                elbow.add_face(
                    inner_vertices[sections][j],
                    outer_vertices[sections][j],
                    outer_vertices[sections][next_j],
                    inner_vertices[sections][next_j]
                );
            }
        }
    }
    
    return elbow;
}

/**
 * 自定义STL写入函数
 * 
 * @param mesh 要写入的网格
 * @param output_filename 输出文件名
 * @return 成功返回true，失败返回false
 */
bool write_mesh_to_stl(const Mesh& mesh, const std::string& output_filename) {
    std::ofstream out(output_filename, std::ios::binary);
    if (!out) {
        std::cerr << "无法创建输出文件: " << output_filename << std::endl;
        return false;
    }
    
    try {
        // 手动创建STL文件格式
        // STL二进制格式头部
        const char header[80] = "STL generated by CGAL Surface_mesh";
        out.write(header, 80);
        
        // 计算要写入的三角形数量
        // 注意：每个四边形需要拆分为两个三角形
        std::size_t num_triangles = 0;
        for (Mesh::Face_index f : mesh.faces()) {
            int vertex_count = 0;
            for (auto vd : CGAL::vertices_around_face(mesh.halfedge(f), mesh)) {
                (void)vd; // 防止未使用警告
                vertex_count++;
            }
            // 每个四边形拆分为2个三角形
            num_triangles += (vertex_count == 4) ? 2 : 1;
        }
        
        // 写入三角形数量
        uint32_t num_triangles_uint32 = static_cast<uint32_t>(num_triangles);
        out.write(reinterpret_cast<const char*>(&num_triangles_uint32), 4);
        
        // 写入每个三角形
        for (Mesh::Face_index f : mesh.faces()) {
            std::vector<Point> vertices;
            for (Mesh::Vertex_index v : CGAL::vertices_around_face(mesh.halfedge(f), mesh)) {
                vertices.push_back(mesh.point(v));
            }
            
            // 如果是四边形（大多数情况），拆分为两个三角形
            if (vertices.size() == 4) {
                // 第一个三角形：V0, V1, V2
                // 计算法向量
                Vector normal = compute_normal(vertices[0], vertices[1], vertices[2]);
                float nx = static_cast<float>(normal.x());
                float ny = static_cast<float>(normal.y());
                float nz = static_cast<float>(normal.z());
                
                // 写入法向量
                out.write(reinterpret_cast<const char*>(&nx), 4);
                out.write(reinterpret_cast<const char*>(&ny), 4);
                out.write(reinterpret_cast<const char*>(&nz), 4);
                
                // 写入三个顶点
                for (int i = 0; i < 3; ++i) {
                    float x = static_cast<float>(vertices[i].x());
                    float y = static_cast<float>(vertices[i].y());
                    float z = static_cast<float>(vertices[i].z());
                    out.write(reinterpret_cast<const char*>(&x), 4);
                    out.write(reinterpret_cast<const char*>(&y), 4);
                    out.write(reinterpret_cast<const char*>(&z), 4);
                }
                
                // 写入属性字节计数（通常为0）
                uint16_t attribute_byte_count = 0;
                out.write(reinterpret_cast<const char*>(&attribute_byte_count), 2);
                
                // 第二个三角形：V0, V2, V3
                // 计算法向量
                normal = compute_normal(vertices[0], vertices[2], vertices[3]);
                nx = static_cast<float>(normal.x());
                ny = static_cast<float>(normal.y());
                nz = static_cast<float>(normal.z());
                
                // 写入法向量
                out.write(reinterpret_cast<const char*>(&nx), 4);
                out.write(reinterpret_cast<const char*>(&ny), 4);
                out.write(reinterpret_cast<const char*>(&nz), 4);
                
                // 写入三个顶点
                float x = static_cast<float>(vertices[0].x());
                float y = static_cast<float>(vertices[0].y());
                float z = static_cast<float>(vertices[0].z());
                out.write(reinterpret_cast<const char*>(&x), 4);
                out.write(reinterpret_cast<const char*>(&y), 4);
                out.write(reinterpret_cast<const char*>(&z), 4);
                
                x = static_cast<float>(vertices[2].x());
                y = static_cast<float>(vertices[2].y());
                z = static_cast<float>(vertices[2].z());
                out.write(reinterpret_cast<const char*>(&x), 4);
                out.write(reinterpret_cast<const char*>(&y), 4);
                out.write(reinterpret_cast<const char*>(&z), 4);
                
                x = static_cast<float>(vertices[3].x());
                y = static_cast<float>(vertices[3].y());
                z = static_cast<float>(vertices[3].z());
                out.write(reinterpret_cast<const char*>(&x), 4);
                out.write(reinterpret_cast<const char*>(&y), 4);
                out.write(reinterpret_cast<const char*>(&z), 4);
                
                // 写入属性字节计数（通常为0）
                out.write(reinterpret_cast<const char*>(&attribute_byte_count), 2);
            } else {
                // 三角形情况（可能性较小）
                // 计算法向量
                Vector normal = compute_normal(vertices[0], vertices[1], vertices[2]);
                float nx = static_cast<float>(normal.x());
                float ny = static_cast<float>(normal.y());
                float nz = static_cast<float>(normal.z());
                
                // 写入法向量
                out.write(reinterpret_cast<const char*>(&nx), 4);
                out.write(reinterpret_cast<const char*>(&ny), 4);
                out.write(reinterpret_cast<const char*>(&nz), 4);
                
                // 写入三个顶点
                for (int i = 0; i < 3; ++i) {
                    float x = static_cast<float>(vertices[i].x());
                    float y = static_cast<float>(vertices[i].y());
                    float z = static_cast<float>(vertices[i].z());
                    out.write(reinterpret_cast<const char*>(&x), 4);
                    out.write(reinterpret_cast<const char*>(&y), 4);
                    out.write(reinterpret_cast<const char*>(&z), 4);
                }
                
                // 写入属性字节计数（通常为0）
                uint16_t attribute_byte_count = 0;
                out.write(reinterpret_cast<const char*>(&attribute_byte_count), 2);
            }
        }
        
        out.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "导出STL文件失败: " << e.what() << std::endl;
        out.close();
        return false;
    }
}

/**
 * 生成45度渐缩弯管的STL文件
 * 
 * @param start_inner_radius 起始点的内半径
 * @param start_outer_radius 起始点的外半径
 * @param end_inner_radius 结束点的内半径
 * @param end_outer_radius 结束点的外半径
 * @param bend_radius 弯管中心线的弯曲半径
 * @param output_filename 输出STL文件名
 * @param center 弯管中心点
 * @param start_direction 弯管起始方向
 * @param bend_plane_normal 弯管平面法线
 * @param sections 沿着圆弧方向的分段数
 * @param radial_sections 沿着径向的分段数
 * @return 成功返回true，失败返回false
 */
bool create_tapered_elbow_stl(
    double start_inner_radius,
    double start_outer_radius,
    double end_inner_radius,
    double end_outer_radius,
    double bend_radius,
    const std::string& output_filename = "tapered_elbow.stl",
    const Point& center = Point(0, 0, 0),
    const Vector& start_direction = Vector(1, 0, 0),
    const Vector& bend_plane_normal = Vector(0, 0, 1),
    int sections = 32,
    int radial_sections = 32
) {
    if (start_inner_radius >= start_outer_radius || end_inner_radius >= end_outer_radius) {
        std::cerr << "错误：内半径必须小于外半径。" << std::endl;
        return false;
    }
    if (start_inner_radius <= 0 || end_inner_radius <= 0) {
        std::cerr << "错误：内半径必须为正值。" << std::endl;
        return false;
    }
    if (bend_radius <= std::max(start_outer_radius, end_outer_radius)) {
        std::cerr << "错误：弯曲半径必须大于管道外半径。" << std::endl;
        return false;
    }
    
    std::cout << "生成45度渐缩弯管:" << std::endl;
    std::cout << "  起始内半径: " << start_inner_radius << std::endl;
    std::cout << "  起始外半径: " << start_outer_radius << std::endl;
    std::cout << "  结束内半径: " << end_inner_radius << std::endl;
    std::cout << "  结束外半径: " << end_outer_radius << std::endl;
    std::cout << "  弯曲半径: " << bend_radius << std::endl;
    std::cout << "  中心: (" << center.x() << ", " << center.y() << ", " << center.z() << ")" << std::endl;
    std::cout << "  起始方向: (" << start_direction.x() << ", " << start_direction.y() << ", " << start_direction.z() << ")" << std::endl;
    std::cout << "  弯管平面法线: (" << bend_plane_normal.x() << ", " << bend_plane_normal.y() << ", " << bend_plane_normal.z() << ")" << std::endl;
    std::cout << "  沿弧分段数: " << sections << std::endl;
    std::cout << "  径向分段数: " << radial_sections << std::endl;
    std::cout << "  输出文件: " << output_filename << std::endl;
    
    try {
        // 创建渐缩弯管网格
        std::cout << "创建渐缩弯管网格..." << std::endl;
        Mesh elbow = create_tapered_elbow(
            start_inner_radius, start_outer_radius,
            end_inner_radius, end_outer_radius,
            bend_radius, sections, radial_sections,
            center, start_direction, bend_plane_normal
        );
        
        if (elbow.is_empty()) {
            std::cerr << "错误：生成的网格为空或无效。" << std::endl;
            return false;
        }
        std::cout << "网格创建完成。" << std::endl;
        
        // 导出结果网格到STL文件
        std::cout << "导出到 " << output_filename << "..." << std::endl;
        
        // 使用STL写入函数
        if (write_mesh_to_stl(elbow, output_filename)) {
            std::cout << "导出完成。" << std::endl;
            return true;
        } else {
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "发生意外错误: " << e.what() << std::endl;
        return false;
    }
}

int main(int argc, char* argv[]) {
    // 默认参数
    double start_inner_r = 0.8;  // 起始内半径
    double start_outer_r = 1.0;  // 起始外半径，壁厚=0.2
    double end_inner_r = 0.5;    // 结束内半径 (缩小到起始内半径的62.5%)
    double end_outer_r = 0.7;    // 结束外半径 (起始壁厚=0.2保持不变)
    double bend_r = 4.0;         // 弯曲半径 (必须足够大，避免弯管畸形)
    std::string output_stl = "tapered_elbow.stl";
    Point center(0, 0, 0);
    Vector start_dir(1, 0, 0);    // X轴方向
    Vector bend_normal(0, 0, 1);  // Z轴向上，弯管在XY平面
    int arc_sections = 64;        // 沿弧长方向的分段数，增加以获得更平滑的弯曲
    int radial_sections = 48;     // 截面径向的分段数，增加以获得更平滑的圆周
    
    // 解析命令行参数（可选）
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--start-inner" && i + 1 < argc) {
            start_inner_r = std::stod(argv[++i]);
        } else if (arg == "--start-outer" && i + 1 < argc) {
            start_outer_r = std::stod(argv[++i]);
        } else if (arg == "--end-inner" && i + 1 < argc) {
            end_inner_r = std::stod(argv[++i]);
        } else if (arg == "--end-outer" && i + 1 < argc) {
            end_outer_r = std::stod(argv[++i]);
        } else if (arg == "--bend-radius" && i + 1 < argc) {
            bend_r = std::stod(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            output_stl = argv[++i];
        } else if (arg == "--arc-sections" && i + 1 < argc) {
            arc_sections = std::stoi(argv[++i]);
        } else if (arg == "--radial-sections" && i + 1 < argc) {
            radial_sections = std::stoi(argv[++i]);
        }
    }
    
    // 计算并显示预期壁厚
    double wall_thickness = start_outer_r - start_inner_r;
    double end_wall_thickness = end_outer_r - end_inner_r;
    std::cout << "预期壁厚: 起始=" << wall_thickness << ", 结束=" << end_wall_thickness << std::endl;
    if (std::abs(wall_thickness - end_wall_thickness) > 1e-6) {
        std::cout << "注意: 起始和结束壁厚不同，程序将使用起始壁厚 " << wall_thickness << std::endl;
        std::cout << "      结束外半径将被调整为: " << (end_inner_r + wall_thickness) << std::endl;
    }
    
    // 检查弯曲半径是否足够大
    if (bend_r <= start_outer_r * 2) {
        std::cout << "警告: 弯曲半径 (" << bend_r << ") 可能过小。为避免畸形，建议至少为外径的2倍。" << std::endl;
        std::cout << "      建议值: " << (start_outer_r * 3) << " 或更大" << std::endl;
    }
    
    // 生成渐缩弯管STL
    bool success = create_tapered_elbow_stl(
        start_inner_r, start_outer_r,
        end_inner_r, end_outer_r,
        bend_r, output_stl,
        center, start_dir, bend_normal,
        arc_sections, radial_sections
    );
    
    if (success) {
        std::cout << "\n成功生成 '" << output_stl << "'" << std::endl;
        std::cout << "起始端口位于X轴正方向，管道在XY平面上弯曲45度，结束端口朝向Y轴正方向" << std::endl;
        return 0;
    } else {
        std::cout << "\n生成STL文件失败。" << std::endl;
        return 1;
    }
} 