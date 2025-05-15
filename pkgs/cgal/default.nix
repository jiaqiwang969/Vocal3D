# This file defines the custom CGAL package.
# Based on the user-provided version and patch.
{
  lib,
  stdenv,
  fetchurl,
  cmake,
  boost,
  gmp,
  mpfr,
}: # Dependencies injected by callPackage

stdenv.mkDerivation rec {
  pname = "cgal"; # Using the standard name as per the provided file
  version = "5.0.3";

  src = fetchurl {
    url = "https://github.com/CGAL/cgal/archive/refs/tags/v${version}.tar.gz";
    hash = "sha256-FePdpmrwnnBfAKJ5/kf0qVw70xuXMv0mSr0Pd8O0uCA="; # Correct hash from provided file
  };

  # Build system dependencies
  nativeBuildInputs = [ cmake ];

  # Library dependencies required by CGAL
  buildInputs = [
    boost
    gmp
    mpfr
  ];

  # Apply the patch to fix installation paths for Nix
  #patches = [ ./cgal_path.patch ];

  # Disable checks as specified
  doCheck = false;

  # Remove the preConfigure block added previously as the patch handles the path issue
  # preConfigure = ''... '' removed

  # Meta information about the package (from provided file)
  meta = with lib; {
    description = "Computational Geometry Algorithms Library";
    homepage = "http://cgal.org";
    license = with licenses; [ gpl3Plus lgpl3Plus ];
    platforms = platforms.all;
    # maintainers = [ maintainers.raskin ]; # Use your maintainer handle if desired
  };
} 