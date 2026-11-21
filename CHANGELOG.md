# Changelog

## [Unreleased]
### Added
- First version of Apex by DSK: a one-button generator of trailer effects and cinematic sound design, 100 % synthesised (no samples).
- 9 effect types with 4 styles each: Riser, Impact, Whoosh, Braam, Tension, Reverse, Downer, Glitch and Transition.
- Exact duration in seconds, beats (host tempo) or frames (24 / 25 / 30 / 60 fps); the climax lands exactly on the Hit Point; head and tail.
- Six macros (Intensity, Size, Dark / Light, Stereo Motion, Tail, Character) plus key and octave; six editable layers (Sub, Body, Transient, Noise, Metal, Tail).
- Deterministic by seed: the plugin playback equals the offline render bit for bit; background renderer, never on the audio thread.
- Trigger by button, by MIDI note (same effect, or a different variation per note) or locked to the DAW timeline.
- Drag the effect out as a WAV; export WAV / FLAC / OGG (44.1 / 48 / 96 kHz) with head and tail; export a pack of numbered variations with pack.json / pack.csv.
- Loudness target in LUFS (BS.1770-style K-weighting with gating) and a -1 dBFS look-ahead limiter; automatic checks for level, DC, NaN and clicks.
- Waveform + spectrogram timeline with the Hit Point, a ruler in seconds / bars / frames and a playhead; GPU cinematic visual with a painted fallback.
- Rule-based designer (GENERATE, New seed, Mutate, Mix) with best-of-N validation; 252 factory effects; preset browser, A/B, undo / redo, MIDI learn.
- Bilingual interface (English / Español), Help page, installer, CI workflows (Windows, macOS universal, Linux).
