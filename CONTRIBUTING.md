# Contributing

Thanks for helping. Bug reports, platform changes (Kick and YouTube change often), translations and fixes are all welcome.

## Before you start

- Search existing issues first. For larger changes, open an issue to agree on the approach before writing code.
- Keep pull requests focused: one fix or feature per pull request.
- Never put real stream keys, API keys, tokens or other people's chat logs in issues, code, fixtures or screenshots.

## Building

See [Building from source](docs/wiki/Building-from-Source.md). In short:

```
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
```

The chat providers and the overlay server do not depend on OBS or Qt and have their own tests:

```
cmake -S tests -B build_tests
cmake --build build_tests --config RelWithDebInfo
ctest --test-dir build_tests -C RelWithDebInfo --output-on-failure
```

`tandem-chat-probe` (built with the tests) connects to a real chat and prints what it receives, which is handy when a platform changes something:

```
tandem-chat-probe twitch <channel> 30
tandem-chat-probe kick <slug> 30
TANDEM_YT_KEY=<your key> tandem-chat-probe youtube <video url or id> 30
```

## Code guidelines

- C++20. New files start with `// SPDX-License-Identifier: GPL-2.0-or-later`. Match the style of the file you are editing.
- No network or blocking work on OBS's video, audio, graphics or UI threads. Chat providers run on their own threads and talk to the UI through `ChatHub`.
- Keep platform-specific details (URLs, event names, field names) as named constants at the top of the provider file.
- Never log secrets. Never put an API key in a URL.
- Parser changes need a test, with a sanitized fixture in `tests/fixtures/` (made-up names and ids).
- Do not add code from GPLv3-only projects; Tandem is GPL-2.0-or-later.
- New user-visible text goes into `data/locale/en-US.ini`.

## Pull requests

- Describe what changed and how you tested it (OBS version, OS, platforms).
- Add a line to `CHANGELOG.md` under "Unreleased".
- CI must pass.

By contributing you agree that your contribution is licensed under the GPL-2.0-or-later, like the rest of Tandem.
