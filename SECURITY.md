# Security policy

## Reporting a vulnerability

Please report security problems privately through GitHub: open the repository's **Security** tab and choose **Report a vulnerability**. Do not open a public issue for anything that could expose stream keys, API keys or let another program or website control Tandem.

Include the Tandem and OBS versions, your operating system, steps to reproduce, and what an attacker could do. Never include your real stream key or API key; a made-up value is fine.

You can expect an acknowledgement within a week. Fixes are released as soon as they are ready, and reporters are credited in the changelog unless they prefer not to be.

## Supported versions

Only the latest release gets security fixes.

## Scope notes

- The overlay server binds to `127.0.0.1` only and rejects requests whose `Host` header is not `127.0.0.1:<port>` or `localhost:<port>`. Reports about reaching it from another machine or from a web page are in scope.
- Stream keys and the YouTube API key must never appear in logs, URLs or `tandem.json` on Windows. Any case where they do is in scope.
- Antivirus false positives on release files are not vulnerabilities, but please open an issue so we can submit the file for review.
