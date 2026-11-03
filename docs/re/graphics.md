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
