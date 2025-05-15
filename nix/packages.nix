# This file defines the buildable packages for the project.
# It expects pkgs, myCgal, lib, stdenv, and src (project source) to be passed in.
{
  pkgs, myCgal, lib, stdenv, src
}: # Arguments injected by callPackage in flake.nix
let
  # Common build inputs for both Slicer packages
  commonNativeBuildInputs = [
    pkgs.pkg-config
    pkgs.gnumake
    pkgs.cmake
    pkgs.gcc12    # Ensure consistency with devShell
    pkgs.git
    pkgs.cacert
    pkgs.curl
    pkgs.perl     # Needed for OpenSSL configure
  ];

  commonBuildInputs = [
    pkgs.wxGTK32
    pkgs.eigen
    pkgs.boost
    myCgal        # Use the custom CGAL package
    pkgs.gmp
    pkgs.mpfr
    pkgs.mesa
    pkgs.libGLU
    pkgs.freeglut
    pkgs.openal
    pkgs.libGL
    pkgs.glew
    pkgs.xorg.libXi
    pkgs.xorg.libXmu
    pkgs.xorg.libXext
    pkgs.xorg.libX11
    pkgs.libglvnd
    pkgs.libxkbcommon
    pkgs.ffmpeg_6
    pkgs.ffmpeg_6.dev
    pkgs.qt5Full
    pkgs.vulkan-loader
  ];

  # Common meta attributes
  commonMeta = {
    platforms = lib.platforms.linux;
    # Consider adding maintainers = with lib.maintainers; [ yourGithubHandle ];
  };

