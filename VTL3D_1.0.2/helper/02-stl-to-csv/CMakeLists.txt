cmake_minimum_required(VERSION 3.8)
project(STLGenerator)

# 设置C++标准
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找CGAL库
find_package(CGAL REQUIRED)

# 添加可执行文件
add_executable(generate_hollow_cylinder generate_hollow_cylinder.cpp)
add_executable(generate_tapered_elbow generate_tapered_elbow.cpp)

# 添加一些编译选项
target_compile_definitions(generate_hollow_cylinder PRIVATE 
  _USE_MATH_DEFINES
)
target_compile_definitions(generate_tapered_elbow PRIVATE 
  _USE_MATH_DEFINES
)

# 链接CGAL库
target_link_libraries(generate_hollow_cylinder 
  ${CGAL_LIBRARIES}
)
target_link_libraries(generate_tapered_elbow 
  ${CGAL_LIBRARIES}
)

# 设置输出目录
set_target_properties(generate_hollow_cylinder PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
set_target_properties(generate_tapered_elbow PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

# MSVC特定设置
if(MSVC)
  target_compile_definitions(generate_hollow_cylinder PRIVATE
    _CRT_SECURE_NO_WARNINGS
    CGAL_NO_GMP
  )
  target_compile_definitions(generate_tapered_elbow PRIVATE
    _CRT_SECURE_NO_WARNINGS
    CGAL_NO_GMP
  )
endif()

# 打印配置信息
message(STATUS "CGAL库已找到: ${CGAL_FOUND}")
message(STATUS "CGAL版本: ${CGAL_VERSION}") 