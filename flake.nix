{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs = inputs: let
    system = "x86_64-linux";
    pkgs = import inputs.nixpkgs {inherit system;};
  in {
    devShells.${system}.default = pkgs.mkShell {
      packages = with pkgs; [
        wayland
        clang-tools
        pkg-config
        fontconfig
        freetype
        pixman
        libxkbcommon

        nerd-fonts.dejavu-sans-mono
      ];
    };

    packages.${system}.default = pkgs.callPackage ./package.nix {};
  };
}
