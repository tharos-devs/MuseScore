# MuseScore Custom Build — Changelog

Custom features and notable fixes built on top of upstream MuseScore, since this branch diverged from `main` around 2026-08-25.

## Features

### 2026-09-22
- Added a "Condensed view" toggle to the Mixer's View menu, narrowing every regular channel column to trade per-row precision for screen space (Master keeps its normal width and full detail).

### 2026-09-20
- Added full undo/redo support to the Mixer panel, covering channel color, aux-bus routing, drag-reorder, channel add/delete, and volume/pan/gain/aux-send-level changes.
- Mute, Solo, and Aux-send bus picks now apply to an entire multi-selected group of Mixer channels at once, not just the one clicked.
- Instrument tracks' context menu gained "Add FX channel"/"Add Group channel" actions, including variants that route every selected track to the new bus in one step.
- Added drag-and-drop reordering of the Mixer's FX buses and Group buses among their own kind.
- Reordered the Mixer's channel layout so Aux/Group buses sit next to the instrument tracks they route from, with Video and Metronome pinned beside Master.
- The Mixer's Master channel strip now stays pinned in view while scrolling the channel list horizontally.
- The Mixer's row-label column (Sound/Gain/Audio FX/etc.) now stays pinned while scrolling horizontally instead of scrolling away.
- Added Video panel menu toggles to independently hide the Timeline and the Controls toolbar.

### 2026-09-19
- Added global Mute/Solo toggle buttons to the Mixer header that mute/solo (and later restore) every relevant channel in one click.
- Added a Video toggle button to the main toolbar, next to Mixer.

### 2026-09-09
- Added a per-track Gain knob (-24..+24 dB, applied pre-FX) to the Mixer.
- Added Group (subgroup) buses alongside the existing Aux send/return buses, routing a track's output exclusively through the bus instead of as a parallel send.

### 2026-09-08
- Aux and Group bus channels can now be renamed inline (double-click or context menu), with the new name propagating to every send slot that targets them.
- Redesigned Aux sends: up to 5 independently-routable send slots per track and up to 20 Aux/Group buses (up from a fixed 2), each freely assignable via dropdown.

### 2026-09-07
- Added drag-and-drop reordering of Audio FX slots within a single Mixer channel strip.

### 2026-09-06
- Added a "Full screen" toggle to the floating Mixer panel, matching the Video panel's existing one.
- Raised the Mixer's Audio FX slot limit from 4 to 5, with instant progressive reveal of the next empty slot.
- Replaced the toolbar's Automation button with a split button whose dropdown jumps straight to a specific automation type (Dynamics/Tempo/Volume/Pan).
- Added a live drag-value tooltip to the Dynamics/Tempo/Volume/Pan automation curves.

### 2026-08-30
- Added a video player synced with score playback: dockable Video panel, adjustable offset, timecoded hit points, and a dedicated Mixer Video channel.
- Mixer channels (instrument tracks and Aux buses) can be assigned a custom color, shown across the strip's title, badges, and controls.

### 2026-08-25
- Added a click-to-swap popup for basic articulations (Staccato, Staccatissimo, Accent, Marcato, Tenuto), matching the existing Dynamics popup's interaction pattern.
- Added adjustable per-note playback start/duration offsets, editable via on-canvas drag handles.
- Added a per-note velocity drag-handle overlay for editing note loudness directly on the score.

## Fixes contributed back to stock MuseScore

- Fixed a popup-positioning bug, shared by the Dynamics and Articulation popups, where swapping to a differently-sized glyph made the popup jump or render half-hidden under it (2026-09-20).
- Fixed a dock-panel bug where a panel (e.g. the Mixer) could reopen snapped to the wrong screen position after a sibling panel sharing its dock group had been dragged elsewhere (2026-09-19).
- Fixed a dock-framework bug (a destructive recursive equal-width layout pass) that could silently reset any panel's saved width on relaunch — surfaced through the Video panel, but the root cause applies to any dock panel (2026-09-19).
- Fixed note playback velocity being computed incorrectly for notes using a percentage-offset velocity (rather than an absolute value), causing a modest offset to be misread as a much smaller absolute velocity (2026-09-09).
- Fixed Audio FX bypass/enabled state not being saved to the project file, silently resetting every FX slot to enabled on reopen (2026-09-07).
- Fixed a crash and spurious duplicate notes when tying grace notes together — MuseScore#34803 (issue #34751) (2026-09-06).
- Fixed Explode dropping the slur between a grace note and its main note when the note moved to a new staff — MuseScore#34810 (issue #34809) (2026-09-06).
- Fixed Automation view curves becoming misaligned with the score after switching between Page and Continuous view — MuseScore#34801 (issue #34776) (2026-09-06).
- Fixed the Master channel's VST3 FX editor not opening on double-click in the Mixer — MuseScore#34763 (issue #33872) (2026-08-31).
- Fixed the Mixer's Master channel Mute button having no actual audio effect — MuseScore#34747 (issue #34746) (2026-08-30).
- Fixed Mixer manual volume/pan resetting to 0 dB, and the Solo button reverting, after toggling Solo and playing back — MuseScore#34676 (issue #34673) (2026-08-25).

## Fixes (custom-only)

- Fixed muting a Mixer Group bus not silencing its member tracks' sends to an unrelated, shared Aux FX bus (e.g. a reverb), which kept leaking their audio through that bus instead of going fully silent (2026-09-21).
