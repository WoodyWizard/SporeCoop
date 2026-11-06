"""Use two connected disposable Tribe profiles to verify a guest species on actual villagers."""
import argparse
from pathlib import Path
import re
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from sporectl import Console


def command(client, text):
    result = client.call(text)
    if not result["ok"]:
        raise RuntimeError(f"{text}: {result['out']}")
    return result["out"]


def villages(client, key):
    output = command(client, "coop.tribe villages")
    rows = re.findall(r"village [0-9A-F]+ species=([0-9A-F]+) members=(\d+) native_player=([01])", output)
    if not rows:
        raise RuntimeError(output)
    return [(int(n), native) for species, n, native in rows if species == key]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--host", required=True, type=int)
    p.add_argument("--guest", required=True, type=int)
    p.add_argument("--planet", required=True, help="exact disposable planet file, including .spo")
    p.add_argument("--species", required=True, help="built-in creature key distinct from the host village")
    p.add_argument("--restored", action="store_true", help="verify an existing village after a host reload")
    p.add_argument("--transferred-roster", action="store_true", help="check both games received the already registered roster")
    args = p.parse_args()
    key = f"{int(args.species, 16):08X}"
    host, guest = Console(args.host, timeout=15), Console(args.guest, timeout=15)
    for client in (host, guest):
        state = command(client, "coop.avatar")
        if not state.startswith("Tribe:") or f"world '{args.planet}'" not in state:
            raise RuntimeError(f"Wrong test planet: {state.splitlines()[0]}")
        status = command(client, "coop.status")
        if not re.search(r'campaign profile: [\w.-]+-duo\d+ / Creepy & Cute', status):
            raise RuntimeError("Test requires the Duo build with Creepy & Cute loaded on both PCs.")
    if not args.restored and (villages(host, key) or villages(guest, key)):
        raise RuntimeError("Start with the clean fixture, or use --restored for a saved village.")
    command(guest, f"coop.creations choose creature {key}")
    if not args.restored:
        for _ in range(30):
            response = command(host, "coop.tribe found_guest")
            if not response.startswith("Waiting for the guest"):
                break
            time.sleep(1)
        if not response.startswith("npc tribe "):
            raise RuntimeError(response)
    for _ in range(50):
        local, remote = villages(host, key), villages(guest, key)
        if local == remote == [(3, "0")]:
            break
        time.sleep(0.4)
    else:
        raise RuntimeError(f"Native villager populations disagree: host {local}, guest {remote}")
    repeat = command(host, "coop.tribe found_guest")
    if "already has a village" not in repeat or villages(host, key) != [(3, "0")]:
        raise RuntimeError(f"Retry duplicated the village: {repeat}")
    perspective = command(guest, f"coop.tribe test_perspective {key}")
    if "Native guest village selected and original restored." not in perspective or villages(guest, key) != [(3, "0")]:
        raise RuntimeError(perspective)
    roster = command(host, "coop.tribe register_guest")
    if not roster.rstrip().endswith("guest " + key) or command(host, "coop.founders").strip() != roster.strip():
        raise RuntimeError(f"Host founding roster: {roster}")
    if args.transferred_roster and command(guest, "coop.founders").strip() != roster.strip():
        raise RuntimeError("The guest did not load the host's registered founding species.")
    print(f"PASS: {args.planet}, species {key}, 3 villagers in a second village on both games. Retry was idempotent.")


if __name__ == "__main__":
    main()
