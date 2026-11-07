# Open Fume Extractor for Home Assistant

[![Open in HACS](https://my.home-assistant.io/badges/hacs_repository.svg)](https://my.home-assistant.io/redirect/hacs_repository/?owner=IceCube20&repository=Open-Fume-Extractor-Home-Assistant&category=integration)
[![Add integration to Home Assistant](https://my.home-assistant.io/badges/config_flow_start.svg)](https://my.home-assistant.io/redirect/config_flow_start/?domain=open_fume_extractor)

Local Home Assistant integration and dashboard cards for
[Open Fume Extractor](https://github.com/IceCube20/Open-Fume-Extractor).
Connect directly to your master over the local network; no MQTT broker or cloud
account is required. MQTT can continue to run independently for other clients.

This is the dedicated Home Assistant repository. Master and module firmware,
hardware documentation and the USB flasher remain in the
[firmware project](https://github.com/IceCube20/Open-Fume-Extractor).

## Requirements

- Home Assistant **2026.10 or newer** (tested on 2026.10.1).
- Open Fume Extractor Master **2.0.53RC or newer** is recommended.
- The master's IP/hostname and current web username/password.
- Local network access from Home Assistant to the master.
- HACS **2.x** for HACS installation, or filesystem access for manual installation.

The integration is a community release candidate, not an official Home Assistant
integration or a default HACS listing. It can be installed as a **custom HACS
repository**.

## Install with HACS

Use **Open in HACS** above to open this custom repository in your own Home
Assistant instance, add it if prompted, and download the integration. After
**restarting Home Assistant**, use **Add integration to Home Assistant** to start
setup. The setup button does not install the integration or HACS itself.

Manual HACS steps:

1. Open HACS and select **three-dot menu > Custom repositories**.
2. Add `https://github.com/IceCube20/Open-Fume-Extractor-Home-Assistant`.
3. Select type **Integration**, then add the repository.
4. Search HACS for **Open Fume Extractor** and download it.
5. Enable pre-release/beta versions for this repository if needed. Under
   **Need a different version?**, choose a numbered RC release, such as
   **v0.1.28rc1** (release title **0.1.28RC**). The `main` development branch is
   hidden from the HACS download
   selector; it is not the recommended installation source.
6. **Restart Home Assistant**, not just the integration.
7. Open **Settings > Devices & services > Add integration > Open Fume Extractor**.
8. Enter the master's IP/hostname and its **web** login, not a developer password.

The master and supported modules are created automatically. The native polling
interval is configurable from **0.5 to 60 seconds** in the integration options.
MQTT Discovery does not need to be enabled for this native integration.

### HACS catalogue status

This repository is currently **not included in the default HACS catalogue**.
Anyone can install it through the HACS button or custom-repository steps above;
it becomes searchable in their HACS after adding it. Passing the HACS validation
action does not automatically add a repository to the default catalogue.
Default inclusion requires a reviewed and merged contribution to
[`hacs/default`](https://github.com/hacs/default), following the
[HACS inclusion requirements](https://www.hacs.xyz/docs/publish/include/).

### Existing manual installations

Do not delete your integration, devices or entities. Add this repository in
HACS, download it over the existing `custom_components/open_fume_extractor`
folder, then restart Home Assistant. The integration domain and entity unique
IDs are unchanged, so existing setup, automations and dashboard references can
be reused. Back up your Home Assistant configuration before updating.

### Existing `main` installations

If the update entity shows commit IDs such as `9c23e65` rather than a release
version, HACS is likely tracking the development branch. A README-only commit
can then appear as an update even though the integration code is unchanged.

In HACS, open this repository and select **three-dot menu > Update information**,
then **Redownload > Need a different version?**. Enable pre-release/beta versions
if needed and choose **v0.1.28rc1** (or a newer numbered release). Download it,
restart Home Assistant and fully reload the browser/app. Do not remove the
integration, devices or entities.

`hide_default_branch` prevents offering `main` in the normal download selector;
it does **not** automatically migrate an existing branch installation or change
your saved HACS pre-release preference. Release tracking must be selected once
in HACS. RC releases remain pre-releases; this does not mark them as stable.

## Manual installation

Download the integration ZIP from
[Releases](https://github.com/IceCube20/Open-Fume-Extractor-Home-Assistant/releases).
Extract it and place its `open_fume_extractor` folder at:

```text
/config/custom_components/open_fume_extractor/
```

Restart Home Assistant and add the integration through **Devices & services**.
Do not create a second nested `custom_components/custom_components` directory.

## Status update interval

The integration options accept **0.5 to 60 seconds**, including fractional values.
The default stays at **5 seconds**; existing settings are preserved.
An interval of 0.5 seconds polls twice per second and increases master/network
load. It does not change the module polling rate or guarantee delivery of every
short input/output pulse.

## Dashboard cards

The integration includes **OFE Overview**, **OFE Master**, **OFE Module** and
alarm cards, with module controls and animated OFE/EVT indicators.

From **0.1.0rc25**, the integration automatically registers the bundled
**JavaScript module** when Lovelace resources use normal UI/storage mode.
Existing OFE resource entries are updated to the new version and duplicates
of that exact local bundle are removed. No dashboard layouts or entity IDs
are changed. No separate frontend repository or card download is needed.

After installation or an update, **restart Home Assistant and fully reload the
browser/app**. Then edit a dashboard and choose an OFE card. Already open pages
cannot replace registered JavaScript custom elements without a full reload.

For YAML-managed resources, add this **once** yourself (your YAML is never edited):

```yaml
lovelace:
  resource_mode: yaml
  resources:
    - url: /open_fume_extractor/ofe-cards.js
      type: module
```

Use the unversioned URL there and fully reload the browser after updates.
If automatic registration fails, a manual storage-mode fallback is available
under **Settings > Dashboards > Resources** (Advanced Mode): add/update
`/open_fume_extractor/ofe-cards.js?v=0.1.28rc1` as a JavaScript module, not a second
duplicate entry. Check HA logs for the underlying resource-registration error.

## Documentation and support

- [Entity catalogue, controls and card examples](docs/REFERENCE.md)
- [Release notes](CHANGELOG.md)
- [Report integration issues](https://github.com/IceCube20/Open-Fume-Extractor-Home-Assistant/issues)
- [Firmware, hardware and installation](https://github.com/IceCube20/Open-Fume-Extractor)
- [Official HACS custom-repository instructions](https://www.hacs.xyz/docs/faq/custom_repositories/)

Use a trusted local network. The master currently uses HTTP Basic authentication
without HTTPS: credentials and traffic are **not encrypted**. Do not expose the
master directly to the internet. Protect Home Assistant configuration/backups,
which contain the saved web login. Diagnostics omit credentials and tokens.

## Development

```sh
python -m pip install -r requirements-test.txt
python -m unittest discover -s tests -v
python tools/build_package.py
```

The unit tests use lightweight Home Assistant stubs and do not replace testing
in a running HA installation. Cross-firmware MQTT/API contract tests remain in
the main firmware repository. Integration runtime code is self-contained under
`custom_components/open_fume_extractor/`.

Card browser checks run the real OFE bundle with a small HA host/controls stub
in German/English, light/dark themes and mobile/desktop sizes. They verify
layout, live updates, focus retention, more-info actions and alarm states; they
do not replace testing inside Home Assistant. To run them locally:

```sh
npm install --no-save --ignore-scripts playwright @mdi/js@7.4.47
npx playwright install chromium
OFE_MDI_MODULE=node_modules/@mdi/js/mdi.js node tools/test_cards.cjs
```

Tags must match the integration manifest version, for example `v0.1.28rc1`.
The human-readable release title is **0.1.28RC**; machine-readable versions retain
the RC number so Home Assistant and HACS can compare updates reliably.
CI tests the component and HACS layout before publishing a manual-install ZIP.
No firmware binaries, credentials or signing keys belong in this repository.

Licensed under GPL-3.0; see [LICENSE](LICENSE). Open Fume Extractor is a community
project by IceCube20 and is not an official JBC or Weller product.
