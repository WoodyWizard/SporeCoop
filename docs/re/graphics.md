# Graphics: video modes, the D3D9 device, fullscreen keys (GOG 2024, dc04aee5)

Found 2026-10-25 from a players' crash report (the host on Windows, AMD driver AMDXN32.DLL, two
identical crashes at 0x7777BE reading 0xFFFE0310) and runs on lane 2. Marks: ✔ seen at runtime or
checked in the code, ~ inferred, ? open.

## 1. The keys ✔

`AppShortcuts_OnKeyDown` 0x814180 (vtable 0x1418A00 slot 4) gets (vk, modifiers); the modifiers come
from `GetKeyState` at the key message (0x848620): 1 Shift, 2 Ctrl, 4 Alt, 0x10 Win.

| Keys | Modifiers | What |
|---|---|---|
| Ctrl+Shift+C | 3 | cheat console |
| Alt+Enter | 4 | cAppSystem vt 0x64 (`AppSystem_ToggleFullscreen` 0x7E5FB0): byte app+0x171 = 1, message 0x62752D3 |
| Alt+Shift+Enter | 5 | cAppSystem vt 0x68 (`AppSystem_NextAdapter` 0x7E5FE0): byte app+0x172 = 1 |
| Alt+F4 | 4 | quit (message 0x657ABE5) |

A command id 0x47FEBA4 (handler 0xAE0DB0) also sets +0x171. `cAppSystem::Update` 0x7E9E80 reads the
two flags: fullscreen = !current when +0x171; adapter = (current + 1) % adapter count (IRenderer
vt 0x20 / 0x24) when +0x172, else -1; then IRenderer vt 0x18 and `cViewer::SetAspectRatio`.

## 2. Renderer_SetVideoMode 0x75E9A0 ✔

IRenderer (vtable 0x140DD30, `IRenderer::Get` 0x67DD10 = [0x15FD8C0]) slot 6, thiscall(r, bool
fullscreen, int w, int h, int adapter) ret 0x10, -1 keeps the current value. The device settings
are 15 dwords at 0x1712444: +0 adapter, +8 width, +0xC height, +0x10 refresh, +0x14 format, +0x24
fullscreen (seen 0 -> 1 after Alt+Enter). The renderer keeps width/height/fullscreen at +0x5C/+0x60/
+0x6C (the test reads +0x6C).

