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
 * 创建一个中空圆柱体网格
 * 
 * @param inner_radius 内半径
 * @param outer_radius 外半径
 * @param height 高度
 * @param sections 用于近似圆的段数
 * @param center 圆柱体中心点
 * @param axis 圆柱体轴向
 * @return 生成的中空圆柱体网格
 */
Mesh create_hollow_cylinder(
    double inner_radius,
    double outer_radius,
    double height,
    int sections,
    const Point& center,
    const Vector& axis
) {
    Mesh hollow_cylinder;
    
    // 标准化轴向向量
    Vector normalized_axis = axis / std::sqrt(axis.squared_length());
    
    // 创建两个正交向量，与轴向垂直
    Vector v1, v2;
    if (std::abs(normalized_axis.x()) > std::abs(normalized_axis.y())) {
        v1 = Vector(normalized_axis.z(), 0, -normalized_axis.x());
    } else {
        v1 = Vector(0, normalized_axis.z(), -normalized_axis.y());
    }
    v1 = v1 / std::sqrt(v1.squared_length());
    v2 = CGAL::cross_product(normalized_axis, v1);
    
    const double half_height = height / 2.0;
    Point bottom_center = center - normalized_axis * half_height;
    Point top_center = center + normalized_axis * half_height;
    
    // 存储内外圆周上的顶点索引
    std::vector<Mesh::Vertex_index> bottom_inner_vertices;
    std::vector<Mesh::Vertex_index> bottom_outer_vertices;
    std::vector<Mesh::Vertex_index> top_inner_vertices;
    std::vector<Mesh::Vertex_index> top_outer_vertices;
    
    // 创建内外圆周上的点
    for (int i = 0; i < sections; ++i) {
        double angle = 2.0 * M_PI * i / sections;
        double cos_angle = std::cos(angle);
        double sin_angle = std::sin(angle);
        
        Vector offset_inner = v1 * (inner_radius * cos_angle) + v2 * (inner_radius * sin_angle);
        Vector offset_outer = v1 * (outer_radius * cos_angle) + v2 * (outer_radius * sin_angle);
        
        Point bottom_inner_point = bottom_center + offset_inner;
        Point bottom_outer_point = bottom_center + offset_outer;
        Point top_inner_point = top_center + offset_inner;
        Point top_outer_point = top_center + offset_outer;
        
        bottom_inner_vertices.push_back(hollow_cylinder.add_vertex(bottom_inner_point));
        bottom_outer_vertices.push_back(hollow_cylinder.add_vertex(bottom_outer_point));
        top_inner_vertices.push_back(hollow_cylinder.add_vertex(top_inner_point));
        top_outer_vertices.push_back(hollow_cylinder.add_vertex(top_outer_point));
    }
    
    // 创建底面环形
    for (int i = 0; i < sections; ++i) {
        int next = (i + 1) % sections;
        hollow_cylinder.add_face(
            bottom_inner_vertices[i],
            bottom_inner_vertices[next],
            bottom_outer_vertices[next],
            bottom_outer_vertices[i]
        );
    }
    
    // 创建顶面环形
    for (int i = 0; i < sections; ++i) {
        int next = (i + 1) % sections;
        hollow_cylinder.add_face(
            top_inner_vertices[i],
            top_outer_vertices[i],
            top_outer_vertices[next],
            top_inner_vertices[next]
        );
    }
    
    // 创建外侧面
    for (int i = 0; i < sections; ++i) {
        int next = (i + 1) % sections;
        hollow_cylinder.add_face(
            bottom_outer_vertices[i],
            bottom_outer_vertices[next],
            top_outer_vertices[next],
            top_outer_vertices[i]
        );
    }
    
    // 创建内侧面
    for (int i = 0; i < sections; ++i) {
        int next = (i + 1) % sections;
        hollow_cylinder.add_face(
            bottom_inner_vertices[i],
            top_inner_vertices[i],
            top_inner_vertices[next],
            bottom_inner_vertices[next]
        );
    }
    
    return hollow_cylinder;
}

