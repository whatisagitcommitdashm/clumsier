# Presets and sequences

A preset contains one traffic selection, a delay policy, and 1-64 named steps. Every step uses Lag; other effects are excluded. Choose **Added delay** to specify independent inbound/outbound delays, or **Target ping** to describe the ping you want rather than the amount to add.

## Simple and Advanced views

The app opens in simple mode on **Quick controls**, alongside **Sequences** and **Hotkeys**. Quick controls lets you turn effects on/off and change their amounts without a saved sequence. Start captures incoming traffic by default; custom filters remain available in Advanced mode. **Advanced mode** reveals the traffic filter, per-effect direction switches, technical delay details, and network fields in the sequence editor. Switching views does not change capture, the active step, or saved settings. The view choice lasts for the current session; command-line launches with settings use Advanced mode.

New sequences use inbound delay, any protocol, inbound capture, no remote IP, port 0, and no Windows filter. Simple mode hides these fields. Existing custom settings are preserved when editing an imported or previously customized sequence; the editor indicates when custom settings are present.

Target-ping steps show the desired ping or Lowest available. Added-delay steps show the applicable delay fields; with the default inbound policy there is just one Added delay field. Wrap at sequence ends sits beneath the step editor. Server selection appears first, followed by sequence selection and its preview. The server baseline is used only for target-ping sequences.

## First run

1. Open **Sequences** and select a sequence, or choose **New sequence** to make your own. Fresh beta libraries include Skylands, Pirate Bay, and Good Basic.
2. Enter your usual Mineplex ping when prompted. For other servers, use **Manage servers** to add their usual ping with Clumsier stopped, then select the server in the sequence's dropdown.
3. Review the preview, then start either with the **Start** button, or a hotkey.

## Targets and lowest available

A numeric target remains in the shared preset exactly as entered. If it is below your baseline, Clumsier adds zero delay and displays that the target cannot be reached. For example, target 30 with baseline 50 still says target 30 and expected approximately 50. It does not rewrite the target to 50.

**Lowest available** explicitly means zero added delay for everyone, whatever their baseline. It has no numeric target. This differs from a target of 0, which communicates an ideal numeric target that may be unreachable.

Compensation is `max(0, target - baseline)`. The delay policy applies all of it inbound, all outbound, or splits it between both directions. Odd milliseconds are split with the extra millisecond outbound, so the sum stays exact. This estimate assumes the selected traffic follows the normal remote-server round trip and matches the chosen directions. A custom native filter can exclude traffic needed for that estimate; Clumsier cannot infer those semantics from arbitrary filter expressions.

Target and baseline values are whole milliseconds from 0 to 60000. The backend supports at most 15000 ms of added delay per direction. Loading checks every step against the chosen baseline before accepting the sequence. A below-baseline target is supported; a delay exceeding the backend limit is an error.

## Editing and switching

The editor saves the current step's fields when you select another step, add a step, move a step, or save the preset. Removing a step discards that step's fields. Names and optional notes travel with the preset. Changing modes asks before resetting the step values; review them before saving.

Traffic fields apply to the whole sequence. A blank remote IP and port 0 mean any address/port; a specific port requires TCP or UDP. Leave the Windows filter blank to use these portable fields. A nonempty Windows filter overrides them and records `windivert` as the required backend.

Selecting a sequence starts at step 1. Switching sequences or Quick Controls while running applies the selected settings and stays running. A traffic-selection change restarts capture internally; a failed change attempts to restore the previous configuration and reports the result. Advanced mode exposes traffic settings; the retired IUP effects panel is no longer present.

Next and Previous stay at the ends unless **Wrap at sequence ends** is enabled. Reset returns to step 1 without stopping capture. Stop preserves the selected step; Start resumes with that step's settings. While stopped, you can navigate steps without starting capture.

Valid field edits apply when editing ends, including during capture. Invalid entries restore the previous value. Save persists edits; Autosave can do this automatically. Discard restores the saved configuration. Changing a baseline updates the calculation; deleting the active sequence stops capture. Restarting keeps the library but does not start capture.

Hotkey actions pause while preset/profile editors, file dialogs, and deletion confirmations are open. Input still passes through to other applications. Closing an editor waits for held keys to be released before rearming shortcuts.

Changing steps uses the existing live Lag update: held packets keep their original enqueue times and follow the new delay on the next processing pass. Lowest available releases queued Lag packets on that pass and bypasses intentional delay for new packets; capture remains active.

## Files and sharing

- `%LOCALAPPDATA%\Clumsier\presets\`: one versioned JSON file per preset.
- `%LOCALAPPDATA%\Clumsier\profiles\`: separate local server names and baseline values.
- `%LOCALAPPDATA%\Clumsier\hotkeys.ini`: your hotkeys, independent of presets.

Export includes the original targets, names, notes, traffic selection, mode, delay policy, loop preference, and optional `server` name. It includes neither personal baseline ping nor hotkeys. Import validates and selects a separate saved item with capture stopped. Matching server names reuse local baselines; unfamiliar servers prompt for a baseline. Older files without a server name still import and use manual server selection. Same-name imports remain separate.

The JSON is intentionally readable and editable. `examples/base.json` is a complete target-ping example. In added-delay mode, use `"mode": "added_delay"` and steps such as:

```json
{ "name": "Strong leap", "note": "", "type": "delay", "inbound_ms": 300, "outbound_ms": 0 }
```

A direction excluded by the delay policy must have delay 0. A lowest step has no timing fields; a target step has only `target_ms`. The parser rejects unknown fields, duplicates, unsupported versions, invalid types, and out-of-range values. Files are limited to 256 KiB and libraries to 256 visible items per category. Unreadable or excess library files are reported and left untouched. Saves write a temporary file beside the destination and replace it only after a successful write and flush.
