// Forwarding of every winmm.dll export to the real one in System32 (list in winmm_exports.inc / winmm.def).
#pragma once

namespace proxy {

// Loads System32\winmm.dll and fills the forwarding slots. Returns the number of exports the real DLL lacks
// (those stay null and crash if called), or -1 if it did not load.
int load_real_winmm();

// Number of forwarded exports.
int export_count();

} // namespace proxy
