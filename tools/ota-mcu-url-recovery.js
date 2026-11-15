// Run once in the console of the affected MCU's open /update page, only after
// a failed upload. No firmware is committed; the pending upload is cancelled.
(async () => {
  const previous = window.fetch;
  window.fetch = function (url, options) {
    const fixed = typeof url === "string"
      ? url.replace(/^\/update\/MCU\/(end|abort)(?=\?|$)/, "/update/master/$1")
      : url;
    return previous.call(window, fixed, options);
  };
  try {
    const response = await window.fetch("/update/master/abort", { method: "POST" });
    const result = await response.text();
    if (!response.ok) throw new Error(`HTTP ${response.status}: ${result}`);
    console.log("OTA-URLs korrigiert. Update jetzt erneut starten; Seite vorher nicht neu laden.");
  } catch (error) {
    window.fetch = previous;
    throw error;
  }
})();