in {
  # Full Slicer package (example, might need specific flags)
  Slicer = stdenv.mkDerivation rec {
    pname = "vtl3d-slicer-full";
    version = "0.1"; # Update as needed
    inherit src;

    nativeBuildInputs = commonNativeBuildInputs;
    buildInputs = commonBuildInputs;

    cmakeFlags = [
      "-DCMAKE_BUILD_TYPE=Release"
      "-DSlicer_CMake_HTTPS_Supported:BOOL=TRUE"
      "-DSlicer_WC_LAST_CHANGED_DATE=1970-01-01"
      # Add flags specific to the full Slicer build if any
      # Nix usually handles CMAKE_PREFIX_PATH automatically based on buildInputs
    ];

    # Allow network access for ExternalProject, etc.
    __impure = true;

    # Using Nix's default phases, but customize if needed:
    configurePhase = ''
      runHook preConfigure
      cmake -S $src/Slicer -B . $cmakeFlags
      runHook postConfigure
    '';
    # Explicitly define buildPhase to show parallel make (same as default)
    buildPhase = ''
      runHook preBuild
      make -j$NIX_BUILD_CORES # $NIX_BUILD_CORES is set by Nix
      runHook postBuild
    '';
    # Explicitly define install phase for Slicer package
    installPhase = ''
      runHook preInstall
      # Assuming the build happens in a standard Nix build directory
      cd Slicer-build  
      make install DESTDIR=$out # Assuming Slicer respects DESTDIR
      runHook postInstall
    '';

    meta = commonMeta // {
      description = "VocalTractLab 3D Slicer Application (Full Build)";
      # homepage = "...";
      license = lib.licenses.unfree; # Update license if known
    };
  };

  # Minimal Slicer build package
  miniSlicer = stdenv.mkDerivation rec {
    pname = "mini-slicer-barebones";
    version = "0.1-minimal";
    inherit src;

    nativeBuildInputs = commonNativeBuildInputs;
    buildInputs = commonBuildInputs;

    # CMake flags for minimal build (copied from original flake)
    cmakeFlags = [
      "-DCMAKE_BUILD_TYPE=Release"
      "-DSlicer_CMake_HTTPS_Supported:BOOL=TRUE"
      "-DSlicer_WC_LAST_CHANGED_DATE=1970-01-01"
      "-DBUILD_TESTING:BOOL=OFF"
      "-DSlicer_BUILD_DICOM_SUPPORT:BOOL=OFF"
      "-DSlicer_BUILD_DIFFUSION_SUPPORT:BOOL=OFF"
      "-DSlicer_BUILD_I18N_SUPPORT:BOOL=OFF"
      "-DSlicer_BUILD_USAGE_LOGGING_SUPPORT:BOOL=OFF"
      "-DSlicer_BUILD_WEBENGINE_SUPPORT:BOOL=OFF"
      "-DSlicer_USE_PYTHONQT:BOOL=ON"
      "-DSlicer_BUILD_CLI_SUPPORT:BOOL=OFF"
      "-DSlicer_BUILD_QT_DESIGNER_PLUGINS:BOOL=OFF"
      "-DSlicer_BUILD_EXTENSIONMANAGER_SUPPORT:BOOL=OFF"
      "-DSlicer_BUILD_APPLICATIONUPDATE_SUPPORT:BOOL=OFF"
      "-DSlicer_BUILD_MULTIMEDIA_SUPPORT:BOOL=OFF"
      "-DSlicer_USE_NUMPY:BOOL=OFF"
      "-DSlicer_USE_SCIPY:BOOL=OFF"
      "-DSlicer_USE_SimpleITK:BOOL=OFF"
      "-DSlicer_BUILD_DOCUMENTATION:BOOL=OFF"
      "-DSlicer_INSTALL_DEVELOPMENT:BOOL=ON"
      "-DSlicer_VTK_SMP_IMPLEMENTATION_TYPE:STRING=Sequential"
      "-DSlicer_BUILD_PARAMETERSERIALIZER_SUPPORT:BOOL=OFF"
      "-DSlicer_USE_QtTesting:BOOL=OFF"
      "-DSlicer_BUILD_BRAINSTOOLS:BOOL=OFF"
      "-DSlicer_BUILD_DataStore:BOOL=OFF"
      "-DSlicer_BUILD_CompareVolumes:BOOL=OFF"
      "-DSlicer_BUILD_LandmarkRegistration:BOOL=OFF"
      "-DSlicer_BUILD_SurfaceToolbox:BOOL=OFF"
      "-DSlicer_BUILD_MultiVolumeExplorer:BOOL=OFF"
      "-DSlicer_CLIMODULES_ENABLED:STRING=ResampleDTIVolume;ResampleScalarVectorDWIVolume"
      "-DSlicer_CLIMODULES_DISABLED:STRING="
      "-DSlicer_QTLOADABLEMODULES_ENABLED:STRING="
      "-DSlicer_QTLOADABLEMODULES_DISABLED:STRING=SceneViews;SlicerWelcome;ViewControllers"
      "-DSlicer_QTSCRIPTEDMODULES_ENABLED:STRING="
      "-DSlicer_QTSCRIPTEDMODULES_DISABLED:STRING=CropVolumeSequence;DataProbe;DICOMLib;DICOMPatcher;DMRIInstall;Endoscopy;ExtensionWizard;ImportItkSnapLabel;PerformanceTests;SampleData;ScreenCapture;SegmentEditor;SegmentStatistics;SelfTests;VectorToScalarVolume;WebServer"
      # Ensure the path is relative to the source root passed via 'src'
      "-DSlicer_EXTENSION_SOURCE_DIRS:STRING=Modules/Scripted/Home"
      # Tell CMake to use the local Slicer source directory instead of FetchContent
      "-Dslicersources_SOURCE_DIR:PATH=${src}/Slicer"
      # Python paths are generally handled by Nix finding python in buildInputs
      # Explicitly setting them might be needed if CMake struggles:
      # "-DPython_EXECUTABLE:FILEPATH=${pkgs.python3}/bin/python3"
      # "-DPython_INCLUDE_DIR:PATH=${pkgs.python3}/include/python${pkgs.python3.pythonVersion}"
      # "-DPython_LIBRARY:FILEPATH=${pkgs.python3}/lib/libpython${pkgs.python3.pythonVersion}.so"
    ];

    # Allow network access
    __impure = true;

    # Explicitly set configure phase to point CMake to the Slicer subdirectory
    configurePhase = ''
      runHook preConfigure
      # Point CMake source (-S) to the miniSlicer subdirectory
      cmake -S $src/miniSlicer -B . $cmakeFlags
      runHook postConfigure
    '';

    # Explicitly define buildPhase to show parallel make (same as default)
    buildPhase = ''
      runHook preBuild
      make -j$NIX_BUILD_CORES # $NIX_BUILD_CORES is set by Nix
      runHook postBuild
    '';

    # Explicit install phase for miniSlicer
    installPhase = ''
      runHook preInstall
      # Assuming the build happens in a standard Nix build directory
      # but the actual 'install' target is in the Slicer-build subdirectory
      cd Slicer-build  # <--- Change directory HERE
      make install DESTDIR=$out # Assuming miniSlicer respects DESTDIR
      runHook postInstall
    '';

    meta = commonMeta // {
      description = "Minimal Slicer build with Python support";
      homepage = "https://www.slicer.org/";
      license = lib.licenses.bsd3; # Slicer's license
    };
  };

} # End of the main attribute set returned 