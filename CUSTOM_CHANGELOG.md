# MuseScore Custom Build — Changelog

Custom features and notable fixes built on top of upstream MuseScore, since this branch diverged from `main` around 2026-08-25.

## Features

### 2026-10-10
- Mixer: channel EQ, 4 bands applied right after the gain, before the Audio FX, on every track, aux/group bus, the master and the video channel. An "EQ" row (View menu) shows each channel's curve; a click opens its editor above the channel (one at a time): drag a band's point (frequency, gain; wheel: Q), set each band's on/off and type, and its gain, frequency and Q with drag bars (Shift: fine, wheel, double click to type); hover shows the frequency, note and level. Right click on the thumbnail: enable/disable, reset. Defaults: 100 Hz Low Shelf II, 800 Hz and 2 kHz Parametric II, 12 kHz High Shelf II. Types: Parametric I/II, Low/High Shelf I-IV, High/Low Pass I/II, their curves matched to measured reference curves. Moving a band while playing doesn't click; undoable; saved with the project and track presets.
- MIDI input: "All devices" in Preferences > I/O > MIDI input listens to every MIDI input device at once, e.g. a keyboard for note input and a control surface (nanoKONTROL…) for MIDI CC recording, including devices plugged in later (immediately on Mac, within 5 s on Windows/Linux); a device that can't be opened (used by another application) doesn't prevent listening to the others. Never chosen automatically. Also fixed, on device changes: a Windows device's SysEx buffer put back while closing it (handle leak, possible crash) and Linux closing the ALSA sequencer before stopping the thread reading it.

### 2026-10-09
- Note velocity: Alt/Option+drag on a staff paints velocities - every bar the mouse goes over takes the velocity of the mouse's height (all the notes of a chord), along the mouse's whole path so a fast move skips none, on the line the gesture started on, in one undo step; the bar under the mouse shows its value and the changed notes are heard (throttled). Started anywhere on the staff, on a bar or not.
- Note velocity: Cmd/Ctrl+click on a velocity bar opens a field to type the note's velocity (1-127), like on automation points; with the note part of a selection, every selected note gets that value. Return or a click elsewhere applies it, Escape cancels. The field (automation points too) now stays on screen and applies a value typed back to the shown one.
- Track presets: "Save as track preset…" (Mixer channel menu, Track list right click) saves a snapshot of a track: its instrument (VST3 with its loaded patch, MuseSounds or SoundFont sound), gain, volume, pan, effects, sends to the aux/group buses (by name: sends to a bus the project doesn't have are left out), color, MIDI port/channel and articulation map - never the score's instrument. Name unique per plugin, free tags; the instrument, its family and the plugin are tags of their own.
- "Load track preset…" opens the Track presets window (one, non-modal, always on top by default): the score's instruments on the left, and three tabs in three columns narrowing down: Track presets (Family/Instrument/Plugin columns in the "Group by" order), MuseSounds (vendor, category, sound: every installed sound, third-party ones included, no preset needed) and SoundFonts (SoundFont, MS Basic's categories or the bank, preset). Search, Load or double click to apply to the selected instruments (MuseSounds and SoundFonts change only the sound), a preset's plugin/name/tags shown and edited below (Update, Delete), presets of a missing plugin dimmed. Tab, "Group by" and selections remembered. The presets' infos are indexed: opening never reads the plugins' states.
- The Mixer follows gain, volume and pan changed elsewhere (e.g. a track preset).
- Articulation map editor: "Score markings" in the side panel chooses which notation symbols and playing technique texts select an articulation in the lane (e.g. Staccato for a "Staccatissimo" or "Spiccato" articulation), in a searchable list grouped like ARTICULATION_MAP_REFERENCE.md, each with its symbol; pre-checked from the name as before, written in the file only when different, set on all the selected articulations. A new "Score markings" column shows them for every articulation, dimmed where an articulation higher in the map takes the marking; Copy keeps them.
- Articulation map editor: "Colors" recolors the selected articulations (or all of them) with one of 5 color gradients; Default and Disable side by side, "+ Add" next to "Activation sequence" and a taller window, to see several activation sequence lines; a narrower Name column.
- Articulation map editor: several rows can be selected (Cmd/Ctrl+click, Shift+click, Cmd/Ctrl+A) to remove (also with Delete), move by dragging, or copy them at once; with several articulations selected, Disable, Articulation delay, Note delay, MIDI channel and the color (its square in the list) are set on all of them.
- Track list: several rows can be selected like in the Mixer (click, Cmd/Ctrl+click, Shift+click); the color, Mute and Solo of a selected row apply to the whole selection (one undo step for the color).
- Fixed: no panel could be docked at the bottom of the main window anymore (the drop zone showed at the right instead): a saved layout could keep the hidden drop zones at the wrong edges. They're now put back at their own edge each time a page loads, which also repairs such a layout.
- Video panel: "Clear video" in the "…" menu, like the sidebar's button (disabled without a video).

