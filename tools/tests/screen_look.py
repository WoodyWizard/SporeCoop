"""What a player really sees, measured on a screenshot of its game (BUG-0920): the world is drawn (not a black
picture) and the stage's own HUD is there. No game state is asked: `game.mode` said Creature and `coop.avatar` was
ok while the guest's screen was black with only the mod's overlay on it.

  s = look(con, 'name.png')     {'world': mean, 'bar': mean, 'map': mean} (0-255), None without Pillow or a shot
  drawn(s), hud(s)              the world's picture is not black / the game's HUD is drawn
  text(s)                       'world 98, bar 44, map 100'

Regions as fractions of the picture (any size): `world` the middle of the view, `bar` the timeline bar along the
bottom edge, `map` the minimap at the bottom left (Cell .. Civ stages have both; measured in the Creature stage:
world 68-112, bar 44-56, map 54-101; a black screen with the mod's overlay: 0 / 0 / 0)."""
from twoinst import shot

REGIONS = {'world': (0.05, 0.15, 0.60, 0.80), 'bar': (0.22, 0.975, 0.92, 0.995), 'map': (0.02, 0.875, 0.13, 0.95)}
WORLD_MIN = 12  # a night scene is dark, never this dark; the black screen: under 1
HUD_MIN = 15


def measure(path):
    """the regions' mean brightness of an image file; None without Pillow or a readable image"""
    try:
        from PIL import Image, ImageStat
        with Image.open(path) as im:
            g = im.convert('L')
            w, h = g.size
            return {k: ImageStat.Stat(g.crop((int(a * w), int(b * h), int(c * w), int(d * h)))).mean[0]
                    for k, (a, b, c, d) in REGIONS.items()}
    except (OSError, ImportError, SyntaxError, ValueError):
        return None


def look(con, name):
    return measure(shot(con, name))


def drawn(s):
    return bool(s) and s['world'] > WORLD_MIN


def hud(s):
    return bool(s) and s['bar'] > HUD_MIN and s['map'] > HUD_MIN


def text(s):
    return 'no picture' if not s else 'world %.0f, bar %.0f, map %.0f' % (s['world'], s['bar'], s['map'])
