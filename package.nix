{
  stdenv,
  wayland-protocols,
  wayland,
  wayland-scanner,
  libxkbcommon,
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
  ];

  nativeBuildInputs = [
    wayland-scanner
  ];

  makeFlags = ["PREFIX=$(out) BINS=${pname}"];
}
