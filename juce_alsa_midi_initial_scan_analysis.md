# JUCE ALSA MIDI: Why the Standalone GUI Works and surge-xt-cli Does Not

## The structural bug

`AlsaMidiHelpers::Client` (in `juce_Midi_linux.cpp`) maintains `cachedEndpoints`, a map of
known ALSA sequencer clients and ports. Before the patch, this map was initialised empty and
was only ever updated by `notifyPortsChanged()`, which is called in response to ALSA sequencer
events (`SND_SEQ_EVENT_PORT_START`, `SND_SEQ_EVENT_CLIENT_CHANGE`, etc.). Devices that are
already present when the `Client` is constructed never generate such events, so the cache
remained empty for the lifetime of the application.

## How the standalone GUI works around this

The GUI is saved by an indirect side-effect of its own initialisation:

1. `AudioDeviceManager` is a member of `StandalonePluginHolder` and is default-constructed
   before the plugin loads. Its own member `midiDeviceListConnection` calls
   `MidiDeviceListConnection::make()`, which creates the `MidiDeviceListConnectionBroadcaster`
   singleton, which creates `ump::Endpoints`, which creates the ALSA `Client`, which creates
   `EndpointsImplNative`.

2. `EndpointsImplNative` owns an `AnnouncementsPort` member that calls
   `snd_seq_connect_from(seq, portId, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE)`
   to subscribe to system-level port announcements. The act of creating this subscription causes
   ALSA to deliver a `SND_SEQ_EVENT_PORT_SUBSCRIBED` event back to JUCE's own sequencer handle.

3. `SND_SEQ_EVENT_PORT_SUBSCRIBED` is in the `systemEvents` list that `SequencerThread` watches.
   When the background thread processes it, it calls `notifier.triggerAsyncUpdate()`, which
   posts an async callback to the JUCE message thread.

4. Because the GUI runs a proper JUCE message loop, `handleAsyncUpdate()` fires within
   milliseconds, calling `notifyPortsChanged()` → `findEndpoints()` → `cachedEndpoints`
   populated. The `midiDeviceListConnection` callback then fires
   `AudioDeviceManager::midiDeviceListChanged()`, which re-enables any previously saved MIDI
   devices.

By the time a user can interact with the settings page or play a note, `cachedEndpoints` is
already correct. The timing looks accidental but is structurally reliable: human interaction
always comes after the message loop has had time to deliver the async update.

## Why surge-xt-cli fails

The CLI calls `juce::MidiInput::getAvailableDevices()` synchronously and immediately after
startup, on the same thread and in the same call stack that first creates the `Client` and
`AnnouncementsPort`. The `PORT_SUBSCRIBED` event is queued in the ALSA kernel buffer at that
moment, but the `SequencerThread` has not yet polled for it, and even if it had, the resulting
`triggerAsyncUpdate()` cannot be delivered because the CLI does not run a JUCE message dispatch
loop before (or during) the `getAvailableDevices()` call. The function returns an empty list
and the application has no further opportunity to correct it.

## The fix

Adding `cachedEndpoints = findEndpoints(handle.get())` in the `Client` constructor body
(after all members, including `inputThread`, are initialised) performs a synchronous enumeration
of every ALSA sequencer client and port that exists at that moment. This makes the initial state
of `cachedEndpoints` correct regardless of whether a message loop is running, and without
requiring any change to callers.

## How fragile is the GUI's workaround?

The GUI's mechanism depends on four independent conditions holding simultaneously:

1. **`midiDeviceListConnection` must remain a default-initialized non-static member of
   `AudioDeviceManager`.** If JUCE ever deferred its initialization, the `Client` would be
   created later, possibly too close to the first `getAvailableDevices()` call.

2. **`SND_SEQ_EVENT_PORT_SUBSCRIBED` must stay in the `systemEvents` array** in
   `SequencerThread::processEvent()`. It is there alongside the obvious port-change events, but
   a future cleanup that narrowed the list to semantically meaningful events could remove it
   silently.

3. **ALSA must continue to deliver `PORT_SUBSCRIBED` back to the subscribing client itself.**
   This is standard sequencer behaviour today but is a kernel implementation detail, not a
   documented API contract.

4. **The message loop must process the async update before any MIDI-critical code runs.** This
   holds for human-driven interaction but is a timing assumption. An automated test, a DAW host
   that queries MIDI immediately on load, or a GUI refactor that delays the event loop could
   violate it.

Any one of these changing in a JUCE upgrade, kernel update, or Surge refactor would silently
reintroduce the bug for the GUI. The patch removes all four dependencies.

## Does the patch risk breaking the standalone GUI?

No. The existing async mechanism (PORT_SUBSCRIBED → `notifyPortsChanged()` →
`midiDeviceListChanged()`) is left intact and still fires shortly after startup. The only
observable change is that `cachedEndpoints` is correct at the moment `initialise()` first calls
`openLastRequestedMidiDevices()`, so saved MIDI devices are re-enabled on that first call rather
than waiting for the async callback. When `midiDeviceListChanged()` fires a moment later and
calls `openLastRequestedMidiDevices()` a second time, `setMidiInputDeviceEnabled()` is a no-op
for already-enabled devices (it checks `isMidiInputDeviceEnabled()` first). No device is opened
twice. The patch is a strict improvement for the GUI as well.
