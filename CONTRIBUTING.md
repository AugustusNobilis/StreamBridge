# Contributing

Use Windows x64, Visual Studio 2022 with Desktop C++, CMake 3.28+ and the pinned dependencies. Follow [BUILD-WINDOWS.md](docs/BUILD-WINDOWS.md). Format C++ with clang-format 19 and run the relevant tests before a pull request.

Contributions must be GPL-2.0-or-later compatible. Preserve SPDX headers, attribute copied code and update THIRD_PARTY_NOTICES.md for dependencies. Do not import code based only on repository-level license assumptions.

Keep changes scoped: core policies, streaming, accounts, browser and installer have distinct boundaries. Report the concrete issue, changed behavior and executed validation. Mark unexecuted tests explicitly. Do not substitute mock API tests for platform consent or synthetic widgets for DonationAlerts acceptance.

Never commit `.deps`, build/stage/dist directories, credentials, widget URLs, private certificates, OBS profiles, crash dumps or real-user diagnostic logs. Use synthetic fixtures. Do not add telemetry, remote OBS commands, a system proxy change, TLS bypass, direct proxy fallback or bundled private OAuth client secret.

Rendering and network operations belong off the OBS UI thread. Preserve ownership and shutdown ordering, bounded queues and retry limits. A failed source must blank/fail closed. Changes to routing, browser sandbox or credential storage require focused security review and regression evidence.

GitHub Actions creates a **draft prerelease** only from a version tag matching CMake. Publishing the reviewed draft is a maintainer action. Provide matching source and dependency notices with every binary distribution.
