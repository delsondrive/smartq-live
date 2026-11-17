# Third-party dependencies

Adapted from Smart Q's list. Re-check the JUCE 9 terms (<https://juce.com/legal/juce-9-licence/>) before any release.

## Shipped in the app

| Component | Version | Licence | Notes |
|---|---|---|---|
| JUCE | 9.0.4 | Dual: AGPL-3.0-only, or the JUCE 9 commercial licence (EULA) | Smart Q Live uses the EULA, Starter type, as Smart Q does. See Smart Q's `THIRD_PARTY.md` for a summary of the Starter terms. |
| Steinberg ASIO headers (bundled in JUCE) | see `JUCE.spdx.json` | Steinberg's ASIO licence | JUCE bundles Steinberg's ASIO headers, whose licence terms apply to a build with `JUCE_ASIO=1`. |
| zlib, HarfBuzz, SheenBidi, LunaSVG, PlutoVG, libpng, jpeglib, libwebp, FLAC, libogg, libvorbis, Opus (bundled in JUCE) | see `JUCE.spdx.json` | Zlib, MIT, Apache-2.0, IJG, BSD-3-Clause and others | Only code from the JUCE modules that Smart Q Live links ends up in the binary. |
| cpp-httplib | v0.64.0 (commit 24e58ce5b25d55045af4cd6f463e83a8082fbe6b, 2026-10-29) | MIT | The phone remote's web server, header only, built without TLS or compression. Copyright (c) 2017 yhirose. Pinned to the newest release at least two weeks old when chosen (2026-11-17), and after v0.20.1, which fixed CVE-2025-46728 (bodies sent in chunks or without a length could pass the size limit); the server also refuses such bodies itself. |
| Montserrat SemiBold, as outlines only | 2024 | SIL Open Font License 1.1 | The letters of "Smart" in `assets/smart-q-logo.svg` are vector outlines made from this font. Copyright 2024 The Montserrat.Git Project Authors. The font itself is not included. |

## Build and test only (not shipped)

| Component | Version | Licence | Notes |
|---|---|---|---|
| Catch2 | v3.16.1 | BSL-1.0 | Test framework, linked into `sqlive_tests` only. |
| CMake, Visual Studio Build Tools | | Microsoft and Kitware terms | Build tools only. |

## The cue voice

No recorded voice ships with Smart Q Live, so there is nothing to license: the cue clips are spoken by the computer's own
voice (Windows speech, or `say` on a Mac) on the user's computer.
