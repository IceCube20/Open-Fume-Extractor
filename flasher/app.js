const catalogUrl = "flasher/firmware-catalog.json";
const select = document.querySelector("#target-select");
const installer = document.querySelector("#install-button");
const status = document.querySelector("#catalog-status");
const fields = {
  version: document.querySelector("#firmware-version"),
  chip: document.querySelector("#chip-family"),
  flash: document.querySelector("#flash-size"),
  target: document.querySelector("#signature-target"),
  sha: document.querySelector("#merged-sha"),
  merged: document.querySelector("#merged-download"),
  ota: document.querySelector("#ota-download"),
};

let catalog = [];

function setTarget(id, updateUrl = true) {
  const firmware = catalog.find((entry) => entry.id === id) ?? catalog[0];
  if (!firmware) return;

  select.value = firmware.id;
  fields.version.textContent = firmware.version;
  fields.chip.textContent = firmware.chipFamily;
  fields.flash.textContent = firmware.flashSize;
  fields.target.textContent = firmware.signatureTarget;
  fields.sha.textContent = firmware.mergedSha256;
  fields.merged.href = firmware.merged;
  fields.ota.href = firmware.ota;
  installer.setAttribute("manifest", firmware.manifest);

  if (updateUrl) {
    const url = new URL(window.location.href);
    url.searchParams.set("target", firmware.id);
    window.history.replaceState({}, "", url);
  }
}

async function loadCatalog() {
  try {
    const response = await fetch(catalogUrl, { cache: "no-store" });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    catalog = await response.json();
    if (!Array.isArray(catalog) || catalog.length === 0) throw new Error("Catalog is empty");

    select.replaceChildren();
    for (const firmware of catalog) {
      const option = document.createElement("option");
      option.value = firmware.id;
      option.textContent = `${firmware.label} - ${firmware.version}`;
      select.append(option);
    }
    select.disabled = false;
    select.addEventListener("change", () => setTarget(select.value));

    const requestedTarget = new URL(window.location.href).searchParams.get("target");
    const initialTarget = catalog.some((entry) => entry.id === requestedTarget)
      ? requestedTarget
      : catalog[0].id;
    setTarget(initialTarget, false);
    status.textContent = `${catalog.length} verified firmware targets available`;
    status.className = "status ready";
  } catch (error) {
    console.error("Unable to load firmware catalog", error);
    status.textContent = "Firmware catalog unavailable";
    status.className = "status error";
    select.innerHTML = "<option>Unable to load firmware</option>";
  }
}

loadCatalog();
