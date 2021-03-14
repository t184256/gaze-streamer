{
  description = "A very simple eye tracking example";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/release-20.09";
    tobii.url = "github:t184256/tobii_eye_tracker_linux_installer";
  };

  outputs = { self, nixpkgs, tobii }: {
    packages.x86_64-linux = with import nixpkgs { system = "x86_64-linux"; }; {
      dual-gaze-streamer = stdenv.mkDerivation {
        name = "tobii-example";
        version = "1";
        src = self;
        nativeBuildInputs = [ tobii.packages.x86_64-linux.tobii-library ];
        installPhase = ''
          mkdir -p $out/bin
          cp dual-gaze-streamer $out/bin/
        '';
      };

      devShell.x86_64-linux =
        import ./shell.nix {
          pkgs = nixpkgs.legacyPackages.x86_64-linux;
          tobii = tobii.packages.x86_64-linux;
        };
    };

    defaultPackage.x86_64-linux = self.packages.x86_64-linux.dual-gaze-streamer;
  };
}
