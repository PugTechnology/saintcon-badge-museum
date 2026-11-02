# SAINTCON Badge Museum

Play SAINTCON conference badges in your browser, running their real firmware compiled to WebAssembly.

**Live:** https://sc.pug.technology/

| Year | Badge | Address |
|---|---|---|
| 2026 | "Orion Trail" by [Professor-plum](https://github.com/Professor-plum) ([source](https://github.com/Professor-plum/SAINTCON-BADGE-2026), MIT) | https://26.sc.pug.technology/ |
| 2025 | "The Wrench" by [compukidmike](https://github.com/compukidmike) ([source](https://github.com/compukidmike/Saintcon2025)): original firmware, plus the [pug pet](https://github.com/PugTechnology/saintcon25-pug-pet) as a second mode | https://25.sc.pug.technology/ |
| 2024 | "Tower Battle" wrist badge by [compukidmike](https://github.com/compukidmike) ([source](https://github.com/compukidmike/Saintcon2024)): original firmware with a stand-in tower server | https://24.sc.pug.technology/ |
| 2023 | "Incidents" by [compukidmike](https://github.com/compukidmike) ([source](https://github.com/compukidmike/Saintcon2023)), with contributions from jarmknecht and klipperx: original firmware with a stand-in game server | https://23.sc.pug.technology/ |
| 2021 | Combination lock badge by [compukidmike](https://github.com/compukidmike), [Professor_Plum](https://github.com/Professor-plum) and [Sodium_Hydrogen](https://github.com/Sodium-Hydrogen) ([source](https://github.com/compukidmike/Saintcon2021)), with a firmware fix by [kfeuz](https://github.com/kfeuz): original firmware with stand-in NFC tags and a solo trade partner | https://21.sc.pug.technology/ |
| 2018 | LED matrix badge by [compukidmike](https://github.com/compukidmike) ([source](https://github.com/compukidmike/Saintcon2018)), with frozen modules added by miketweaver: the original MicroPython code running in MicroPython's WebAssembly port | https://18.sc.pug.technology/ |

Unofficial fan project. Not affiliated with or endorsed by [SAINTCON](https://saintcon.org/) or UtahSAINT.

## How it works

- Each year's original firmware is fetched from its author's repository at a pinned commit, built against a small
  platform layer (display, LEDs, buttons, storage, a pretend game server for servers that no longer exist), and
  compiled to WebAssembly with Emscripten. The firmware source itself is not copied into this repository.
- Conference secrets (Wi-Fi passwords, API keys) are scrubbed by the fetch scripts before anything is built.
- Your progress lives only in your own browser (localStorage). No analytics, no cookies.

## Layout

- `emulator/web/` is the whole website (static files). `emulator/web/badges/<year>/` holds each year's board
  picture, layout, compiled core and license notices.
- `emulator/cores/<year>/` has the fetch script, platform layer and build script for each year's original firmware.
- `emulator/web/badges/2025/core.js` and `core.wasm` are the pug pet, built in its own repo,
  [PugTechnology/saintcon25-pug-pet](https://github.com/PugTechnology/saintcon25-pug-pet).
- `emulator/deploy/wrangler.jsonc` deploys the site as a Cloudflare Worker with static assets.

## Run it locally

```bash
./tools/emu-serve            # serves emulator/web on http://localhost:8765/
```

Building cores needs Docker (Emscripten image): `emulator/cores/<year>/build.sh` builds each year's original
firmware. See `emulator/README.md`.

## License

The code made for this project (the emulator shell, platform layers, build scripts) is MIT licensed, see `LICENSE`.
The pug pet is MIT licensed in its own repo. The badge firmware belongs to its authors: the compiled cores in
`emulator/web/badges/<year>/` contain their code under their terms (the 2026 firmware is MIT; the 2018, 2021, 2023, 2024 and 2025 firmware
belongs to compukidmike and its contributors, as credited above), plus the third-party libraries listed in each
year's `THIRD_PARTY_LICENSES.txt`.
