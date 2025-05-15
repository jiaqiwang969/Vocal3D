{
  description = "Development environment for VocalTractLab3D (Modular Flake)";

  inputs = {
    # Main Nixpkgs source
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.05"; # Using 24.05 stable

    # Flake utilities
    flake-utils.url = "github:numtide/flake-utils";

    # nixGL for OpenGL acceleration
    nixgl.url = "github:nix-community/nixGL";
  };

  outputs = { self, nixpkgs, flake-utils, nixgl }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        # --- Pkgs Configuration ---
        # Import nixpkgs for the current system with necessary configurations/overlays
        pkgs = import nixpkgs {
          inherit system;
          config = { allowUnfree = true; }; # Allow unfree packages if needed
          overlays = [
            nixgl.overlay # Apply nixGL overlay
            # Add other overlays here if needed
          ];
        };

        # --- Custom Local Packages ---
        # Load custom CGAL build from its dedicated file
        myCgal = pkgs.callPackage ./pkgs/cgal {
          # callPackage automatically injects dependencies like boost, gmp, mpfr from pkgs
        };

        # Load custom jinja2-github build from its dedicated file
        myJinja2Github = pkgs.callPackage ./pkgs/jinja2-github {
          # callPackage automatically injects python3Packages, fetchurl, lib from pkgs
        };

        # --- Project Source ---
        # Define the project source, cleaned of VCS files etc.
        # This assumes your flake.nix is at the root of your project source code.
        projectSrc = pkgs.lib.cleanSource ./.;

      in
      {
        # --- Development Shells ---
        # Load shell definitions and仅暴露真正 derivations，避免将 `override*` 等函数作为输出
        devShells = let
          shellsAll = pkgs.callPackage ./nix/shells.nix {
          inherit pkgs myCgal myJinja2Github;
            lib = pkgs.lib;
          };
        in {
          default = shellsAll.default;
          python  = shellsAll.python;
        };

        # --- Packages ---
        # Load package definitions并只导出派生本身，去除 override* 函数键
        packages = let
          pkgAll = pkgs.callPackage ./nix/packages.nix {
          inherit pkgs myCgal;
            lib = pkgs.lib;
            stdenv = pkgs.stdenv;
            src = projectSrc;
          };
        in {
          Slicer = pkgAll.Slicer;
          miniSlicer = pkgAll.miniSlicer;
        };

        # --- Default Package ---
        # Set the default package for `nix build`
        defaultPackage = self.packages.${system}.miniSlicer;

        # --- Formatter ---
        # Define a code formatter for `nix fmt`
        formatter = pkgs.alejandra; # Or pkgs.nixpkgs-fmt

        # --- Apps ---
        # Define runnable applications for `nix run` (optional)
        # Example:
        # apps.miniSlicer = flake-utils.lib.mkApp { drv = self.packages.${system}.miniSlicer; };
        # apps.default = self.apps.${system}.miniSlicer;

      }
    );

  # --- Nix Configuration --- (Optional)
  # Add settings for Nix commands, e.g., binary caches
  # nixConfig = {
  #   extra-substituters = [ "https://your-cache.cachix.org" ];
  #   extra-trusted-public-keys = [ "your-cache.cachix.org-1:YourPublicKey" ];
  # };
}