### 2026-10-08
- Articulation lane: a click goes to the note whose span the mouse is in (from that note to the next one), not to the nearest note. An articulation spanning several notes is edited by clicking anywhere over its own note's span; over a later note it covers, it's cut just before that note, which shows as a free slot where a click adds an articulation. An articulation only lights up when a click edits it, and the lane's line no longer crosses its text then.

### 2026-10-06
- Track list: the articulation map button shows for every VST3 instrument, dimmed without a map (its right click menu still offers New…/Load…). The articulation map editor only opens once per track (clicked again, the open one comes to the front), and the articulation map and instrument window buttons are colored while their window is open.
- Track list: a "Video" line first, below the top bar: color (Edit color / Reset color, also in the Mixer's Video channel menu now), level meter, Mute, Solo (the video's, shared with the Video panel, the Mixer and the Timeline) and a button to choose the video file; "No video" without one.
- Articulation map editor: a button right of "Track delay" opens the track's VST3 instrument window (colored while it's open), and clicking an articulation sends its keyswitches/CCs (as edited, even unsaved) to the instrument, to hear it - while playback is stopped.
- A VST3 plugin's window now closes when the plugin is removed, wherever it was opened from (Mixer, Track list, articulation map editor); replaced by another plugin (another instrument for the track, another effect in the slot), the new one's window opens instead.
- Fixed: the application crashed on quit (since 2026-10-04): objects of the audio engine destroyed after its thread had stopped stayed subscribed to its channels.
- Panels' "…" menu: a "Zoom" submenu (Zoom +, Zoom -, Reset zoom) in the Mixer, Track list, Video, Palettes, Layout and Properties (which gets a "…" menu), from 50% to 200%, remembered per panel; the Track list is one step smaller by default (90%). In the Timeline, it changes the measures' width (it was only possible with the mouse).
- Track list and Video panels at a side: resizable, unless tabbed or stacked with Palettes, Layout, Properties, Selection filter or History (then 300 px like them). The Track list's articulation map / instrument window columns only take room when a track uses them.
- Fixed: reopening a project with the Video panel at a side could widen its column for good. Menus and popups opened from a zoomed panel open at their button.
- Fixed: the Track list's button to open a VST3 instrument's window never showed; it's now after the articulation map's button, both in their own columns.
- Opening a project now shows a small window (titled with the file name) saying "Loading…" and, for VST3 instruments and effects, the plugin being loaded ("3/12 <plugin name>", out of the project's VST3 plugins). It closes by itself once the project is really ready. VST3 plugins are now loaded and restored one after the other with short pauses in between, so the application no longer freezes in one long block (a project with 8 big sample players went from a 12 s freeze to pauses of 2 s at most), and the window keeps showing progress.
- Fixed: a VST3 plugin could, in rare timing, store its default state in the project instead of the saved one while the project was opening.
- New "Track list" panel (View > Track list): one line per instrument with its color (click: Edit color / Reset color), a small level meter, Mute, Solo, show/hide, the Mixer's Sound menu, the VST3 instrument's window and its articulation map (click: edit; right click: the Mixer's Articulation map menu). A bar on top holds global Mute/Solo, like the Mixer's. It docks with the side panels (or in the secondary window), and its "…" menu has Zoom in/out/Reset zoom. For an instrument that changes in the score (e.g. flute to piccolo), Mute, Solo and color apply to all of its instruments.
- Fixed: undoing a Mixer color change did nothing once the Mixer had been closed in between.
- Fixed: changing a sound in one place could put back an aux send level changed meanwhile in the Mixer; the Mixer now also follows aux sends changed elsewhere.
- Audio exports now sound as heard: they go through the Master channel (its Gain, effects, fader and mute), and Volume/Pan automation curves (tracks and Master) are followed — they were frozen at their value at the start (stock bug). A Master fader at -6 dB now gives a 6 dB quieter export.
- "MP4 video (attached video)" export: an "Audio format" list — AAC or MP3 (lossy, with the bit rate) and ALAC or FLAC (lossless, 16 or 24 bits, 16 bits dithered), all in the .mp4, kept in sync to the sample. MP3 is exported at 160 kbit/s or more, and Apple's apps (QuickTime…) don't play an MP3 track in an MP4 (VLC and Windows do).
- Fixed: the MP3, WAV, FLAC and OGG audio writers ignored the export's options, so the attached video's audio could be missing from an export using them.
- Video panel: the picture follows the output latency measured by the audio driver (its buffers and the device's own latency), instead of an estimate of one buffer, on macOS.

### 2026-10-04
- Fixed (Windows): a score could not be saved, neither a new one nor an existing one: after choosing "On your computer" in the save location dialog, the file dialog never appeared (upstream bug since its file dialogs became asynchronous). File > Open had the same flaw.
- Fixed: an articulation map saved in the editor (e.g. an articulation disabled) kept playing its former version until reloaded into the track. Saving now reloads it into every track of the score that uses that file.
- Articulation maps: playing technique texts from the palette ("pizz.", "sul pont.", "col legno", "mute"…) now select the articulation named after them (`Pizzicato`, `SulPonticello`, `ColLegno`, `Mute`…) until the next one ("arco", "normal"), also for notes played on the MIDI keyboard. `ARTICULATION_MAP_REFERENCE.md` lists every score sign and playing technique the articulation lane recognizes, with the exact name to give the articulation.
- Palettes, Properties, Layout and the other side panels are back at a fixed width (300px): since the Video panel could join them, their column could widen by itself. The Video panel fits that column (narrower buttons, sidebar always below the timeline at the side); tabbed with them it takes their width, anywhere else it stays resizable.
- Keyswitch converter (`tools/keyswitch-converter/keyswitch-converter.html`, outside MuseScore): converts Studio One sound variations (.keyswitch) into articulation maps, a few files or whole folders at once. A single page opened in any browser on Mac or Windows; nothing is installed nor sent anywhere.
- Toolbar: the Automation dropdown shows an icon for each type, and the button shows the current one ("♩=" for tempo). A new Expression button replaces the Note offset, Note velocities and Articulations buttons: its dropdown shows or hides each of these editors (they can be combined), and the last one shown is the one the button toggles.
- Shortcuts: Shift+R toggles Record MIDI CC; new "Automation: Dynamics/Tempo/Volume/Pan" actions (show that curve, or turn automation off when it's the one shown) to assign your own shortcuts to.
- New "Caret input" note input mode (key C outside note input): the toolbar durations only choose the duration and A–G enter notes at the caret. The mouse places notes on a rhythmic grid, anywhere in the measure (also inside a longer note or rest, which is cut there), and Left/Right move the caret along that grid. The grid (32nd to whole note, dotted values included) is picked from the mode's toolbar button, which shows it; the note input ruler shows one line per grid step, long ones on the beats.

### 2026-10-03
- The Video panel can be docked below Palettes, Layout and Properties (or tabbed with them) in the side column, and comes back there when reopened; that column can now be made wider than 300 px. Its width is kept on relaunch (it widened back to 640 px), and the transport buttons no longer overlap the "Recently opened videos" arrow when the panel is narrow.
- Fixed: dragging a panel over another one showed the wrong drop zones when that panel wasn't at its default place (side by side instead of above/below), or lower in its column (no "above" zone) - stock bug.
- "Delete selected points" for MIDI CC curves moved from the toolbar's Automation dropdown to the staff menu's "MIDI CC" submenu (first item, also in automation mode).
- Timeline meta rows: reordered by dragging their label (a blue line shows where), order remembered like their visibility; Measures always first, right below Video, with a collapse/expand arrow; measure separators across all meta rows; clicking a meta row moves the playback position to that measure; tempo marks show their note like in the score and aren't cut needlessly.
- Fixed: collapsing/expanding the Timeline's meta rows (or showing/hiding one) left the instrument rows in place, under a dark empty area (stock bug); the wheel over the Measures row zooms again (and around the mouse).
- Timeline Video row: the attached video's pictures along the score, at the top of the Timeline (View menu: "Video"). Decoded in the background, only those in view, and kept in memory; "Loading…" until the first ones show, "No video" without a video. Its height is set by dragging its bottom edge (1 to 6 rows, remembered), and its header has Mute, Solo (the video's sound, like the Mixer) and a button to choose the video file.
- The Timeline zooms in much further (up to 400 px per measure, was 50), for precise video pictures; Ctrl+wheel zooms like the wheel over the Measures row (doubling every 4 notches, around the mouse), instead of 1 px a notch.
- Timeline zoom from the Measures row: click and hold on it, then drag up to zoom in or down to zoom out; the mouse wheel over that row zooms the same way. The clicked (or hovered) point stays in place. A plain click still moves the playback position.
- VST3 instruments can now receive their MIDI on any channel (and port, when the plugin has several event inputs, e.g. not Kontakt): the current sound's line in the Mixer's Sound menu (e.g. "Kontakt 8") opens a submenu to pick it, saved with the project. Everything the track sends follows it: notes, keyswitches, MIDI CC curves, sustain, pitch bend and the MIDI keyboard.
- An articulation map can put each articulation on its own MIDI channel (`ch=2`, or a channel alone: `ch=3 Pizzicato`), e.g. one instrument per channel in a Kontakt multi; the map editor has a "MIDI channel" field. Notes and keyswitches play on their articulation's channel, and MIDI CC curves, sustain and pitch bend go to the channel of the articulation playing at that time (the new channel gets the curves' current values on each change; pedals are released where they were pressed).
- Notes and controllers played on the MIDI keyboard (or while entering notes) follow the articulation at the input position: the clicked note's own one, else the one in effect at a clicked rest or measure; while playing, the one at the playback position.
- VST3 plugins keep running for a second after Stop before being deactivated: reverb and release tails end naturally instead of being cut, and the plugin's own displays (e.g. Kontakt's MIDI activity light) no longer freeze lit.
- Saving now reads the current state of every VST3 plugin (instruments and effects), and opening a plugin's window marks the project as modified: changes a plugin doesn't report (e.g. Kontakt's solo/mute) are no longer lost.

### 2026-10-02
- The articulation lane now also stops on rests: an articulation set on a rest applies from the next note on (also on a staff with rests only). Its menu starts with the articulation last placed on the track (with its folders, e.g. "Long > Con vibrato") and ends with "Edit articulation map…".
- Articulation lane chips keep their size when hovered: their full name shows in a tooltip while Cmd is held, a click always goes to the note under the target marker (even over a chip), and the cursor stays the default arrow (a double arrow only while dragging a chip). The hovered chip's text stays readable with light colors.
- In the articulation map editor, "Copy" duplicates the selected articulation right below it (name and triggers), "New folder" always adds the folder at the end of the map, outside of any folder, and dropping an item below the last row moves it there (that drop didn't move anything before). The "Note On + Off" type is now labelled "Note On/Off".
- Cmd+click on a Tempo, Volume, Pan or MIDI CC automation point opens a small field to type its value in its own unit (BPM, dB, balance, MIDI value), kept within bounds: Enter or a click elsewhere applies it, Escape cancels. Automation curves show the default arrow cursor (no cross).
- Dynamics automation values now read as dynamics ("f", "p +50%") instead of a percentage, and dragging or adding a point draws it to the dynamics' own levels (Alt: free placement).
- The staff menu's "Automation type" submenu (automation mode) starts with "Delete selected points": erases the points of the curve shown within the range selection, keeping those the score drives.
- While playing, VST3 instruments now hear the notes and all MIDI controllers played on the MIDI keyboard (MuseScore ignored them during playback), and pressing a MIDI key never starts note input. All controllers are passed on, not only modulation and sustain.
- A MIDI input activity light in the status bar, next to the zoom: it flashes green on every MIDI message received.
- Clicking a measure number in the Timeline's Measures row now moves the playback position to that measure (it selects the measure's first element, all visible instruments), like a plain click on an instrument cell; it used to only scroll the score there.

### 2026-10-01
- Synced with upstream `main` (120 commits, incl. the new "Convert file to score" import, offsets applied after autoplace with "Freeze placement", MusicXML/TablEdit import fixes). The build now uses the docking engine upstream defaults to (KDDockWidgets v2).
- The floating Mixer's "Full screen" menu item now shows a checkmark while the Mixer is full screen.
- A track's "Articulation map" menu (Mixer and VST3 staff) now has "New…": it opens the editor on an empty map bound to the track, attached on "Reload into the track".
- In the articulation map editor, a new articulation's keyswitch continues from the previous articulation's (one semitone up).
- Added MIDI CC recording: a "Record MIDI CC" button in the playback toolbar (red when armed) records, during playback, the MIDI CCs received from the MIDI input device into the selected VST3 staff's MIDI CC curves (each CC into its own curve, heard live). "Touch" mode: the existing curve is only replaced where the controller moved (seeks and loops included), written on Stop as one undoable step, simplified, and drawn live while recording.
- The MIDI CC menus now start with "Delete selected points": erases the shown MIDI CC curve's points within the range selection, as one undoable step.
- Added a secondary window (View > Secondary window) to build a DAW-like second workspace, e.g. full screen on another monitor: the Timeline, Mixer, Video and Piano keyboard panels can be docked into it (along its edges, next to each other or as tabs, with a preview of the drop), other panels like the palettes stay out of it. It's shown with the score (hidden on the Home page), has its own keyboard shortcuts, and is restored with its panels at startup; the layouts it's saved in stay readable by builds without it.
- The Mixer's "…" menu now has "Zoom in", "Zoom out" and "Reset zoom": scales the channel strips from 50% to 200% in 10% steps, kept across relaunches.
- The Timeline's "…" menu now has a View submenu to show or hide each meta row (Tempo, Time signature, Timecode, Hit points, Rehearsal mark, Key signature, Barlines, Jumps and markers, Measures), with a checkmark, kept across relaunches; the right-click menu on the row labels uses the same setting.
- The Timeline now shows a playback cursor line: it follows playback like the score's cursor (repeats, jumps and loops included), stays at the start position while stopped, and the Timeline scrolls page by page to keep it visible. A plain click on a cell moves the playback position there (it selects the measure's first element); Shift+click selects measures.
- The Timeline's instrument rows now start with a strip in the track's Mixer color (click it to change the color), cells with content take that color, and each row has always-visible Mute, Solo and show-in-score (eye) buttons, in sync with the Mixer.
- The Timeline now has one row per instrument (a piano, harp… gets a single row, colored when any of its staves has content), named like the Layout panel (e.g. "Horn in F 1"); with stave sharing on, only the combined part's row is shown ("Horn in F 1-2"), its buttons and color acting on the parts it combines. All measure numbers are shown, left-justified, at any zoom.

### 2026-09-30
- Added an "MP4 video (attached video)" export (the existing one is now "MP4 video (score)"): the attached video over the score's timeline, placed by its offset, with the score's audio and the video's own audio as mixed in the Mixer, sample-accurate. The picture is copied as is (no loss of quality) — only re-encoded (same size, frame rate and bit rate) when the video starts after the score, to add real black frames before it. Uses the FFmpeg bundled with Qt, no setup needed.
- The attached video's audio is now a real track of the audio engine, played on the same clock as the score (sample-accurate, identical on every playback, loops and repeats included): the Mixer's Video channel gets a real meter, Gain above 0 dB, FX slots, Aux/Group sends and goes through the Master; its solo works like any track's. Audio exports can include it ("Include the attached video's audio", off by default).
- The video picture is now frame-accurate: the frame shown only depends on the score's position (the same frame at the same place, every time), follows the engine's clock during playback with latency compensation, and loops without freezing. Uses the FFmpeg bundled with Qt, no setup needed; falls back to the previous player otherwise.
- Clicking a video position the score can't reach (before its start, after its end) still shows that frame.

### 2026-09-29
- Right-clicking a staff played by a VST3 instrument now offers the same "Articulation map" submenu as the Mixer (Load, Edit, Reload, Remove), for the instrument at the clicked position.
- In automation mode, the curve points inside the score's range selection are selected together: dragging one moves them all up or down at once (across lines and instruments), undone in one step.
- Added curved (Bezier) segments to the MIDI CC, Volume, Pan and Tempo automation curves: hovering a segment reveals a small diamond handle in its middle, dragged up or down to bend the segment (double-click to straighten it again), played exactly as drawn.
- In automation mode, the score's right-click menu now shows "MIDI CC" right below "Automation type".
- Added MIDI CC automation for VST3 instruments: a "MIDI CC" submenu in the toolbar's Automation dropdown and in a VST3 staff's context menu shows a Modulation (CC1), Volume (CC7) or Expression (CC11) curve over the staff, drawn and edited like the Tempo curve (add/move points, double-click to remove, value tooltip while dragging), and sent to the plugin during playback. "Other MIDI CC…" picks any of the 128 controllers from a searchable list; the ones picked are kept with the score.
- The toolbar's Automation button now shows the name of the curve being edited (Dynamics, Tempo, Modulation...).
- Added an articulation map editor (View menu, or a Mixer track's new "Articulation map" submenu) to build a sample library's articulations without writing the text file by hand: folders become the picker's submenus, drag and drop sets the order, each articulation gets its activation sequence (Note On + Off / MIDI CC / Program Change), color, delays and default flag, and can be disabled without deleting it. Opened from a track, it can save and reload the map into it in one click.
- The articulation lane now marks which note a click would give an articulation to, and keeps marking it while the articulation menu is open.

### 2026-09-28
- Added articulation maps for third-party VST3 instruments (Kontakt, VSL Synchron Player, EastWest Opus, Musio...): a text file per sample library describes each articulation's keyswitches/CC/program change, loaded per instrument from the Mixer, and score articulations (staccato, pizz., tremolo...) switch it automatically.
- Added an "Articulations" toolbar toggle showing a lane under each staff with the articulation each passage plays, where clicking a note picks another articulation (e.g. play a notated staccato as staccatissimo) and dragging a chip adjusts when the change is sent. The lane is see-through, and a hovered chip too, so the notes under it stay visible.

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

- Fixed an automation curve that could stay frozen on screen while the score was scrolled, when two lines started with measures of the same index (e.g. multimeasure rests) (2026-09-29).
- Fixed MuseScore hanging on quit (the score closed, leaving an empty frozen window that had to be force-quit) after a VST3 plugin's editor had been opened, even once closed — e.g. Kontakt: plugins are now unloaded properly at shutdown (2026-09-29).
- Fixed removed audio tracks (deleted instruments, changed sounds, closed projects) never releasing their VST instrument, effects and loaded samples until MuseScore quit (2026-09-29).
- Fixed automation curves with a repeat: points drawn after the repeat actually went into its second pass (so nothing was played after the repeat), and editing a repeated bar moved a point shown in another bar (2026-09-29).
- Fixed automation curves (dynamics, and MIDI CC) playing at the wrong time after a repeat when Play Repeats is off (2026-09-29).
- Fixed automation curves in Page view drawing a point near a line break on the neighboring line too, where it could be dragged by mistake (2026-09-29).
- Fixed pressing on an automation line and dragging right away snapping the new point back to the bottom instead of creating it where it was dropped (2026-09-29).
- Fixed Tempo curve points that couldn't be removed: a user point hidden under a tempo marking's point, and an edited tempo marking's point after the marking itself was deleted (2026-09-29).
- Fixed a crash (then a frozen app with audio still playing) when starting playback after saving in Continuous view: saving's thumbnail relayout left the playback cursor and the score view pointing at deleted layout (2026-09-28).
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
- Fixed the Mixer's Video channel meter lighting up during playback of a video that has no audio track (2026-09-27).
- Fixed the Video panel showing up empty (no player) when it was restored open along with a score, until it was hidden and shown again (2026-09-29).
- Fixed the instruments starting up to one engine cycle late after a count-in, by a different amount on every playback (2026-09-30).
- Fixed adding an FX to a Mixer Aux channel resetting its displayed fader, pan, gain and color (2026-09-30).
- Fixed audio exports going ahead while the attached video's audio was still loading right after opening a project, and canceling an export being ignored while it waited for the playback to be ready (2026-09-30).
- Fixed a floating panel (e.g. the Mixer, full screen or not) restored at startup keeping its old, smaller content size inside its window (2026-10-01).
- Fixed thousands of "Cannot read property ... of null" warnings logged by the Mixer when quitting or closing a score with the Mixer open (2026-10-01).
- Fixed a crash when dragging a panel to an edge of the main window, and "Dock" doing nothing, once the saved layout had lost the panel's (or the docking zones') docked location (2026-10-01).
- Fixed opening a score logging a "not found type for tag" error for nearly every note (its "eid", or a playback offset): a stock bug, the tags were read fine (2026-10-01).
- Fixed the Harp palette's pedal diagrams rewriting their text on every layout, outside of any undoable step (logging "called outside of transaction" at startup and while drawing the palette): a stock bug (2026-10-01).
- Fixed creating a new score ending the undo stack's lock halfway through (the rest of the setup ran unlocked, and a never begun undoable step was ended), with ~140 "called outside of transaction" warnings: a stock bug (2026-10-01).
- Fixed the app freezing (endless toolbar relayout) when the notation toolbar was a few pixels too wide to fit, e.g. after creating a new score with some instruments: it kept switching between compact and full mode: a stock bug of the new docking engine (2026-10-01).
- Fixed opening a score saved without the custom Mixer bus data (e.g. by stock MuseScore) marking it as modified (2026-10-01).
- Fixed the docked Video panel not narrowing below the sidebar's width once its right-side sidebar was hidden (2026-10-01).
- Fixed the Audio FX slot's menu arrow being centered on a blank slot instead of on its right (2026-10-01).
- Fixed the main window (KDDockWidgets v2) reopening shrunk to a tiny size after being kept maximized, or minimized to the Dock after quitting while minimized (2026-10-01).
- Fixed clicking a measure cell in the Timeline (also with Shift/Ctrl, or a lasso) selecting the measure before the clicked one: a stock bug (2026-10-01).
- Fixed changing only a track's color in the Mixer not marking the project as modified: a stock bug (2026-10-01).
- Fixed the Mixer not picking up an instrument track's Mute/Solo changed from outside it (e.g. the Timeline): its channel and global Mute/Solo buttons stayed off (2026-10-01).
- Fixed notes getting stuck on Stop once a VST3 track plays on several MIDI channels: the playing notes' key could collide across channels (e.g. pitch 62 on channel 1 and 60 on channel 2), so one of them never got its note-off: a stock bug (2026-10-03).
- Fixed a VST3 plugin's state being saved outdated when it was changed with its window open, e.g. an instrument loaded in Kontakt: the plugin only reports it through restartComponent(), which was ignored: a stock bug (2026-10-03).
- Fixed the articulation, note offset and velocity lanes and the automation curves in a part with multimeasure rests: the lanes lost every chord after the first multimeasure rest of a line, and dragging a curve point there moved it onto a single position (overwriting other points) (2026-10-03).
- Fixed the controllers (mod wheel, sustain, pitch bend...) played on the MIDI keyboard from a part going to the score's first instruments instead of the part's: a stock bug (2026-10-03).
- Crashes can now be investigated: the crash handler writes the call stack to the log output and lets macOS write its crash report (it used to end the app silently, leaving neither, and plugins like Kontakt prevent running under a debugger) (2026-10-03).
- Fixed the articulation lane not letting a mark be placed on the notes before the first mark (e.g. in measure 1) when the articulation map has no default articulation (2026-10-04).
- Fixed choosing the ASIO audio driver on Windows leaving MuseScore silent, with empty "Audio device" and "Buffer size" lists, until it was restarted: it opened the first ASIO driver listed (e.g. Steinberg's "Generic Low Latency ASIO Driver", without any output) instead of the audio interface's. It now opens the ASIO driver used last, else the first one with stereo outputs, else any other that opens; when none opens, Preferences say so and go back to the previous driver. Also: a driver that fails to open is released (it could stay blocked), each driver is probed once per run, a failed start is reported instead of a silent driver that looks opened, the driver isn't reopened anymore on mere resync/latency notices, and choosing a device while none is opened opens it right away (2026-10-10).
- Fixed, on Windows and Linux, releasing Alt after an Alt+drag or Alt+click (velocity paint, dynamics points without snapping, copying an element...) highlighting the menu bar, which sent the next keys (arrows, letters) to the menu instead of the score; Alt pressed and released alone still highlights it (2026-10-10).
- Fixed the articulation map editor not updating the track when saving a track's map whose original file isn't on this computer (e.g. a score received from someone else): the embedded copy was edited and saved to a new file, but the track kept its old copy, so reopening the editor showed the old map (e.g. colors set with "Colors" lost). A map opened from a track (Edit…) now updates that track when saved, unless another map was opened or a new one started in the editor, or the track's map was removed meanwhile (2026-10-10).
