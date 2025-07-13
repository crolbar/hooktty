{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
  inputs.dll.url = "github:crolbar/dll";

  outputs = inputs: let
    system = "x86_64-linux";
    pkgs = import inputs.nixpkgs {inherit system;};
    dll = inputs.dll.packages.${system}.default;
  in {
    devShells.${system}.default = pkgs.mkShell {
      packages = with pkgs; [
        wayland
        wayland-scanner
        clang-tools
        pkg-config
        fontconfig
        freetype
        pixman
        libxkbcommon
        dll
      ];
    };

    packages.${system}.default = pkgs.callPackage ./package.nix {inherit dll;};
  };
}
