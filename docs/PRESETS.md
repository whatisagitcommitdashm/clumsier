# Presets and sequences

A preset contains one traffic selection, a delay policy, and 1-64 named steps. Every step uses Lag; other effects are excluded. Choose **Added delay** to specify independent inbound/outbound delays, or **Target ping** to describe the ping you want rather than the amount to add.

## Simple and Advanced views

The app opens in simple mode on **Quick controls**, alongside **Sequences** and **Hotkeys**. Quick controls lets you turn effects on/off and change their amounts without a saved sequence. Start captures incoming traffic by default; custom filters remain available in Advanced mode. **Advanced mode** reveals the traffic filter, per-effect direction switches, technical delay details, and network fields in the sequence editor. Switching views does not change capture, the active step, or saved settings. The view choice lasts for the current session; command-line launches with settings use Advanced mode.

New sequences use inbound delay, any protocol, inbound capture, no remote IP, port 0, and no Windows filter. Simple mode hides these fields. Existing custom settings are preserved when editing an imported or previously customized sequence; the editor indicates when custom settings are present.

Target-ping steps show the desired ping or Lowest available. Added-delay steps show the applicable delay fields; with the default inbound policy there is just one Added delay field. Wrap at sequence ends sits beneath the step editor. Server selection appears first, followed by sequence selection and its preview. The server baseline is used only for target-ping sequences.

**Use this sequence** prepares step 1; **Start** starts capture. The active status above the tabs describes the current session, while the preview describes the library selection. Existing server profiles and hotkeys are available in both views. **Use quick controls** explicitly hands control back from a loaded sequence without changing capture state or the current settings. Changing tabs alone never changes the active sequence. Unload remains available in Advanced mode.

## First run

1. Open the **Sequences** tab and import `examples/four-leaps.json`, or choose **New sequence** to make your own.
2. For target-ping mode, create a **Add server / ping** with a server name and your average stable ping measured with capture stopped. Select that profile.
3. Review the preview, then choose **Use this sequence**. Loading does not start capture.
4. Start capture with F5 or the Start button. Use **Next**, **Previous**, or **Reset to first**. Assign their shortcuts in the **Hotkeys** tab; these three actions start unassigned. F5/F6/F7 keep their existing meanings.

With a 50 ms baseline, the example adds 150, 0, 100, then 50 ms inbound. The expected pings are approximately 200, 50, 150, then 100 ms. These are estimates based on your entered baseline, not measurements or a feedback controller.

## Targets and lowest available

A numeric target remains in the shared preset exactly as entered. If it is below your baseline, Clumsier adds zero delay and displays that the target cannot be reached. For example, target 30 with baseline 50 still says target 30 and expected approximately 50. It does not rewrite the target to 50.

**Lowest available** explicitly means zero added delay for everyone, whatever their baseline. It has no numeric target. This differs from a target of 0, which communicates an ideal numeric target that may be unreachable.

Compensation is `max(0, target - baseline)`. The delay policy applies all of it inbound, all outbound, or splits it between both directions. Odd milliseconds are split with the extra millisecond outbound, so the sum stays exact. This estimate assumes the selected traffic follows the normal remote-server round trip and matches the chosen directions. A custom native filter can exclude traffic needed for that estimate; Clumsier cannot infer those semantics from arbitrary filter expressions.

Target and baseline values are whole milliseconds from 0 to 60000. The backend supports at most 15000 ms of added delay per direction. Loading checks every step against the chosen baseline before accepting the sequence. A below-baseline target is supported; a delay exceeding the backend limit is an error.

## Editing and switching

The editor saves the current step's fields when you select another step, add a step, move a step, or save the preset. Removing a step discards that step's fields. Names and optional notes travel with the preset. Changing modes asks before resetting the step values; review them before saving.

Traffic fields apply to the whole sequence. A blank remote IP and port 0 mean any address/port; a specific port requires TCP or UDP. Leave the Windows filter blank to use these portable fields. A nonempty Windows filter overrides them and records `windivert` as the required backend.

Loading always selects step 1. During capture you can load another preset with exactly the same traffic selection. Stop capture before changing traffic selections. A loaded preset locks the manual effects panel; turn off other effects before loading one. **Unload** returns to manual control, keeping the current delay and capture state. In Advanced mode, the inherited manual delay knob edits both direction delays together when changed.

Next and Previous stay at the ends unless **Wrap at sequence ends** is enabled. Reset returns to step 1 without stopping capture. Stop preserves the selected step; Start resumes with that step's settings. While stopped, you can navigate steps without starting capture.

The active sequence is a snapshot. Saving edits, choosing a different baseline, or deleting its library file does not alter it. Choose Use this sequence to apply a saved edit or another baseline. The persistent active-step display reports the snapshot actually in use, while the preview shows the library selection. Restarting the app keeps the library and profiles but does not load or start a sequence automatically.