/**
 * 自定义STL写入函数，适用于CGAL 5.0.3
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
 * 生成空心圆柱体的STL文件
 * 
 * @param inner_radius 内半径
 * @param outer_radius 外半径
 * @param height 高度
 * @param output_filename 输出STL文件名
 * @param center 圆柱体中心点
 * @param axis 圆柱体轴向
 * @param sections 用于近似圆的段数
 * @return 成功返回true，失败返回false
 */
bool create_hollow_cylinder_stl(
    double inner_radius,
    double outer_radius,
    double height,
    const std::string& output_filename = "hollow_cylinder.stl",
    const Point& center = Point(0, 0, 0),
    const Vector& axis = Vector(0, 0, 1),
    int sections = 32
) {
    if (inner_radius >= outer_radius) {
        std::cerr << "错误：内半径必须小于外半径。" << std::endl;
        return false;
    }
    if (height <= 0) {
        std::cerr << "错误：高度必须为正值。" << std::endl;
        return false;
    }
    if (inner_radius <= 0) {
        std::cerr << "错误：内半径必须为正值。" << std::endl;
        return false;
    }
    
    std::cout << "生成空心圆柱体:" << std::endl;
    std::cout << "  内半径: " << inner_radius << std::endl;
    std::cout << "  外半径: " << outer_radius << std::endl;
    std::cout << "  高度: " << height << std::endl;
    std::cout << "  中心: (" << center.x() << ", " << center.y() << ", " << center.z() << ")" << std::endl;
    std::cout << "  轴向: (" << axis.x() << ", " << axis.y() << ", " << axis.z() << ")" << std::endl;
    std::cout << "  段数: " << sections << std::endl;
    std::cout << "  输出文件: " << output_filename << std::endl;
    
    try {
        // 创建中空圆柱体网格
        std::cout << "创建中空圆柱体网格..." << std::endl;
        Mesh hollow_cylinder = create_hollow_cylinder(
            inner_radius, outer_radius, height, sections, center, axis
        );
        
        if (hollow_cylinder.is_empty()) {
            std::cerr << "错误：生成的网格为空或无效。" << std::endl;
            return false;
        }
        std::cout << "网格创建完成。" << std::endl;
        
        // 导出结果网格到STL文件
        std::cout << "导出到 " << output_filename << "..." << std::endl;
        
        // 使用我们自己的函数写入STL
        if (write_mesh_to_stl(hollow_cylinder, output_filename)) {
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
    // 定义空心圆柱体参数
    double inner_r = 0.8;  // 内半径示例
    double outer_r = 1.0;  // 外半径示例
    double cyl_height = 5.0; // 高度示例
    std::string output_stl = "example_pipe.stl";
    Point cyl_center(0, 0, 0); // 圆柱体中心位于原点
    Vector cyl_axis(0, 0, 1);  // 沿Z轴对齐
    int sections = 64; // 增加段数以获得更平滑的外观
    
    // 解析命令行参数（可选）
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--inner" && i + 1 < argc) {
            inner_r = std::stod(argv[++i]);
        } else if (arg == "--outer" && i + 1 < argc) {
            outer_r = std::stod(argv[++i]);
        } else if (arg == "--height" && i + 1 < argc) {
            cyl_height = std::stod(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            output_stl = argv[++i];
        } else if (arg == "--sections" && i + 1 < argc) {
            sections = std::stoi(argv[++i]);
        }
    }
    
    // 生成STL
    bool success = create_hollow_cylinder_stl(
        inner_r,
        outer_r,
        cyl_height,
        output_stl,
        cyl_center,
        cyl_axis,
        sections
    );
    
    if (success) {
        std::cout << "\n成功生成 '" << output_stl << "'" << std::endl;
        return 0;
    } else {
        std::cout << "\n生成STL文件失败。" << std::endl;
        return 1;
    }
} 