{
  lib,
  stdenv,
  fetchurl,
  cmake,
  boost,
  gmp,
  mpfr,
}:

stdenv.mkDerivation rec {
  pname = "cgal";
  version = "5.0.3";

  src = fetchurl {
    url = "https://github.com/CGAL/cgal/archive/refs/tags/v${version}.tar.gz";
    hash = "sha256-FePdpmrwnnBfAKJ5/kf0qVw70xuXMv0mSr0Pd8O0uCA=";
  };

  # note: optional component libCGAL_ImageIO would need zlib and opengl;
  #   there are also libCGAL_Qt{3,4} omitted ATM
  buildInputs = [
    boost
    gmp
    mpfr
  ];
  nativeBuildInputs = [ cmake ];

  #patches = [ ./cgal_path.patch ];

  doCheck = false;

  meta = with lib; {
    description = "Computational Geometry Algorithms Library";
    homepage = "http://cgal.org";
    license = with licenses; [
      gpl3Plus
      lgpl3Plus
    ];
    platforms = platforms.all;
    maintainers = [ maintainers.raskin ];
  };
} 