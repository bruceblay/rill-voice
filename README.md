<img src="docs/images/characters.png" alt="The six Rill Voice visuals: Trio, Rubin, Eyes, Carriage, Windows and Portrait" width="800">

# Rill Voice

**rill** /rɪl/ *noun*: a small stream or a tiny, shallow channel cut into soil by running water.

A generative vocal instrument for the **M5Stack StickS3**. Six synthesized singers (alto, bass, choir, falsetto, tenor and vocoder) sing evolving melodies in scat and vocables, "da di doh", "loo", "mm", never words. Six visual characters of faces, eyes and people sing along with what you hear. Tap for a new piece. Shake for a new visual.

Prototype. Not yet on M5Burner or in Rill Sound.

**Rill family:** [Synth](https://github.com/bruceblay/rill-synth) · [Mallet](https://github.com/bruceblay/rill-mallet) · [World](https://github.com/bruceblay/rill-world) · [Drums](https://github.com/bruceblay/rill-drums) · [Rill Sound](https://rillsound.com)

## Sound

Mallet's composer (itself Rill's) writes the music: phrases that develop, answers, harmony, delay and room. The voice layer is formant synthesis. A glottal pulse runs through four cascaded resonators that glide from a consonant to a vowel. The consonants are all voiced ones (d, b, n, m, l), made from closure and formant motion rather than noise. The vocoder is a saw carrier through a ten-band filter bank that follows the same vowels.

Each part (lead, answer, support) is one monophonic singer, so a line is sung legato from note to note. Each piece picks one vocabulary (da di doh doom, la li lo loo, na ni no, ah eh oh) and a repeated phrase is sung the same way each time.

## Visuals

Flat cut-paper figures on Rill's daylight palettes, with eyes and mouths cut back to the paper. Mouths follow the engine's actual vowels: open on "ah", a slit on "ee", round on "oo", shut for the "m" of "doom".

- **Trio**: three singers, one to a part.
- **Rubin**: two profiles facing, and the vase between them.
- **Eyes**: an eye opens for each note, looks toward the newest and fades.
- **Carriage**: passengers on a commuter train, three of them singing.
- **Windows**: a street at evening, a singer at a window for each part.
- **Portrait**: one large face singing the lead.

## Controls

| Control | Action |
| --- | --- |
| Front button: tap | New piece |
| Front button: hold | Fade sound out or in |
| Side button: tap | Cycle volume |
| Side button: hold | Slow the ensemble a step |
| Shake | New visual |

## Ensemble

Voice uses the same ESP-NOW sync as Synth, Mallet, World and Drums (protocol version 2), sharing tempo, bar line and key with them.

## Build and install

```sh
python3 tools/test.py
python3 tools/flash.py --port /dev/cu.usbmodemXXXX
```

`tools/render.cpp` writes a WAV of one singer; `tools/visual_preview.cpp` renders one visual character driven by the engine.

## License

GPL-3.0-or-later.
