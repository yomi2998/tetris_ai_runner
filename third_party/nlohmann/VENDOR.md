# nlohmann/json vendor snapshot

Upstream: https://github.com/nlohmann/json

Release: v3.12.0 (Release JSON for Modern C++ version 3.12.0)

Vendored file, unmodified single amalgamated header:

| File | sha256 |
| --- | --- |
| `include/nlohmann/json.hpp` | `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63` |

Provenance: the header carries version 3.12.0 and SPDX MIT attribution to Niels Lohmann in its banner. It was previously tracked at `src/nlohmann/json.hpp` and moved here without content changes so all third-party code lives under `third_party/`. Layout mirrors upstream (`include/nlohmann/json.hpp`), so existing `#include <nlohmann/json.hpp>` directives keep working once `third_party/nlohmann/include` is on the include path.

## License

MIT. Full terms in `LICENSE.MIT`. Copyright (c) 2013-2025 Niels Lohmann, matching the header banner.

## Consumers

- `src/match.cpp`
- `src/tournament/checkpoint.h`
- `src/tournament/tuner.cpp`
