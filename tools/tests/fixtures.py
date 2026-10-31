"""The test saves ("fixtures", out/fixtures/<name>.tgz in the main checkout) and what each one
guarantees. twoinst.start() loads the planet given here and checks the facts, so a test that
needs something the save does not have fails with that reason. New fixtures come from
tools/tests/make_fixture.py (a base fixture + commands + save), never from the player's saves.

fact = (what, console command, regex that must match its output)

Both players' own creations are fixtures too: out/fixtures/creations-host.package and
creations-guest.package become the profiles' EditorSaves.package on every cold start.
"""

FIXTURES = {
    'creature': dict(planet='Game0/Scalinus', stage='Creature',
                     facts=[('the avatar plays', 'coop.avatar', r'Creature')]),
    # BUG-0329: the Creature stage of a full campaign's own world (Cadian, its nest on a slope; lab
    # 20261030-210914 campaign#3, lane 7 host profile Game0): the village of its Tribe stage sits 5 m
    # lower on the host that made it than in the save the guests load
    'creature_cadian': dict(planet='Game0/Cadian', stage='Creature',
                            facts=[('the avatar plays', 'coop.avatar', r'Creature')]),
    'cell': dict(planet='Game0/Cadian', stage='Cell',
                 facts=[('cells in the pool', 'coop.pool', r'local_gameplay_cells=[1-9]')]),
    'tribe': dict(planet='Game0/Zorispe', stage='Tribe',
                  facts=[('the player tribe has members', 'coop.tribe', r'player'),
                         ('another tribe exists', 'coop.tribe', r'tribe \d+(?! .*player)')]),
    'civ': dict(planet='Game0/Arigine', stage='Civ',
                facts=[('a player city', 'coop.civ', r'city')]),
    # a new Civilization game (new_stage_game.py civ --npc-cities 3): the player city and three
    # cities of other nations (the nearest about 400 m away on a planet of radius 500)
    'civ_npc': dict(planet='Game0/Tavael', stage='Civ',
                    facts=[('a player city', 'coop.civ', r'city .*\(player\)'),
                           ('a city of another nation', 'coop.civ', r'city -?\d+ \S+ pol=[0-9A-F]+ owner=')]),
    # home: the PlanetID of the home planet (coop.space star prints it as "active" there)
    'space': dict(planet='Game0/Madas', stage='Space', home='00460035',
                  facts=[('in a star system', 'coop.space star', r'place 8')]),
    # `space` + interstellar drive (upgrade 4), energy upgrade 5, the colony tool, $1 100 000, two colonies
    # made by `coop.space empire colony` (P01460035 in the home system, Gabar P00460014 at the
    # tutorial's beacon star), first contact with the two nearest NPC empires and
    # 20 spice in the cargo (docs/06-testing.md 6.15 has the recipe and what it lacks)
    'space_empire': dict(planet='Game0/Madas', stage='Space', home='00460035',
                         colonies=['01460035', '00460014'], contacts=['0046102D', '00460038'],
                         facts=[('in a star system', 'coop.space star', r'place 8'),
                                ('the interstellar drive', 'coop.space empire', r'drive=1'),
                                ('a million', 'coop.space empire', r'money=[1-9]\d{6,}'),
                                ('two colonies besides the home planet', 'coop.space empire',
                                 r'colonies=([3-9]|\d\d)'),
                                ('spice stored in a colony', 'coop.space empire', r'colony (?!00460035)[0-9A-F]{8} spice=[1-9]'),
                                ('contact with another empire', 'coop.space empire', r'contacts=[1-9]'),
                                ('spice in the cargo', 'coop.space spice', r'count [1-9]')]),
}


def planet_of(name):
    f = FIXTURES.get(name)
    return f['planet'] if f else None


def check(con, name):
    """[(fact, ok)] for the loaded fixture"""
    import re
    out = []
    for what, cmd, rx in FIXTURES.get(name, {}).get('facts', []):
        try:
            text = con.call(cmd).get('out', '')
        except (OSError, ValueError):
            text = ''
        out.append((what, re.search(rx, text, re.S) is not None))
    return out
