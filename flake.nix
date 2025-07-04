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
      ];
      FONT_DIR="${pkgs.nerd-fonts.dejavu-sans-mono}/share/fonts/truetype/NerdFonts/DejaVuSansM";
      FONT="${pkgs.nerd-fonts.dejavu-sans-mono}/share/fonts/truetype/NerdFonts/DejaVuSansM/DejaVuSansMNerdFontMono-Regular.ttf";
    };

    packages.${system}.default = pkgs.callPackage ./package.nix {};
  };
}
