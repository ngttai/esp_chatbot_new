# Licensing and third-party notices

Original code owned by this project's author is licensed under the Apache
License, Version 2.0, unless a file explicitly states otherwise. See [LICENSE](LICENSE).
This grant covers only rights held by the project author; it does not relicense
third-party code, fonts, images, animations, audio or other assets.

Existing per-file SPDX identifiers, copyright notices and upstream license
terms remain applicable, including files marked `CC0-1.0`. A missing license
header does not establish project ownership or permission to relicense an
imported file.

## ESP-Brookesia and ESP Speaker UI

- Upstream: <https://github.com/espressif/esp-brookesia>.
- This application is derived from the Brookesia `example_agent_chatbot`
  example. Its UI includes a port of the ESP Speaker example from Brookesia v0.6.
- The Brookesia upstream distribution includes the Apache-2.0 license.
  Espressif copyright and per-file notices are retained where present.
- Local Brookesia component overrides live in `components/`. Their upstream
  notices remain applicable to the imported portions.
- The UI import provenance and path mapping are recorded in
  [PORTING_MANIFEST.md](main/modules/display/speaker_ui/PORTING_MANIFEST.md).
  That manifest documents origin/integrity; it is not a license grant.

## Build dependencies

ESP-IDF, LVGL, SDL2 and Component Manager packages retain their respective
licenses. Exact firmware package versions are recorded in `dependencies.lock`;
direct dependencies and overrides are declared in `main/idf_component.yml`.
Consult each resolved package's license and notice files, including those in
`managed_components/`, when distributing a firmware binary. The Apache-2.0
license in this repository is not a substitute for dependency-specific notices.

## Emote assets

Assets under `assets/emote/` originate from Espressif's
[Emote Pack.zip](https://dl.espressif.com/AE/Emote%20Pack.zip), as identified by
the project author. The pack is linked from
[`espressif2022/esp_emote_gen_player` v0.0.2, revision `7139b46`](https://github.com/espressif2022/esp_emote_gen_player/tree/7139b46c6616d466ff153cb9d2ddf63661434f22/).

This is a source attribution only. The downloaded pack has no license file
according to the project author; its asset license is not yet confirmed.
The player's Apache-2.0 declaration is not asserted as the license for this pack.

## Assets and remaining provenance checks

The following groups need asset-level provenance/license verification before
claiming a complete redistribution audit:

- Imported UI fonts, icons, AI-profile imagery and Clock/Weather resources under
  `main/modules/display/speaker_ui/`.
- Asset-specific terms and source-pack correspondence for `assets/emote/`,
  whose reported origin is recorded above.
- Audio files under `littlefs/xiaozhi/`.

The presence of these assets in the repository, or an Apache-2.0 license on
related source code, does not by itself establish their redistribution terms.
README simulator screenshots depict these UI resources and do not grant any
additional rights to the underlying assets.

This file is an attribution and scope summary, not a complete third-party
license inventory or confirmation that every release asset has been cleared.