Hotkey actions pause while preset/profile editors, file dialogs, and deletion confirmations are open. Input still passes through to other applications. Closing an editor waits for held keys to be released before rearming shortcuts.

Changing steps uses the existing live Lag update: held packets keep their original enqueue times and follow the new delay on the next processing pass. Lowest available releases queued Lag packets on that pass and bypasses intentional delay for new packets; capture remains active.

## Files and sharing

- `%LOCALAPPDATA%\Clumsier\presets\`: one versioned JSON file per preset.
- `%LOCALAPPDATA%\Clumsier\profiles\`: separate local server names and baseline values.
- `%LOCALAPPDATA%\Clumsier\hotkeys.ini`: your hotkeys, independent of presets.

Export writes the selected saved preset, including its original targets, names, notes, traffic selection, mode, delay policy, and loop preference. It includes neither your baseline nor your hotkeys. Import validates the file and adds a new library item without activating it or overwriting another preset. Same-name imports remain separate; a short ID suffix distinguishes them in the selector. Duplicate also creates a separate item when saved.

The JSON is intentionally readable and editable. `examples/four-leaps.json` is a complete target-ping example. In added-delay mode, use `"mode": "added_delay"` and steps such as:

```json
{ "name": "First step", "note": "", "type": "delay", "inbound_ms": 100, "outbound_ms": 0 }
```

A direction excluded by the delay policy must have delay 0. A lowest step has no timing fields; a target step has only `target_ms`. The parser rejects unknown fields, duplicates, unsupported versions, invalid types, and out-of-range values. Files are limited to 256 KiB and libraries to 256 visible items per category. Unreadable or excess library files are reported and left untouched. Saves write a temporary file beside the destination and replace it only after a successful write and flush.

## Manual acceptance checks

Run `scripts\test-presets.cmd` first, alongside the existing regression scripts. Then check the following in the actual app:

1. Import the example, create baseline 50, and confirm the simple preview shows expected pings of 200, 50, 150, and 100 ms. In Advanced mode, confirm added delays of +150, +0, +100, +50 inbound. Load it and verify capture is still stopped.
2. Start capture and advance through all four steps in game. Confirm the active labels follow your hotkey presses, a held hotkey does not repeat, and the same input still reaches the game. Test Previous and Reset, both sequence endings, and F5/F6/F7 repeatedly.
3. At a high-delay step, switch to Lowest available. Check that traffic recovers without restarting capture. Stop during a high-delay step as another recovery check.
4. Try baseline 250: the numeric targets should remain 200/150/100 with zero compensation and a below-baseline notice. Lowest should say Lowest available without that notice. Also try an explicit numeric target 30 at baseline 50.
5. Edit a saved preset during capture: the active step and values must stay unchanged until Use this sequence. Select/edit another profile and verify the same behavior. Switching traffic while running should be rejected, leaving the active sequence intact.
6. Create an added-delay preset; test both directions with unequal values. Add, rename, reorder, and remove steps; cancel an edit; duplicate a preset. Check invalid values are explained without saving or activating them.
7. Export, import twice, and restart. Both imports and local profiles should remain; no sequence should auto-start. Inspect the export and confirm there is no baseline or hotkey data.
8. While an editor is open, type keys assigned to Start or Next and verify capture/step state does not change. Close it, release held keys, and verify shortcuts work again. Unload and test manual Lag plus another inherited effect.

For design feedback, focus on whether the active snapshot is clearly distinguished from the library preview, whether target/baseline/added-delay labels are understandable at a glance, and whether editing/switching needs too many clicks. Also check layout and scrolling at your usual display scaling. Automated callback tests do not verify visual layout or actual game timing.

## Checks for the simple-view pass

- Start the app normally: Advanced mode should be off. The filter and direction switches should be hidden. Quick controls should be the first tab, with Sequences and Hotkeys beside it. Test an effect with Start/Stop without selecting a sequence.
- Create a sequence in simple mode. Check that traffic fields are hidden and that wrap is below the steps. Inspect it in Advanced mode to confirm the documented defaults.
- Change between target ping and added delay. Check that only the applicable value fields are shown, and that lowest available disables the numeric target field.
- Edit a sequence with a custom filter in simple mode, save, then inspect it in Advanced mode. The filter and direction policy must survive unchanged.
- Toggle views during an active sequence. Capture, timing, and the active step must stay unchanged. Changing modes or tabs should preserve the current session. Use quick controls should explicitly unload the sequence and enable effect editing again.
- Check the wording and number of clicks needed to create a sequence, enter your usual ping, assign Next, and start playing. Check both modes at your normal display scaling.


Resize the main window narrower and shorter. Labels and selectors should fit where possible, and overflow should be scrollable rather than cut off. Check that the Advanced mode switch remains reachable.
