{
  stdenv,
  wayland-protocols,
  wayland,
  wayland-scanner,
  ...
}:
stdenv.mkDerivation rec {
  pname = "hooktty";
  version = "0.1";
  src = ./.;

  buildInputs = [
    wayland-protocols
    wayland
  ];

  nativeBuildInputs = [
    wayland-scanner
  ];

  makeFlags = ["PREFIX=$(out) BINS=${pname}"];
}
