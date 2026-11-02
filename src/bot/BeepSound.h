#pragma once

// beep() of Lua / JavaScript strategies: the same bell-like tone as the reference bot's
// beep() (index.js, Web Audio): a 2642 Hz sine with a 6869 Hz overtone at 8 %, a 1 ms attack
// and an exponential decay (time constant 0.30 s) over 5.5 s.
namespace BeepSound
{
// Plays the tone without blocking (a new call restarts it). Windows: PlaySound from memory;
// other systems fall back to QApplication::beep().
void play();
} // namespace BeepSound
