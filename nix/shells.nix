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
      export GL_LIBRARY_PATH="${pkgs.libGL}/lib:${pkgs.libGLU}/lib"
      export LD_LIBRARY_PATH="${pkgs.libGL}/lib:${pkgs.libGLU}/lib:${pkgs.glew}/lib:${pkgs.ffmpeg_6}/lib:$LD_LIBRARY_PATH"

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
      configure_slicer_full() {
        local source_dir="''${1:-.}"
        local build_dir="''${2:-./build-slicer-full}"
        local install_prefix="''${3:-$build_dir/install}"

        echo "Configuring Slicer (Full)..."
        echo "Source Dir: $source_dir"
        echo "Build Dir:  $build_dir"
        echo "Install Prefix: $install_prefix"

        mkdir -p "$build_dir"
        cmake -S "$source_dir" -B "$build_dir" \
          -DCMAKE_INSTALL_PREFIX="$install_prefix" \
          -DCMAKE_BUILD_TYPE=Release \
          -DSlicer_CMake_HTTPS_Supported:BOOL=TRUE \
          -DSlicer_WC_LAST_CHANGED_DATE=1970-01-01
          # Add other Slicer full build flags here

        echo "Configuration complete. cd '$build_dir' and run 'make -j$(nproc)' or 'make install'."
      }

      configure_slicer_mini() {
        local source_dir="''${1:-.}"
        local build_dir="''${2:-./build-slicer-mini}"
        local install_prefix="''${3:-$build_dir/install}"
        # Resolve source_dir to an absolute path relative to PWD
        # Check if readlink exists, otherwise use python alternative for wider compatibility
        local absolute_source_dir
        if command -v readlink &> /dev/null; then
            absolute_source_dir="$(readlink -f "$source_dir")"
        elif command -v python &> /dev/null; then
            absolute_source_dir="$(python -c "import os, sys; print(os.path.abspath(sys.argv[1]))" "$source_dir")"
        else
            echo "Error: Cannot resolve absolute path. Need 'readlink' or 'python'." >&2
            return 1
        fi
        local slicer_source_path="$absolute_source_dir/Slicer" # Path to the Slicer subdirectory

        echo "Configuring Slicer (Minimal)..."
        echo "Source Dir: $absolute_source_dir"
        echo "Build Dir:  $build_dir"
        echo "Install Prefix: $install_prefix"
        # Adjust Extension Dir calculation to use absolute path
        local extension_source_dir="$absolute_source_dir/Modules/Scripted/Home" # Assuming Home module location
        echo "Extension Dir: $extension_source_dir"

        mkdir -p "$build_dir"
        # Use absolute paths for -S and -Dslicersources_SOURCE_DIR
        cmake -S "$absolute_source_dir" -B "$build_dir" \
          -DCMAKE_INSTALL_PREFIX="$install_prefix" \
          -DCMAKE_BUILD_TYPE=Release \
          -DSlicer_CMake_HTTPS_Supported:BOOL=TRUE \
          -DSlicer_WC_LAST_CHANGED_DATE=1970-01-01 \
          -Dslicersources_SOURCE_DIR:PATH="$slicer_source_path" # Use absolute Slicer source path

        echo "Configuration complete. cd '$build_dir' and run 'make -j$(nproc)' or 'make install'."
      }
      # --- End CMake Helper Functions ---ss

      echo "=== Environment Summary (Custom CGAL ${myCgal.version}) ==="
      echo "GCC Version: $(${pkgs.gcc}/bin/gcc --version | head -n1)"
      echo "Boost Version: ${pkgs.boost.version}"
      echo "WxWidgets Version: $(${pkgs.wxGTK32}/bin/wx-config --version)"
      echo "CGAL Version: ${myCgal.version} (custom build)"
      echo "Eigen Version: ${pkgs.eigen.version}"
      echo "FFmpeg Version: ${pkgs.ffmpeg_6.version}"
      echo "Qt5 (Base) Version: ${pkgs.qt5.qtbase.version}"
      echo "CGAL_DIR: $CGAL_DIR (custom build)"
      echo "BOOST_ROOT: $BOOST_ROOT"
      echo "wxWidgets_CONFIG_EXECUTABLE: $wxWidgets_CONFIG_EXECUTABLE"
      echo "CMAKE_PREFIX_PATH: $CMAKE_PREFIX_PATH"
      echo "LD_LIBRARY_PATH: $LD_LIBRARY_PATH"
      echo "======================================================="
      echo "nixGL Integration: Use 'nixGL <command>' or 'run_with_nixgl <command>' (Forces QT_QPA_PLATFORM=xcb)"
      echo "======================================================="
      echo -e "
CMake Helper Usage (run from project root):
  configure_slicer_mini ./miniSlicer ./build/build-mini ./build/install-mini
  cd ./build/build-mini && make -j$(nproc)
  or
  configure_slicer_full ./miniSlicer/Slicer ./build/build-full ./build/install-full
  cd ./build/build-full && make -j$(nproc)
"
      echo "Compiler cache (ccache) enabled."
      echo "CCACHE_DIR: $CCACHE_DIR"
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
      echo "=== Python Development Environment ==="
      echo "Python Version: $(python --version)"
      echo "Packages available: matplotlib, numpy, trimesh, rtree, cookiecutter, jinja2-github"
      echo "===================================="
      # Optional venv setup:
      # if [ ! -d ".venv" ]; then python -m venv .venv; fi
      # source .venv/bin/activate
    '';
  };

}
