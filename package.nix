{
  stdenv,
  wayland-protocols,
  wayland,
  wayland-scanner,
  libxkbcommon,
  fontconfig,
  freetype,
  pixman,
  pkg-config,
  ...
}:
stdenv.mkDerivation rec {
  pname = "hooktty";
  version = "0.1";
  src = ./.;

  buildInputs = [
    wayland-protocols
    libxkbcommon
    wayland
    pixman
  ];

  nativeBuildInputs = [
    wayland-scanner
    fontconfig
    freetype
    pkg-config
  ];

  makeFlags = ["PREFIX=$(out) BINS=${pname}"];
}
