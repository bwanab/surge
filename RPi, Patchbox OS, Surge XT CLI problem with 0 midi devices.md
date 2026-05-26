Here's a summary you can paste into a new chat:

**Goal:** Run `surge-xt-cli` headlessly on a Raspberry Pi (Patchbox OS, aarch64) with MIDI input from a Roland AE-30 windsynth or a midi keyboard via JACK/ALSA.

**Problem:** `surge-xt-cli --all-midi-inputs` consistently reports "Found 0 devices!" regardless of audio interface chosen.

**Environment:**

- Patchbox OS (Raspberry Pi), aarch64
- ALSA 1.2.8
- JACK running as user `jack`
- a2jmidid bridging ALSA devices into JACK
- surge-xt-cli built from latest GitHub source using GCC
- AE-30, pisound, and virmidi ports all visible in `arecordmidi -l` and `/proc/asound/seq/clients`

**Investigation findings:**

- `amidi -l` and `arecordmidi -l` show devices correctly
- `/proc/asound/seq/clients` shows all expected MIDI clients
- `/dev/snd/seq` permissions are correct; `patch` user is in `audio` group
- `strings` on the binary confirms `JUCE_ALSA=1`, `JUCE_JACK=1`, and ALSA sequencer functions are compiled and linked
- `libasound2-dev` was present at build time
- The binary uses **JUCE's AlsaMidiHelpers** (not RtMidi)
- JUCE's `juce_ALSA_weak_linux.h` declares UMP functions as weak symbols; these resolve to non-null at runtime on ALSA 1.2.8 even though the system headers don't expose them properly (ALSA 1.2.8 added partial UMP support). The guard `#if SND_LIB_VERSION < 1.2.10` means JUCE uses stub UMP structs that are too small, potentially causing `snd_seq_get_ump_endpoint_info` to return 0 (success) for legacy devices and misclassify them — **but disabling this code path did not fix the problem**
- `getAvailableDevices()` calls `MidiDeviceListConnectionBroadcaster::get().getAllMidiDeviceInfo()` which depends on `ump::Endpoints::getInstance()` — a singleton that discovers devices asynchronously on the JUCE message thread
- The message thread is set up just before engine initialization, but `getAvailableDevices()` is called immediately after with no time given for async device discovery to complete
- **Current hypothesis:** the Endpoints singleton hasn't finished scanning before `getAvailableDevices()` is called — a race condition

**Attempted fix (in progress):**

- Added a delay loop before `getAvailableDevices()` using `runDispatchLoopUntil(50)` — blocked by `JUCE_MODAL_LOOPS_PERMITTED=0` in this build
- Next attempt: replace with `juce::Thread::sleep(500)` to give the Endpoints singleton time to complete device discovery before querying