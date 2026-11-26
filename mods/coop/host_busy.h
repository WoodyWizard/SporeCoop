// C3 of 0.7.15 (BUG-1142): what the stage modules ask of the host-busy model (host_busy.cpp, busy_model.h)
#pragma once

namespace host_busy {
// this guest's game waits for the host's pause now (one pause of ours on the game's time, or the Cell step held)
bool waiting();
// Tribe: the host's pause reaches this guest as MSG_HOST_BUSY (a 0.7.15 host, `core.host_busy` and
// `tribe.busy_model` on): Tribe's own kind 21 wait stands down (a 0.7.14 host: kind 21 as before)
bool takes_tribe_pause();
// BUG-1461: the host's world stands now (its last MSG_HOST_BUSY: paused, an editor or away, of this guest's stage and
// world): whether or not this guest's own game waits for it
bool host_stands();
// BUG-1577: current frame's classified local UI pause bits (busy_model::kWhyPauseMenu / kWhyModal / kWhySporepedia),
// independent of core.host_busy so automatic world-share saves wait while the pause menu or an exit dialog is open.
uint16_t local_ui_pause_why();
} // namespace host_busy
