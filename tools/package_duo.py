"""Package a Windows Lineages build for private testing. Pass the pinned toolchain's llvm-strip."""
import argparse
import ctypes
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile


def embed_pack(executable, pack):
    """Add the checked update pack as Windows RCDATA resource 101."""
    from ctypes import wintypes as win
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    begin = kernel.BeginUpdateResourceW
    begin.argtypes, begin.restype = [win.LPCWSTR, win.BOOL], win.HANDLE
    update = kernel.UpdateResourceW
    update.argtypes = [win.HANDLE, ctypes.c_void_p, ctypes.c_void_p, win.WORD, ctypes.c_void_p, win.DWORD]
    update.restype = win.BOOL
    end = kernel.EndUpdateResourceW
    end.argtypes, end.restype = [win.HANDLE, win.BOOL], win.BOOL
    data = ctypes.create_string_buffer(pack.read_bytes())
    handle = begin(str(executable), False)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    if not update(handle, 10, 101, 0, data, len(data) - 1):
        error = ctypes.get_last_error()
        end(handle, True)
        raise ctypes.WinError(error)
    if not end(handle, False):
        raise ctypes.WinError(ctypes.get_last_error())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--strip", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = root / "build"
    version = next(line.split('"')[1] for line in (root / "mods/coop/coop.cpp").read_text(encoding="utf-8").splitlines()
                   if line.startswith("#define COOP_VERSION "))
    dist = root / "out/dist"
    dist.mkdir(parents=True, exist_ok=True)
    output = dist / f"SporeCoop-{version}-Windows.zip"
    subprocess.run([sys.executable, root / "tools/build_lineages_resources.py", build / "SporeCoop.package"], check=True)
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
        if sys.platform == "win32":
            embed_pack(stage / "SporeCoopSetup.exe", pack)
            standalone = dist / f"SporeCoopSetup-{version}.exe"
            shutil.copyfile(stage / "SporeCoopSetup.exe", standalone)
            print(standalone)
        with zipfile.ZipFile(stage / output.name, "w", zipfile.ZIP_DEFLATED) as archive:
            for file in (stage / "SporeCoopSetup.exe", pack):
                archive.write(file, file.name)
            for source, name in (("docs/LINEAGES-INSTALL.md", "Start here.txt"), ("LICENSE", "LICENSE.txt"),
                                 ("THIRD_PARTY_NOTICES.md", "Notices.txt"),
                                 ("third_party/minhook/LICENSE.txt", "MinHook-LICENSE.txt")):
                archive.write(root / source, name)
        (stage / output.name).replace(output)
    print(output)


if __name__ == "__main__":
    main()
