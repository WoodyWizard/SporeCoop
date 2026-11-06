"""Package a Windows Duo build for private testing. Pass the pinned toolchain's llvm-strip."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--strip", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = root / "build"
    version = next(line.split('"')[1] for line in (root / "mods/coop/coop.cpp").read_text().splitlines()
                   if line.startswith("#define COOP_VERSION "))
    dist = root / "out/dist"
    dist.mkdir(parents=True, exist_ok=True)
    output = dist / f"SporeCoop-{version}-Windows.zip"
    with tempfile.TemporaryDirectory(dir=dist) as folder:
        stage = Path(folder)
        mod = stage / "SporeCoop"
        symbols = mod / "mods/sporekit/symbols"
        symbols.mkdir(parents=True)
        for source in (root / "data/symbols").glob("*.sym"):
            shutil.copyfile(source, symbols / source.name)
        for source in (root / "data/config").glob("*.ini"):
            shutil.copyfile(source, mod / "mods" / source.name)
        for name, target in (("coop.dll", mod / "mods/coop.dll"), ("steam_api.dll", mod / "steam_api.sporekit.dll"),
                             ("sporekit.dll", mod / "sporekit.dll"), ("SporeCoopSetup.exe", stage / "SporeCoopSetup.exe")):
            shutil.copyfile(build / name, target)
            subprocess.run([args.strip, target], check=True)
        shutil.copyfile(build / "SporeCoop.package", mod / "SporeCoop.package")
        pack = stage / f"SporeCoop-{version}.pack"
        subprocess.run([sys.executable, root / "tools/mkpack.py", stage, version, pack], check=True)
        with zipfile.ZipFile(stage / output.name, "w", zipfile.ZIP_DEFLATED) as archive:
            for file in (stage / "SporeCoopSetup.exe", pack):
                archive.write(file, file.name)
            for source, name in (("docs/DUO.md", "Start here.txt"), ("LICENSE", "LICENSE.txt"),
                                 ("THIRD_PARTY_NOTICES.md", "Notices.txt"),
                                 ("third_party/minhook/LICENSE.txt", "MinHook-LICENSE.txt")):
                archive.write(root / source, name)
        (stage / output.name).replace(output)
    print(output)


if __name__ == "__main__":
    main()
