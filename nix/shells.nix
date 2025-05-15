# This file defines the development shells.
# It expects pkgs, myCgal, myJinja2Github, and lib to be passed in.
{
  pkgs, myCgal, myJinja2Github, lib
}: {

  # Default C++ development shell
  default = pkgs.mkShell {
    name = "vtl3d-env-custom-cgal";

    nativeBuildInputs = [
      pkgs.pkg-config
      pkgs.gnumake
      pkgs.cmake
      pkgs.gcc    # Using GCC from pkgs (currently 24.05)
      pkgs.git    # Add git for ExternalProject/FetchContent
      pkgs.ccache  # <--- 添加 ccache
    ];

    buildInputs = [
      pkgs.wxGTK32
      pkgs.eigen
      pkgs.boost
      myCgal            # Custom CGAL
      pkgs.gmp
      pkgs.mpfr
      pkgs.mesa
      pkgs.freeglut
      pkgs.openal
      pkgs.libGL
      pkgs.libGLU
      pkgs.glew
      pkgs.xorg.libXi
      pkgs.xorg.libXmu
      pkgs.xorg.libXext
      pkgs.xorg.libX11
      pkgs.libglvnd
      pkgs.libxkbcommon
      pkgs.ffmpeg_6
      pkgs.ffmpeg_6.dev
      pkgs.qt5Full       # Slicer needs Qt5
      pkgs.vulkan-loader
    ];

    shellHook = ''
      # --- ccache 配置 (Simpler Method) ---
      export CCACHE_DIR="''${XDG_CACHE_HOME:-$HOME/.cache}/ccache-nix" # 指定缓存目录 (可选)
      # Ensure ccache wrapper is found first in PATH
      export PATH="${pkgs.ccache}/bin:$PATH"
      # Remove explicit CC/CXX overrides and CMake launcher settings
      # Let ccache wrapper handle the compiler interception via PATH
      # ------------------------------------

      # Clear potential conflicting variables
      unset CGAL_DIR BOOST_ROOT CMAKE_PREFIX_PATH WXWIDGETS_CONFIG_EXECUTABLE GMP_INCLUDE_DIR GMP_LIBRARIES MPFR_INCLUDE_DIR MPFR_LIBRARIES GLUT_INCLUDE_DIR GLUT_glut_LIBRARY OPENAL_INCLUDE_DIR OPENAL_LIBRARY
      # Unset compiler variables in case they were set previously
      unset CC CXX CMAKE_C_COMPILER_LAUNCHER CMAKE_CXX_COMPILER_LAUNCHER

      # Set CGAL paths (custom)
      export CGAL_DIR="${myCgal}/lib/cmake/CGAL"

      # Set Boost paths
      export BOOST_ROOT="${pkgs.boost}"
      export BOOST_INCLUDEDIR="${pkgs.boost.dev}/include"
      export BOOST_LIBRARYDIR="${pkgs.boost}/lib"

      # Set WxWidgets config path
      export wxWidgets_CONFIG_EXECUTABLE="${pkgs.wxGTK32}/bin/wx-config"

      # Set FFmpeg paths
      export FFMPEG_INCLUDE_DIR="${pkgs.ffmpeg_6.dev}/include"
      export FFMPEG_LIBRARIES="${pkgs.ffmpeg_6}/lib"
      export PKG_CONFIG_PATH="${pkgs.ffmpeg_6.dev}/lib/pkgconfig:$PKG_CONFIG_PATH"

      # Set CMAKE_PREFIX_PATH including custom CGAL and Qt5
      export CMAKE_PREFIX_PATH="${myCgal}/lib/cmake/CGAL:${pkgs.boost}:${pkgs.wxGTK32}:${pkgs.eigen}:${pkgs.gmp}:${pkgs.mpfr}:${pkgs.freeglut}:${pkgs.openal}:${pkgs.libGL}:${pkgs.libGLU}:${pkgs.glew}:${pkgs.ffmpeg_6}:${pkgs.qt5Full}"

      # Set GMP and MPFR paths
      export GMP_INCLUDE_DIR="${pkgs.gmp.dev}/include"
      export GMP_LIBRARIES="${pkgs.gmp}/lib/libgmp.so"
      export MPFR_INCLUDE_DIR="${pkgs.mpfr.dev}/include"
      export MPFR_LIBRARIES="${pkgs.mpfr}/lib/libmpfr.so"

      # Set GLUT paths
      export GLUT_INCLUDE_DIR="${pkgs.freeglut.dev}/include"
      export GLUT_glut_LIBRARY="${pkgs.freeglut}/lib/libglut.so"

      # Set OpenAL paths
      export OPENAL_INCLUDE_DIR="${pkgs.openal}/include"
      export OPENAL_LIBRARY="${pkgs.openal}/lib/libopenal.so"

      # Set OpenGL environment variables
      export GL_INCLUDE_PATH="${pkgs.libGL.dev}/include:${pkgs.libGLU.dev}/include"
      export GL_LIBRARY_PATH="${pkgs.libGL}/lib:${pkgs.libGLU}/lib:${pkgs.glew}/lib:${pkgs.ffmpeg_6}/lib:$LD_LIBRARY_PATH"

      # Ensure X11 libraries can be found
      export LD_LIBRARY_PATH="${pkgs.xorg.libX11}/lib:${pkgs.xorg.libXext}/lib:${pkgs.xorg.libXi}/lib:${pkgs.xorg.libXmu}/lib:$LD_LIBRARY_PATH"

      # Add gcc runtime libraries (libstdc++ etc.) so wrapped binaries like PythonSlicer can start
      stdcpp_path="$(${pkgs.gcc}/bin/gcc -print-file-name=libstdc++.so)"
      if [[ -n "$stdcpp_path" && -f "$stdcpp_path" ]]; then
        export LD_LIBRARY_PATH="$(dirname "$stdcpp_path"):$LD_LIBRARY_PATH"
      fi

      # Set display variable if not set
      if [[ -z "$DISPLAY" ]]; then
        export DISPLAY=:0
      fi

      # Set XDG Data Dirs for GSettings schemas
      export XDG_DATA_DIRS="${pkgs.gsettings-desktop-schemas}/share/gsettings-schemas/${pkgs.gsettings-desktop-schemas.name}:${pkgs.gtk3}/share/gsettings-schemas/${pkgs.gtk3.name}:$XDG_DATA_DIRS"

      # Set C++ standard to C++14 via CMake variables
      export CMAKE_CXX_STANDARD=14
      export CMAKE_CXX_STANDARD_REQUIRED=ON
      export CMAKE_CXX_EXTENSIONS=OFF

      # Set CMake Policies
      export CMAKE_POLICY_DEFAULT_CMP0144=NEW
      export CMAKE_POLICY_DEFAULT_CMP0167=NEW
      export CMAKE_POLICY_DEFAULT_CMP0072=NEW

      # nixGL runner function with forced XCB platform for Qt
      run_with_nixgl() {
        echo "Running (forcing XCB): $@"
        QT_QPA_PLATFORM=xcb nix run --override-input nixpkgs nixpkgs/nixos-24.05 --impure github:nix-community/nixGL -- "$@"
      }
      alias nixGL='run_with_nixgl'

      # --- CMake Helper Functions ---
      # These functions are assumed to be defined elsewhere or used if VTL3D depends on a pre-built Slicer.
      # configure_slicer_full() { ... }
      # configure_slicer_mini() { ... }
      # --- End CMake Helper Functions ---

      echo ""
      echo "======================================================="
      echo "        VocalTractLab3D 开发环境        "
      echo "======================================================="
      echo "Nix Shell 环境已激活."
      echo ""
      echo "--- 构建 VocalTractLab3D (本项目) ---"
      echo "  假设您当前位于 VocalTractLab3D 项目根目录 (例如, /home/jqwang/Work/01-Vocal3D)."
      echo "  主 CMakeLists.txt 文件预期位于子目录如 'miniVTL3D/sources/' 下."
      echo ""
      echo "  1. 进入 VTL3D 源码目录:"
      echo "     cd miniVTL3D/sources"
      echo "  2. 创建并进入构建目录:"
      echo "     mkdir -p build && cd build"
      echo "  3. 使用 CMake 配置项目:"
      echo "     cmake .."
      echo "  4. 编译项目:"
      echo "     make -j$(nproc)  # 或直接使用 'make'"
      echo ""
      echo "--- 本项目自定义构建别名 ---"
      echo "  'make_vtl': 进入 miniVTL3D/sources/build, 配置 (cmake ..) 并构建项目."
      echo "  'remake_vtl': 进入 miniVTL3D/sources/build 并运行 'make -j$(nproc)' (不清理,不重新配置)."
      echo "  'clean_vtl': 删除 miniVTL3D/sources/build 目录."
      echo ""
      alias make_vtl='(echo "执行 make_vtl: 正在配置并构建 VTL3D..." && cd miniVTL3D/sources && mkdir -p build && cd build && cmake .. && make -j$(nproc) && echo "make_vtl 已完成.")'
      alias remake_vtl='(echo "执行 remake_vtl: 正在构建 VTL3D..." && cd miniVTL3D/sources/build && make -j$(nproc) && echo "remake_vtl 已完成.")'
      alias clean_vtl='(echo "执行 clean_vtl: 正在删除 miniVTL3D/sources/build..." && rm -rf miniVTL3D/sources/build && echo "clean_vtl 已完成.")'

      echo "--- Slicer 依赖构建 (如果适用 & 使用辅助脚本) ---"
      echo "  如果 VocalTractLab3D 依赖于通过以下脚本构建的 Slicer,"
      echo "  请确保脚本 (configure_slicer_mini, configure_slicer_full) 可执行"
      echo "  并且位于您的 PATH 环境变量中或可以通过相对路径调用."
      echo "  这些脚本通常应在包含 Slicer 源码或超级构建 (superbuild) 的目录中运行."
      echo ""
      echo "  配置 Slicer (最小版本):"
      echo "    configure_slicer_mini <Slicer超级构建或源码路径> <Slicer构建目录路径> <Slicer安装目录路径>"
      echo "    示例: configure_slicer_mini ./miniSlicer ./build/SlicerMini-build ./build/SlicerMini-install"
      echo "    然后: cd <Slicer构建目录路径> && make -j$(nproc)"
      echo ""
      echo "  配置 Slicer (完整版本):"
      echo "    configure_slicer_full <Slicer源码路径> <Slicer构建目录路径> <Slicer安装目录路径>"
      echo "    示例: configure_slicer_full ./miniSlicer/Slicer ./build/SlicerFull-build ./build/SlicerFull-install"
      echo "    然后: cd <Slicer构建目录路径> && make -j$(nproc)"
      echo ""
      echo "--- 其他信息 ---"
      echo "编译器缓存 (ccache) 已启用. CCACHE_DIR: $CCACHE_DIR"
      echo "NixGL 集成: 如果GUI应用出现问题，尝试使用 'nixGL <命令>' 或 'run_with_nixgl <命令>' (强制使用 QT_QPA_PLATFORM=xcb)."
      echo "======================================================="
    '';
  };

  # Python development shell
  python = pkgs.mkShell {
    name = "vtl3d-python-env";

    buildInputs = [
      pkgs.python3                 # Default Python 3 from pkgs
      pkgs.python3Packages.matplotlib
      pkgs.python3Packages.numpy
      pkgs.python3Packages.trimesh
      pkgs.python3Packages.rtree
      pkgs.python3Packages.pip
      pkgs.python3Packages.cookiecutter
      myJinja2Github             # Custom jinja2-github
    ];

    shellHook = ''
      echo "=== Python 开发环境 ==="
      echo "Python 版本: $(python --version)"
      echo "可用包: matplotlib, numpy, trimesh, rtree, cookiecutter, jinja2-github"
      echo "==================================="
      # Optional venv setup:
      # if [ ! -d ".venv" ]; then python -m venv .venv; fi
      # source .venv/bin/activate
    '';
  };

}
