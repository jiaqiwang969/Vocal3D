# This file defines the custom jinja2-github package.
# It expects dependencies like python3Packages, fetchurl, lib
# to be passed in by pkgs.callPackage.
{
  python3Packages, fetchurl, lib
}: # Dependencies injected by callPackage

python3Packages.buildPythonPackage rec {
  pname = "jinja2-github";
  version = "0.1.1";
  format = "setuptools"; # This package uses setup.py

  src = fetchurl { # Use fetchurl to download directly
    url = "https://files.pythonhosted.org/packages/e6/ba/9012b1a96c56f217b64b9d84509fab3ca7bee46721035b7d090503e715ed/jinja2_github-0.1.1.tar.gz";
    hash = "sha256-DWuRJl1eKv+YFYzv121Utb/tAsF37UJkPvRftvpNAY8="; # Correct hash reported by Nix
  };

  # Propagated build inputs (runtime dependencies)
  propagatedBuildInputs = [
    python3Packages.jinja2
    python3Packages.requests
    python3Packages.pygithub
  ];

  # Disable the check phase if necessary, e.g., to avoid pip dependency issues
  doCheck = false;

  # Meta information about the package
  meta = with lib; {
    description = "Jinja2 extension for generating GitHub wiki/issue links";
    homepage = "https://github.com/kpfleming/jinja2-github";
    license = licenses.mit; # MIT license
  };
} 