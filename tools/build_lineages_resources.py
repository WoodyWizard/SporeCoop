"""Build Lineages icons and native cache UI without copying installed game data."""
import argparse
from pathlib import Path
from tempfile import TemporaryDirectory
from build_lineages_cache_category import cache_records
from mkpackage import build


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    specs = [f'sporecoop!{p.stem}.png={p}' for p in sorted((root / 'data/ui').glob('*.png'))]
    with TemporaryDirectory() as temporary:
        for number, (key, data) in enumerate(cache_records().items()):
            path = Path(temporary) / str(number)
            path.write_bytes(data)
            specs.append(f'{key}={path}')
        build(str(args.output), specs)


if __name__ == '__main__':
    main()
