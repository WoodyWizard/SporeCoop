"""Compare a guest's Creepy & Cute model and paints on two live games. Pick it in F9 first."""
import argparse
import re

from duo_villages import Console, command


def model(client, key):
    parts = command(client, f"coop.creations parts {key}")
    count = re.search(r"n=(\d+) cc=(\d+) unknown=0 species=1 have_pack=1\b", parts)
    paints = command(client, f"coop.creations paints {key}")
    layers = re.search(r"paints \w+ ((?:[0-9A-F]{8} ){3})cc=([1-3]) unknown=0 have_pack=1\b", paints)
    if not count or int(count[2]) == 0 or not layers:
        raise RuntimeError(f"Expected a loaded species with a pack part and paint: {parts} {paints}")
    return (int(count[1]), int(count[2]), layers[1].strip(), int(layers[2]))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--host", required=True, type=int)
    p.add_argument("--guest", required=True, type=int)
    p.add_argument("--species", required=True, help="guest's original creature instance in hex")
    args = p.parse_args()
    host, guest = Console(args.host, timeout=15), Console(args.guest, timeout=15)
    key = f"{int(args.species, 16):08X}"
    selected = re.search(r"mine creature: ([0-9A-F]{8})", command(guest, "coop.creations"))
    peer = re.findall(r"player ([1-9]\d*) creature: ([0-9A-F]{8})/", command(host, "coop.creations"))
    if not selected or selected[1] != key or len(peer) != 1:
        raise RuntimeError("Select the fixture on the only guest and wait for the host to receive it.")
    native, received = model(guest, key), model(host, peer[0][1])
    if native != received:
        raise RuntimeError(f"Guest and host disagree: {native} vs {received}")
    print(f"PASS: guest {key}, host {peer[0][1]}, {native[0]} parts ({native[1]} pack), paints {native[2]} ({native[3]} pack).")


if __name__ == "__main__":
    main()
