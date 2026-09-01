# GitHub Pages Web Flasher

The repository contains a static ESP Web Tools installer at `index.html`. It
installs the complete `*-merged.bin` image for the selected target at flash
address `0x0`.

## Enable GitHub Pages

1. Push the complete contents of `GitHub-Release` to the repository root.
2. Open the repository on GitHub.
3. Select **Settings**, then **Pages**.
4. Under **Build and deployment**, select **GitHub Actions**.
5. The included `.github/workflows/pages.yml` workflow deploys the static site.
6. Open the HTTPS address shown by GitHub after deployment completes.

The expected address is usually:

```text
https://icecube20.github.io/Open-Fume-Extractor/
```

ESP Web Tools requires HTTPS and Web Serial. Use a current desktop version of
Chrome or Edge. Mobile browsers and Firefox do not currently provide the
required Web Serial interface.

## Release updates

Run the internal release generator after compiling every firmware target:

```powershell
.\tools\prepare_github_release.ps1
```

The generator performs the following work:

- verifies target and version markers
- verifies that every merged image contains the matching application image
- signs and verifies the OTA packages
- refreshes firmware files and SHA-256 checksums
- regenerates the ten ESP Web Tools manifests
- refreshes the firmware catalog used by the web flasher

Do not edit generated manifest versions or firmware paths manually. Commit the
refreshed `GitHub-Release` tree after the generator completes successfully.

## File selection

- The web flasher uses only `*-merged.bin` files.
- Signed normal `.bin` files belong in the master or display OTA updater.
- Selecting the wrong module family can make a device unbootable until the
  correct merged image is written again.
- Display 320x480 and Display 800x480 are separate targets.

The manifests set `new_install_prompt_erase` so the user can choose whether to
erase existing flash data. Erasing is required for a first installation, a
firmware-family change or a partition-layout change.
