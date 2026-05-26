# Fix: CLI MIDI Device Discovery on Linux

## Problem

On Linux, `surge-xt-cli --all-midi-inputs` (and `--list-devices`) reports
"Found 0 devices!" even when MIDI devices are connected at startup.

## Root cause

JUCE's ALSA MIDI backend discovers devices asynchronously. The first call to
`MidiInput::getAvailableDevices()` creates an `AlsaMidiHelpers::Client` and
starts a `SequencerThread`. When the Client subscribes to ALSA system
announcements, ALSA immediately queues a `SND_SEQ_EVENT_PORT_SUBSCRIBED`
event. The SequencerThread picks this up, calls `triggerAsyncUpdate()`, and
posts a callback to the JUCE message thread. Only when that callback fires —
`notifyPortsChanged()` → `findEndpoints()` — is `cachedEndpoints` populated
and devices visible to callers.

The GUI always runs a message loop, so this callback arrives in milliseconds
before any user interaction. The CLI only ran a message loop when OSC was
enabled; otherwise the callback never fired and `cachedEndpoints` stayed empty
for the lifetime of the process.

## Fix (CLI only, no JUCE changes)

Three small changes to `cli-main.cpp`:

1. **`primeMidiDeviceDiscovery()`** — a Linux-only helper called before the
   real `getAvailableDevices()`. It makes one throwaway call to create the
   Client and start the SequencerThread, sleeps 150 ms (the SequencerThread's
   poll timeout is 100 ms, and the event is already queued so the first poll
   returns immediately), then drains the JUCE message queue via
   `juce::detail::dispatchNextMessageOnSystemQueue(true)` — the non-blocking
   single-shot dispatcher that `runDispatchLoop()` already uses internally,
   available without `JUCE_MODAL_LOOPS_PERMITTED`.

2. **Always run the message loop on Linux** (`needsMessageLoop = true`) — not
   just when OSC is active. This matches what the GUI does and also covers
   runtime events like MIDI hotplug.

3. **`--list-devices` fix** — `listAudioDevices()` constructs an
   `AudioDeviceManager`, whose `midiDeviceListConnection` in-class initializer
   creates the ALSA Client. The `MessageManager` must therefore exist *before*
   `listAudioDevices()` is called, or `triggerAsyncUpdate()` has no queue to
   post to. One line added before the `listAudioDevices()` call.

## Tested on

Raspberry Pi aarch64, Patchbox OS, ALSA 1.2.8, six MIDI devices connected at
startup — both `--all-midi-inputs` and `--list-devices` now report all six
devices correctly.