* Same adapter: 0x11F8000 copies the settings and resets the device (0x11F7E10: callback
  [0x171242C], a loop on TestCooperativeLevel while it returns D3DERR_DEVICELOST, the callback
  [0x1712430] = 0x75D9D0 (message 0x44EDD9B "device lost"), release of the game's default-pool
  objects, `Reset` until it succeeds, callback [0x1712434] = 0x75DA10 (message 0x44EDD9C "device
  reset"). SporeKit's Reset hook (SK_EV_DEVICE_LOST/RESET, the overlay font's OnLostDevice/
  OnResetDevice) sits inside. Under Wine/DXVK every Alt+Enter was one Reset returning 0.
* Another adapter (and byte r+0x636 == 0): `cMaterialManager::ReleaseShaders` (vt 0x68), the
  device destroyed (0x11F7F40), `RenderUtils::CreateDevice` on the new adapter, `RecreateShaders`
  (vt 0x6C). **Broken in the game** (3).

Listeners of 0x44EDD9C: terrain (the terrain sphere handler 0xFA92B0 sets sphere+0xA40 = 1, and
0x44EDD9A sets it to -1), movies, the skin painter, editors (0x51E5C0, 0x51EC80) and more.

## 3. The adapter switch crashes Spore ✔

`ReleaseShaders` 0x70E550 walks the StandardShader list of cMaterialManager (+0x98..+0x9C) and turns
the 16 vertex (+0x48) and 16 pixel (+0x88) shaders of each into their bytecode (GetFunction, Release,
slot = the bytecode buffer); `RecreateShaders` 0x70E650 makes shaders from the bytecode again
(device vt 0x16C / 0x1A8) and frees it. The list holds some StandardShaders twice: 264 entries,
entries 0..3 again at 6, 7, 262, 242 and 243 (the same with and without coop.dll, at the main menu
and in the Creature stage). The second visit takes the bytecode buffer for a shader object and calls
through its first dword, the vs_3_0 version token: AV at 0x7777BE reading 0xFFFE0310, ecx =
0xFFFE0300, edi = 0x1A4 (the size of the shader before), ebp = 0x10 (the first slot). The game's
crash handler shows this as 0x7777BE <- 0x70E5B5 <- 0x75EB1F; the frames above it in the dump
(0x8490AC SetClientArea, 0x849176 SetFullscreen, 0x75ED2F) are left-overs in SetVideoMode's 64 KB
frame, not callers.

Reproduced on lane 2 (the headless display has two monitors, Wine reports two adapters,
[0x1712438] = 2) with `call App::cAppSystem::func68h thiscall <IAppSystem::Get>` and with real keys,
with SporeKit alone (`[mods] load=none`) at the main menu as well as in co-op: the same registers as
the player's report. Only machines with two or more adapters get there (two monitors, or an
integrated and a dedicated GPU: the host's 24-thread Ryzen may have both).

With the repeats skipped (a copy of the list without them for both functions, tried and dropped),
the device was recreated on adapter 1 and the next frame called through 0 at 0x11F36A1: `Lock`
(vt 0x2C) on a vertex buffer ([esi+4]) of the old device. So the game's own device recreation is
not usable. The fix (gamefixes.cpp, after 0.7.2.3) turns Alt+Shift+Enter into Alt+Enter (vt 0x68 hooked,
calls vt 0x64): a player who held Shift on Alt+Enter (Alt+Shift is also the Windows keyboard layout
switch) gets the fullscreen toggle. Test: `tools/tests/coop_alt_enter.py`.

Other callers of IRenderer vt 0x18 with an adapter: only `cAppSystem::Configure` 0x7E7430 (options
and start-up, the adapter from the config); not changed.

## 4. "Puts me underground" after Alt+Enter ?

Not reproduced under Wine/DXVK: after 2..8 Alt+Enter toggles in co-op (host and guest) the avatar's
height over the terrain (`coop.avatar`: |pos| minus `cPlanetModel::GetHeightAt`) stayed 0.0 while it
walked, the ground radius under it did not change and the pictures before and after looked the same.
The game puts a creature that is below the terrain back on it within a frame (a test moved the
avatar 3 m down: 0.0 m over the ground at the next sample, 0.5 s), so an avatar that *looks*
underground most likely stands on the collision height while the drawn terrain is higher: the
terrain is redrawn after the Reset (sphere+0xA40 = 1) and on that Windows/AMD setup it came back
different. SporeCoop does not move the local avatar (only test commands do), the puppets and NPCs it
moves are other creatures. docs/known-issues.md has the open entry.

## The skin painter's texture without a raster (2026-11-03, fix/w40-crashes)
<a id="graphics-null-raster"></a>

BUG-0454 / BUG-0371: three host crashes in the creature editor at `RenderWare::Raster::Fill`+0x7
(0x11F02C7, read 0x4) on 2026-11-01 (0.7.3.3) and 2026-11-02 18:21 / 18:34 (0.7.3.4), the same host
(GOG 2024, AMD Radeon RX 9060 XT, driver amdxn32 32.0.32015.7029, Windows 10 build 26300).

* ✔ Both 0.7.3.4 dumps: ecx = esi = ebp = 0 at Fill (`mov ebp,ecx; test [ebp+4],0x10`), the caller
  0x7B009F is `SporeCoop::Texture_BuildMips` 0x7AFFC0: `ITextureManager::Get()->vt 0x3C` returned a
  texture object (ebx, refcount +8 raised) whose raster `[tex+0]` is null, flags +4 bit 0 set (taken as
  ready, vt 0x34 not called); the level loop calls `Raster::Fill(null, image+0x28, 0)`. Called from
  0x7A95EA (FUN_007A95A0) <- the skin painter (`Skinner::cSkinPainterPipeline::ExecutePipeline`
  0x51CCE2). No coop.dll or sporekit.dll frame on either stack.
* ✔ **Not the report screenshot (BUG-0442)**: the 0.7.3.3 crash (2026-11-01 13:50) came before the
  screenshots existed; on 0.7.3.4 the guest's marks were 8 s and 65 s before the crashes, and the shot
  (StretchRect + GetRenderTargetData in Present, every surface released at once) changes no state the
  game's renderer keeps.
* ~ The raster is what the graphics driver did not make (a texture creation that failed). Address
  space was not short (0.7.3.3: 868 MB free, largest block 346 MB 12 s before; BUG-0371's "out of
  memory" verdict is superseded). Why the driver refused it is not known (only this host, only the
  editor's skin textures).
* Fix (`game.null_raster_fill`, gamefixes.cpp): Fill on a null raster returns 0, Fill's own failure
  return (Raster_Lock failed, 0x11F047D). Not verified in the game: the lab's GPU never fails a
  texture; the skin may stay without paint for that bake.

## The Pack panel and the health bar, native UI (2026-11-13, feat/lineages-47)
<a id="graphics-pack-ui"></a>

BUG-9191 (the Party panel).
* ✔ Layouts (SPUI 0x250FE9A2, group 40464200, Spore_Graphics.package and PatchData.package; Spore_EP1_Data.package
  has its own): the Pack panel B40BABD4 (window 03D98298, the "Pack" pill 03DAE412 / caption 064A90A0, slots
  03D98299 / 03D98304 with the portrait window 0614A0C4 30x30, frames 056BD170 / 056CEC38, hidden member parts
  04F656E8 / 03D99151); slot layouts EFAD77A3, 1A1A60BF, 2D192B78, 7EF4FF02, 7DC28C78, 0EB96844 (per stage, the same
  ids); the Creature HUD with the health bar 5ED10964 (also 26B068D2). Found with a scan of the packages for the
  window ids (tools/part_substitutes.py's reader).
* ✔ The health bar (window 0685A85E, 86x13 at 1024x768): the "+" icon (ImageDrawable), the number (font 727EE727), a
  clipping window 018AC46E (flags 0x413: 0x400 clips its children) as wide as the health holding the full green pill
  0665FE90 (StdDrawable), and over it an unnamed window with the edge (StdDrawable). Pixels 1:1: a pill of 58x11, a
  1-pixel edge (0,35,0), the fill a green gradient (0,252,0) on top to (0,59,0) at the bottom; the energy bar the same
  in orange.
* ✔ A StdDrawable's images are pieces of a texture sheet: the image object (vtable 01B60814) holds two pointers
  (+4, +8) and the piece's corners as floats (+0xC..+0x18), its size at +0x1C / +0x20 (32 x 32 here); no resource key
  seen in it.
* ~ Putting a slot layout (EFAD77A3) loaded with sui::load_layout into the main window with every child made visible
  overflowed the stack twice (test game 8): not used.
* ✔ A sui label draws no drawable; a window of the game's image kind (UTFWin::IImageDrawable::AddImageWindow) draws a
  128x128 creation picture scaled into its area. A StdDrawable (the tooltip's, mode 2: cut in thirds) shows nothing of
  a 128-pixel picture in a 45-pixel window; modes 0, 1, 3, 4 neither (in a label).
* ~ Sporepedia::ShopperRequest::Show with currentSelection (+0x18) = a creature's key opens the Sporepedia browser
  (Everything); it did not open that creature's card, also with +0x2A = 1 (the "Close" button then). A partner's
  creation under its shared key is not in the browser's lists (Lineages Cache lists them under other keys).
* ✔ (live 2026-11-13) The creature's own card: Sporepedia::ShopperRequest::Show (= SP::cSPUIAssetBrowser::AdvancedLaunch)
  opens the large card when the request has +0x24 set, a key at +0x18 and **+0x13 clear** (the constructor sets +0x13 = 1:
  the grid, which is what we saw first). A partner's shared key (0x7C...., a mod package) opens its card too
  (Slinkodum on the host), and the host's world species on a guest (Gentmin).
* ✔ (crash dump 2026-11-13, BUG-9196) cPlayer::GetConsequenceTrait 0xC75880 (thiscall(stage id) ret 4: +0x10FC Cell
  A426730B, +0x1100 Creature AD56080C, +0x1104 Tribe F71FA311, +0x1108 Civ BEB528CB, +0x110C Space 2DB6DAD3), called
  without a null check by a Simulator card data class (vtable 0x147CA48, slot 0x8C = 0xDD1000) through
  cGameNounManager +0x74 (the player): a card kept from a game and drawn in the galaxy menu read 0 + 0x10FC.
* ~ Native UI scale (research 2026-11-13): no setting in Properties.txt / ConfigManager.txt, no command-line option, no
  known mod. UTFWin windows have four transforms (+0xC4 local, +0x108 its inverse, +0x14C render, +0x190 the inverse
  global; spore-decomp) and the hit tests go through the inverse; a window proc gets kMsgTransformed 0x15 (OnTransform)
  as the game's own InflateEffect (button growth) uses. The route for a "scale Spore's HUD" setting: a scale transform
  on the HUD roots (minimap, Pack, ability bar, avatar card), not a global one. Not tried yet.
  ~ superseded (2026-11-14): see #graphics-native-scale.

## Native sliders, Settings layout and UI scale (2026-11-14, feat/lineages-47)
<a id="graphics-native-scale"></a>

* ✔ UTFWin::ISlider::Create 0x985AC0 (GOG 2024 / Steam; found as the only push of "UTFWin/EA::UTFWinControls::WinSlider"
  in the running Steam game, image +0x90000): stdcall(int, allocator) ret 8, as IButton::Create; ISlider = Cast
  0x0F00A8A0; vt 0x14 SetOrientation (1 horizontal, 2 vertical), 0x1C SetValue(v, notify), 0x20 GetValue, 0x24 / 0x2C
  min / max, 0x34 / 0x38 the slider drawable. The Audio page's Music slider is 0x8BDD39EC (values 0..1000).
* ✔ A SliderDrawable here is 0x28 bytes (ModAPI: 0x1C): +0 IDrawable vt, +4 DefaultLayoutElement vt, +8 the reference
  count, +0xC the ISliderDrawable vt (what GetSliderDrawable returns), +0x10 images[3] (background, thumb, thumb
  container), +0x1C a float 1.4. The native slider draws no background; the container (38x130) is the groove with its
  ticks, stretched to the window. **The thumb image is a strip of 4 states side by side**: the thumb is a quarter of the
  image wide (a single 13x27 knob drew 3 px wide); the idle knob draws the second frame.
* ✔ The native text labels of the Settings pages ("Shadows", "Screen Size", "Full Screen Mode") are not buttons:
  sui::button_like on them gives nothing; sui::label with their fonts works. The Graphic cache field's spinner drawable
  (0x046170A4) and frame draw nothing in a window of ours.
* ✔ The Settings dialog: root (a 44x38 window, its pieces placed around it) holding the frame (19 pieces), two
  backgrounds, Accept 0x043B7A18, Cancel 0x043C8FD8 and the settings window 0x043DE590 (tabs, title, the pages 0x046967C0);
  the Graphic page 0x0437A968: the quality table, the cache box, Defaults, Screen Size (combo 0x046170A1), Full Screen
  (0x061C90B8, check box 0x05B591C0, a toggle button: state flag 4). Moving and growing these with set_area holds while
  the dialog lives (it is kept between openings).
* ✔ Window transforms (Window +0xC0 local, +0x104 its inverse, +0x148 as drawn, +0x18C the inverse global; each 0x40
  floats then the type: 0 identity, 1 translation and scale). Writing the local matrix does nothing: the window
  manager rebuilds it and sends it to the window's procs as kMsgTransformed (0x15, Message +0x18 the matrix), the way
  InflateEffect grows a button; a proc with event flags 0xFFFF gets it (0x80 alone does not). Scaling x[0] / x[5] there
  scales the window about its own middle; InvalidateTransform (vt 0x98) asks for it again.
* ✔ Hit tests: a scaled window is drawn scaled but the game hit-tests plain windows at their unscaled places (a slider
  took the mouse at its old size, a Cancel did nothing where drawn). Window flag 0x20 makes the hit test of that window
  go through its transform, but not the controls' own mouse use. Windows with a transform of their own (InflateEffect
  menu buttons) are hit-tested through the full transform. coop_ui ns_remap moves the mouse message's point to the
  piece's unscaled place unless the window under it has its own transform.
  ~ superseded (2026-11-14): the main menu's items took the real point one row low too (Create opened Share); the rule
  now asks the game: IWindowManager vt 0x44 GetWindowAtPosition(const Point&) (x, y floats) returns the window the
  game's hit test finds; the remap keeps the real point when that is the window drawn there, else moves it.
* ✔ The Screen Size control 0x046170A1 is a button (caption "1366 x  768 @  60Hz"), not a combo box (IComboBox cast
  fails); its list is a popup of one hidden button per display mode (children of the control, [10 10 85 30] until laid
  out), laid out 20 px apart around the selected one (place 0, beside the control) over a plain background window,
  with scroll arrows: Spore shows only as many as fit (27 of 32 at 1280x960) and never lays out the others. A pick is
  **the button under the pointer** (a moved button picked its own mode). Spore saves the chosen mode as its place in
  the mode list: Preferences.prop "OptionScreenSize 21" (a text line), so the Direct3D mode list must stay whole (a
  filtered GetAdapterModeCount / EnumAdapterModes made an Accept with no current entry save another mode).
* ✔ The renderer's device settings (Steam/GOG 2024 runtime 0x17A2444; 0x1712444 + the image shift): +8 width, +0xC
  height, +0x10 refresh, +0x14 format, +0x24 fullscreen; in a window they hold the window (1280x960 @ 60), not the
  Settings list's mode.

## The whole UI scaled as one picture (2026-11-14, feat/lineages-47)
<a id="graphics-whole-ui-scale"></a>

* ✔ (runtime, TITAN, Creature at 1024 x 768) Spore's UI tree: the main window (0xFFFFFFF0) holds ~20 full-screen
  layers (0x05B598F6 ... 0x3F608B3E, flags 0x13); the HUD pieces sit in full-screen holders (id 0xFFFFFFFF) inside
  them, placed by SimpleLayout anchors (ModAPI SimpleLayout: anchors top 1, bottom 2, left 4, right 8). Setting a
  holder's area (vt 0x60) to a smaller screen and calling Revalidate (vt 0x94) on the windows in it lays the HUD out
  for that screen (the minimap, ability bar, avatar card and bottom bar moved to its edges).
* ✔ The main window's area clips the whole UI: shrunk to the small screen, everything drawn past it (in screen pixels,
  not scaled) was cut off; it keeps the screen and only the holders shrink.
* ✔ The main window gets no kMsgTransformed; each layer (the main window's child) does: its matrix scaled by f with
  the pivot at the screen's top left draws everything in it scaled, children included.
* ✔ The coop_ui mouse remap (each piece's drawn transform, the game's hit test asked) works unchanged with it: a
  click on the scaled respawn check, the pause button, the Options menu's Settings, a drag of the Interface Size slider
  and the Settings' check all hit (125 %).
* ✔ Not anchored: the Creature HUD's Pack button (0x03D98298 under layer 0x08D0F4FB, at x = screen - 44) is placed in
  code; the hints over creatures ("to learn to charm", 0x05246048 in a holder of layer 0xCBDF6E1D) are placed by the
  world's projection each time (scaled, they left their creature): that layer is not scaled.
* ~ The creature editor at 125 %: everything scales together, but the part category tabs (top left) came out smaller
  than at 100 % (not looked into yet).
* ModAPI headers read for this: Spore ModAPI UTFWin/IWindow.h (WindowFlags: kWinFlagClip 0x400; vt 0x94 Revalidate),
  SimpleLayout.h, Message.h (kMsgLayout's area at +0x18).

## The game's address space over a session: what grows (2026-11-06, fix/w46-addr-space)
<a id="graphics-address-space"></a>

BUG-0631 (a host crashed in Civ after ~2 h with the 2 GB space full), BUG-0621 (a Cell guest 3502 -> 1427 MB in 6 min).
Measured with the census (docs/06-testing.md#testing-mem-census) on lanes 4 / 5 (Wine: 4 GB, so nothing ran out; the
numbers are the growth).
* ✔ The game's heap is EA's GeneralAllocator (MallocInternal 0x927E70, MallocAlignedInternal 0x928570): it grows by
  VirtualAlloc'ed 16 MB segments and MMapMalloc 0x9271A0 (VirtualAlloc MEM_RESERVE|MEM_COMMIT, MEM_TOP_DOWN when
  `this+0x498`) for big blocks; `this+0x48C` / `+0x488` count the mapped bytes / blocks. Freed memory goes back to the
  segments, segments are not given back: the heap is a high-water mark. A 16 MB step in a census is one segment.
* ✔ Almost all growth during play is `priv:by:sporeapp.exe` (the game's heap); the graphics (d3d9.dll = DXVK here) took
  one 16 MB block in 10 min of Tribe; our DLLs nothing measurable (the "coop.dll" caller of one 16 MB segment was a
  stale return address of the tracker itself, fixed in the tracker).
* ✔ Skin textures: allocations of 5.3 MB each from the Skinner code (callers 0x513D4B / 0x5600C0 through operator
  new 0x42E148, which names its EASTL allocator "Editor"): one per creature skin painted (a 1024x1024 RGBA texture
  with its mip chain is 5.3 MB). Others seen: material / shader code around 0x7200C8..0x738862, ModelBake
  0x47667E, resource reads (cPropManager::ReadResource 0x6AEED7), effects (Swarm::EffectWorld::CreateEffect
  0xA71408).
* ✔ Creature, two players, the play_creature bots, 20 min (lab 20261106-200638): host -16 MB free, guest -7; the same
  with the guest's session left (not a clean solo: the guest joined again) -24 / -10. Normal lab play does not drain.
* ✔ Creature, the guest goes to a new place of the planet every 20 s (61 places, lab 20261106-202803): the host's heap
  +62 MB, the guest's +28; the same with `npc.view_centres` off (the host keeps nothing alive around the guest,
  BUG-0280; lab 20261106-204954, 10 min) host +3, guest +4 against +28 / +51 with it on (20261106-204949). The host
  pays for what lives around its partner (herds, nests, spawns and their models and skins) and the guest for the
  proxies of the host's NPCs it then shows.
* ✔ It is a cache, not a leak: the guest cycling over the same 12 places for 24 min (6 rounds, lab 20261106-210804):
  host +28 MB in the first round, +3 MB in the 5 rounds after, guest +33 MB then 0. Removing and showing the partner's
  puppet 60 times (`coop.puppet respawn`, 20261106-211958) cost 0 MB on the host, +6 MB on the guest once.
* ✔ Civ and Tribe idle for 10 min (two players, 20261106-213314; solo 20261106-213316): flat after the load. play_civ
  10 min (lab 20261106-214428): host 3442 -> 3378, guest 3401 -> 3316 MB, most of it in the first 4 minutes
  (new vehicle and building models); play_tribe 10 min (20261106-214428-2): host 3450 -> 3403, guest 3427 -> 3389.
* Recorded lab results (every `*.game.log` of the main checkout and the worktrees, per-minute perf lines, a stage stay
  of over 4 min): median loss Creature host 4.5 / guest 3.3 MB/min, Tribe 4.8 / 3.8, Civ 5.4 / 4.7, Space 2.2 / 3.9,
  Cell 0.9 / 0.1; the host loses more than the guest in every planet stage.
* ✔ Cell (BUG-0621): a crowd of 1000 of the host's cells around its own (`coop.pool crowd 1000 8` every 20 s, lab
  20261106-210201) with BUG-0355's fixes off (cell.gfx_full, cell.spare): the host loses 8 MB/min, linear, from
  effects and animations (Swarm::EffectWorld::CreateEffect 0xA71408, Anim::AnimatedCreature::PlayAnimation 0xA0F707:
  the per-frame CreateSoftBody calls on a full graphics pool set up the cell again and play an animation each time,
  BUG-0355); with them on (20261106-211056) 0. The 4-player guest of BUG-0621 (0.7.3.1, before BUG-0355's fix in
  0.7.3.2) had 900 mirrors on screen: the same case.
* Inferred (the player's host, Discord 2026-11-06): its 740 MB in 50 min of Creature are this cache filled by two
  players exploring a planet with many species (each new species: a skin of 5.3 MB, its models and materials, on
  both games), twice on the host because it also keeps the world around its partner alive. Stage changes give little
  back (the heap's segments stay).
* Negative: no allocation of ours grows; no growth per puppet, per mirror or per revisit.

## A fixed camera on the main viewer (2026-11-10, feat/w47-visual)
<a id="graphics-fixed-camera"></a>

✔ seen at runtime in Creature and Tribe (lane 6, GOG 2024), ✔ Ghidra.
* The main viewer is `IGameModeManager::Get()` vt 0x58. viewer+0 is the camera-to-world matrix (rows right, forward,
  up, position; row determinant +1 in both stages), [viewer+0x170] the camera (+0x5C / +0x60 the half view window at
  distance 1, +0x6C 1 = perspective, +0x78 the D3D viewport).
* Every frame the active camera's Update (from `cCameraManager::Update` 0x7C6440, through its vtable at 0x1410744)
  writes the viewer with `cViewer::SetViewTransform` 0x7C4DD0 (a Math::Transform: flags 2|4, count, offset, scale,
  rotation rows right / forward / up; `GetViewTransform` 0x7C41C0 gives one). A transform set right after
  `cCameraManager::Update` returns is the one the frame is drawn with: the hook of `coop.view` (mods/coop/test_view.cpp)
  put the camera at a given eye in the first frame, `coop.cam` read it back exactly (0.000 m), the screenshot showed
  that view, and the game's camera came back on its own the frame after the hook stopped.
* `cViewer::SetPerspectiveProjection` 0x7C5480 takes the VERTICAL field of view in degrees (tan(fov/2) into +0x60, +0x5C
  scaled to keep the aspect); Creature's follow camera draws with +0x60 = 0.4097 (about 44.6 degrees), Tribe's likewise.
  This confirms the fork's viewer notes of [the mating fork notes](creature.md#creature-fork-mating-fov) for
  SetPerspectiveProjection, SetViewWindow and the camera fields.
* The relevance pass (`Relevance_Tick` 0xAD2300) centres on the viewer: a fixed camera 25 m from the players keeps the
  same NPCs alive (no change seen in a 4-view run); far placements would change which NPCs the host's game runs.
* Pitfall: right after a guest's join in Tribe its world is black for some seconds (the name tags are drawn); wait for
  the picture (coop_same_view.py waits for a mean brightness over 25).

## The camera a frame is drawn with: one step behind the viewer at Present (2026-11-12, fix/w48-labels)
<a id="graphics-drawn-camera"></a>

BUG-0948. GOG 2024, lane 4, Creature (the guest's follow camera turned by SporeKit's right-drags of 400 px).
* ✔ (runtime, `kit.cam` probes) Every D3D view matrix the game sets while it draws a frame
  (`IDirect3DDevice9::SetTransform(D3DTS_VIEW)`, 20 to 90 calls a frame, the last ones the UI's) has the forward axis
  (column 3) of the viewer as it was at the previous Present, and of the viewer at the moment of the call: 104 of 104
  and 110 of 110 frames inside turns. The viewer at the frame's first BeginScene and at its Present is already the
  next camera step (equal to each other in every frame; 23 to 837 px apart from the drawn camera at a point 10 m
  ahead while the camera turns, 0 at rest). So the picture of frame N is drawn with the camera of Present N-1, and an
  overlay pass that reads the viewer at Present N projects one camera step ahead of the picture.
* ? Where exactly the camera is written between the frame's draw calls and its first BeginScene was not looked for
  (the draw's SetTransform calls come before the first BeginScene SporeKit sees after a Present).
* Fix (SporeKit, `kit.fix drawncam`): the viewer (its 0x40 bytes, the camera's view window and viewport) is copied at
  the game's first SetTransform(VIEW) after a Present, and `world_to_screen` inside the overlay pass projects with the
  copy (outside the overlay pass it reads the viewer as before). `kit.cam [reset]`: copies, overlay passes, how many
  passes the live viewer differed from the copy and by how many px (a point 10 m ahead, 3 m right, 2 m up of the
  copy; the orbit camera looks at the avatar, so a point straight ahead stays in the middle).
* CORRECTION (2026-11-12, fix/w48-labels) to BUG-0948's "keep the viewer of the moment the game draws (a copy taken in
  the game's draw ...)": a copy at the first BeginScene after Present is the same as the viewer at Present (no help);
  the copy has to be taken at the draw's first SetTransform(VIEW).

## The health bars over creatures: a world layer (2026-11-16, feat/lineages-47)
<a id="graphics-world-bars-layer"></a>

* ✔ (Creature, GOG 2024 / Steam, the MSI at 125 %) the health bar over a creature (the player's own when hurt or
  starving, a selected one's) is a holder 0x93310E4A placed at the creature's screen point (in real screen pixels) with
  the bar 0x026E0DF0 ('health'), 0x026E0DF2 ('hurt'), 0x026E0DF3 ('none') and the number 0x04D039A8 (its caption the
  health) under it, and a hidden 0x933103EB. They lie in a layer of their own, 0xE9F70DF9 (the main window's child), made
  when the first bar shows. Scaled with the HUD (3X A) they were drawn at f x their place: that layer is not scaled
  (BUG-9238), like the hints' 0xCBDF6E1D.
* ✔ sui::screen_rect() of a window in a scaled layer is its layout place (the 1/f screen); where it shows is its drawn
  transform (render_xform, Window +0x148, type 1). A click posted at the drawn point is mapped back by ns_remap like
  the player's, so a click aimed by screen_rect missed (the game saved box, BUG-9238).
* ? Not looked at yet: the bars over Tribe members, Civ units and vehicles, Cell's health ring (cCellUI rollovers):
  find their layer with `coop.ui at <x> <y>` on the bar, then `coop.ui path <id>`.
* ✔ (2026-11-16) Cell and Civ have the same two world layers (0xCBDF6E1D hints, 0xE9F70DF9 bars) at the whole
  screen; the Civ city rollover (owner, population, spice) is placed correctly at 125 %.
* ✔ (2026-11-16, BUG-9241) Spore's tooltip is one window, 0x03754E6C (StdDrawable, kept hidden between uses with its
  last text), a child of the HUD layer 0x05B598F6 (the cheat console 0x014A4606 is there too). The game puts it at the
  pointer's REAL screen place (centred, about 20 px under it), not the remapped mouse place, so in a scaled layer it
  showed at f x the pointer. Fixed in its own transform (ui.tooltip_place); the layer itself stays scaled.
* ~ (2026-11-16) A `move` posted by qa.py over a Creature ability button showed no tooltip (Civ's city buttons did).
* ✔ (2026-11-16, 125 % on 1280 x 960) Tribe: the chieftain hut's health bar sits on the hut. Space (solar system view):
  the planet name box and crosshair sit on the planet, the home marker on the home planet, and the tooltip window
  (here "Main Tools", 0x03754E6C) shows just over the pointer with ui.tooltip_place on. With these, every stage's
  overlays were looked at once (Cell, Creature, Tribe, Civ, Space).

## The overlay pass and the scene's depth buffer (2026-11-21, feat/l47-rings)
<a id="graphics-overlay-depth"></a>

BUG-9321 (the party rings drawn into the world: a unit in front hides that part of its ring). GOG 2024 / Steam on the
Windows QA copy, 1024 x 768, read with `coop.rings depth` (partner_marker::device_report) inside SK_EV_OVERLAY.

* ✔ SporeKit's overlay pass runs in Present: it binds the back buffer as render target 0 and calls BeginScene; it does
  not touch the depth-stencil binding, the transforms or the depth states.
* ✔ Creature (Samuth): at the overlay pass the game's depth-stencil surface is still bound (D3DFMT_D24S8, the back
  buffer's size, no multisampling), and D3DTS_VIEW / D3DTS_PROJECTION are the scene's camera: a perspective projection
  with _11 = 1.83, _22 = 2.44 (tan of the half field of view 0.4097, the viewer's +0x60), _33 = 1.0001, _43 = -0.5 or
  -1.0 (the near plane changes with the camera's zoom; far = 10000 x near), _34 = 1. ZENABLE was 0 and ZWRITEENABLE 1
  (the last pass's states). Triangles drawn with ZENABLE on, ZWRITEENABLE off and LESSEQUAL against that surface are
  hidden behind creatures, legs and tails exactly (screenshots out/qa-shots/creature_*_depth_*).
* ✔ Cell (Diplomavil): in about every other frame the last pass before Present is a 2D one: NO depth-stencil is bound and
  the transforms are an orthographic view (translation -0.5 0.5 2) and projection (2, 2, 0.5, -0.5); in the other
  frames the scene's depth-stencil (the same surface) and perspective transforms are set (camera 20 m over the plane,
  _43 = -2.0). Drawing flat in the first kind of frame and depth-tested in the second made the ring flicker over and
  under the cell (owner report 2026-11-21). The ring code keeps the last scene state seen: the surface (one reference,
  released on SK_EV_DEVICE_LOST and at the unload, else the game's Reset fails) and the view / projection, binds the kept
  surface for its draw when none is bound and puts the binding back after (a D3DSBT_ALL state block does not restore
  it). Over 216 Cell frames 35 used the kept surface; its content was still the scene's depth (no flicker in six frames
  in a row, out/qa-shots/cell_guest_ring_under_host_cell_6_frames_no_flicker.png).
* ✔ The vertices: x, y from world_to_screen (the camera the frame was drawn with, #graphics-drawn-camera), z = _33 +
  _43 / zv and rhw = 1 / zv with zv the view-space depth from the kept view (XYZRHW). Using the device's view and
  projection directly (D3DFVF_XYZ) also worked in Creature, but in frames with a stale or 2D transform it cannot.
* ✔ A ring on the ground with a unit standing on it: 0.12 m + 0.06 x its radius over the ground is enough (no
  z-fighting with the ground seen in Creature at 10 FPS). A unit with no ground under it (a cell lies ON the Cell plane,
  a ship) must get its ring below it (0.6 x the ring's radius away along up): a ring 0.12 m over the Cell plane cut
  through the cell's body and flickered.
* ~ Not checked: the Space stage's depth at the overlay pass (no live Space run), Civ and Tribe with depth on (their
  screenshots were taken before the depth path; the projection path is the same).
* Negative: the overlay pass is after the game's UI, so a ring partly under a HUD window is still drawn over that
  window (the depth buffer holds no UI); the ring code leaves out a ring whose centre is on a HUD box (BUG-9319) and
  hides all rings while the Spore Guide is open.

## The depth clear before the game's UI, and Space and Cell depth at the overlay (2026-11-21, fix/l47-final-rings)
<a id="graphics-ui-depth-clear"></a>

BUG-9327, BUG-9324, BUG-9325 (the party rings, BUG-9321). TITAN QA copy (Steam code), 1024 x 768, read with
`coop.rings trace` (hooks on the device's Clear, SetRenderTarget, SetDepthStencilSurface, DrawIndexedPrimitive) and
`coop.rings ztest`.
* CORRECTION (2026-11-21, fix/l47-final-rings): "Civ and Tribe with depth on ... the projection path is the same"
  (#graphics-overlay-depth) does not hold: in Civ and Tribe the depth buffer is EMPTY at the overlay pass. Evidence: a
  ring under a Civ vehicle (and under a tree's canopy) was drawn over it with LESSEQUAL; with GREATER it was not drawn at
  all; computing its depth with a near plane of 0.5 to 0.01 (instead of the projection's 1.0) changed nothing.
* ✔ Civ frame (trace, one frame): the world is drawn into an offscreen 1024 x 768 render target with the screen-size
  depth-stencil (the same surface the overlay sees bound, D24S8), post effects run on 1024 x 768 / 512 x 384 / 512 x 512
  targets with no depth-stencil, small 32 x 32 and 128 x 96 passes (icons), then ONE draw puts the world on the back
  buffer, then `Clear(Z | STENCIL)` of the whole screen-size depth-stencil (flags 6, no colour) with the back buffer as
  render target, then about 60 draws (the game's UI), then Present. That clear is why the overlay has no scene depth.
* ✔ The fix: the rings are drawn inside that Clear call, just before it runs (partner_marker early_draw: count 0, Z in
  the flags, no TARGET, render target 0 = the swap chain's back buffer, the depth-stencil and the viewport the back
  buffer's size). The scene's depth is still there and the game's UI comes after (a HUD window or the city planner now
  covers a ring by itself). The ring list is the previous overlay pass's (positions one frame old). Seen working in Civ,
  Tribe and Space (on a planet: `before the UI: frames=` grows every frame); Cell has no such clear (frames=0) and keeps
  the overlay path with the kept scene depth (#graphics-overlay-depth).
* ✔ Space at the overlay pass (system view and on a planet): the scene depth-stencil and the perspective transforms are
  bound (near 1, far 10000, camera about 1170 m away in the system view); the ship in front hid its ring.
* ✔ Space cinematics (the death screen "Your ... has taken so much damage", the "We've built you a new ship" caption):
  the scene is drawn blurred through another pass and the depth no longer matches: the ring was drawn sharp over its
  ship on both screens. `world::cinematic_playing()` is true for both (BUG-9324: no ring then).
* ✔ A ship's hull reaches lower than 0.6 x the ring's radius under its centre (close up on a planet the ring went round
  the hull's lower part); 1.0 x the radius (the ring is 1.1 x the bounding radius) keeps it under the whole hull, in the
  hull's own metres also when min_px grows the ring far away (BUG-9325).
* ✔ A device reset is not reachable on the windowed QA copy: minimize / restore (`kit.test.window`), `window.borderless`
  on / off and resizing the window (the game stretches its 1024 x 768 back buffer) gave no SK_EV_DEVICE_LOST, and the
  game's own screen size setting says it needs a restart. The kept depth surface stayed valid through all of them.
* ? The game's own selection marker (the soft glow under a selected Civ vehicle, the owner's lead) was not analysed: the
  draw-before-the-clear path was enough. It is drawn in the world pass (it is hidden by the vehicle), so a game-side
  marker renderer would be another way.
* CORRECTION (2026-11-21, fix/l47-final-rings, later the same day): "✔ Space at the overlay pass ... the ship in front
  hid its ring" above is not proven: the owner pointed out that the system-view pair (old overlay path) shows the ring
  over the hull in both halves, and on a planet Space has the depth clear before its UI (rings drawn there: `before the
  UI: frames=` grew), so the overlay's planet shots were compared at different ship positions. What holds: on a planet
  with the draw before the clear the ship covers its ring (space_final_build_off_then_4_on.png). Seen but not yet
  checked by picture: in the system view the same counter also grew (98 frames with the rings drawn at the clear while
  both ships were in the system), so the system view probably has the same clear. To check: `coop.rings trace 2` in the
  system view and the galaxy view.
* BUG-9325 fallback (space.ring_cut): a Space frame whose rings are drawn at the overlay (no such clear that frame)
  cuts geometrically: each ring vertex whose line to the camera passes through the ship's sphere (0.75 x the ring's
  radius round its centre; the camera from the view matrix, -t R^T) is made clear. `coop.rings` cut_vertices=.
