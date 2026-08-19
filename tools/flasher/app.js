import { ESPLoader, Transport } from "./vendor/esptool-js.js";

const OFFSET_RULES = [
  { match: /factory\.bin$/i, offset: 0x0 },
  { match: /bootloader.*\.bin$/i, offset: 0x0 },
  { match: /partitions?\.bin$/i, offset: 0x8000 },
  { match: /\.bin$/i, offset: 0x10000 },
];

const state = {
  files: [],
  transport: null,
  loader: null,
  chip: null,
  busy: false,
};

const els = {};

function $(id) {
  return document.getElementById(id);
}

function log(message) {
  els.log.textContent += message;
  els.log.scrollTop = els.log.scrollHeight;
}

function logLine(message) {
  log(message.endsWith("\n") ? message : message + "\n");
}

function setStatus(text, kind) {
  els.status.textContent = text;
  els.status.className = "status" + (kind ? " " + kind : "");
}

function formatBytes(size) {
  if (size < 1024) return size + " B";
  if (size < 1024 * 1024) return (size / 1024).toFixed(1) + " KiB";
  return (size / (1024 * 1024)).toFixed(2) + " MiB";
}

function guessOffset(name) {
  for (const rule of OFFSET_RULES) {
    if (rule.match.test(name)) return rule.offset;
  }
  return 0x10000;
}

function parseOffset(text) {
  const trimmed = text.trim();
  if (!/^(0x)?[0-9a-f]+$/i.test(trimmed)) return null;
  return parseInt(trimmed, 16);
}

function renderFiles() {
  els.files.textContent = "";

  for (const entry of state.files) {
    const item = document.createElement("li");
    item.className = "file";

    const name = document.createElement("span");
    name.className = "name";
    name.textContent = entry.name;

    const size = document.createElement("span");
    size.className = "size";
    size.textContent = formatBytes(entry.data.length);

    const offset = document.createElement("input");
    offset.type = "text";
    offset.value = "0x" + entry.address.toString(16).toUpperCase();
    offset.setAttribute("aria-label", "Flash offset for " + entry.name);
    offset.addEventListener("change", () => {
      const parsed = parseOffset(offset.value);
      if (parsed === null) {
        offset.value = "0x" + entry.address.toString(16).toUpperCase();
        logLine("Ignored an invalid offset; expected hex such as 0x10000.");
        return;
      }
      entry.address = parsed;
      offset.value = "0x" + parsed.toString(16).toUpperCase();
    });

    const remove = document.createElement("button");
    remove.textContent = "Remove";
    remove.addEventListener("click", () => {
      state.files = state.files.filter((candidate) => candidate !== entry);
      renderFiles();
      refreshButtons();
    });

    const bar = document.createElement("div");
    bar.className = "bar";
    entry.fill = document.createElement("span");
    bar.appendChild(entry.fill);

    item.append(name, size, offset, remove, bar);
    els.files.appendChild(item);
  }
}

async function addEntry(file, handle) {
  const buffer = await file.arrayBuffer();
  state.files.push({
    name: file.name,
    data: new Uint8Array(buffer),
    address: guessOffset(file.name),
    lastModified: file.lastModified,
    handle: handle || null,
    fill: null,
  });
  const note = handle ? "" : " - snapshot, re-add it after a rebuild";
  logLine(`Added ${file.name} (${formatBytes(buffer.byteLength)}) at 0x${guessOffset(file.name).toString(16)}${note}`);
}

async function addFiles(fileList) {
  for (const file of fileList) await addEntry(file, null);
  renderFiles();
  refreshButtons();
}

async function pickFiles() {
  if (!window.showOpenFilePicker) {
    els.picker.click();
    return;
  }

  try {
    const handles = await window.showOpenFilePicker({
      multiple: true,
      types: [{ description: "Firmware images", accept: { "application/octet-stream": [".bin"] } }],
    });
    for (const handle of handles) await addEntry(await handle.getFile(), handle);
    renderFiles();
    refreshButtons();
  } catch (error) {
    if (error && error.name === "AbortError") return;
    logLine("File selection failed: " + (error && error.message ? error.message : error));
  }
}

async function refreshFromDisk() {
  let reloaded = 0;
  let snapshots = 0;

  for (const entry of state.files) {
    if (!entry.handle) {
      snapshots++;
      continue;
    }

    try {
      const file = await entry.handle.getFile();
      const buffer = await file.arrayBuffer();
      const next = new Uint8Array(buffer);
      if (file.lastModified !== entry.lastModified || next.length !== entry.data.length) {
        logLine(`Reloaded ${entry.name} (${formatBytes(next.length)}) from disk.`);
        reloaded++;
      }
      entry.data = next;
      entry.lastModified = file.lastModified;
    } catch (error) {
      logLine(`Could not re-read ${entry.name}: ${error && error.message ? error.message : error}`);
      logLine("Flashing the copy loaded earlier; remove and re-add it if the build has changed.");
    }
  }

  if (reloaded) renderFiles();
  if (snapshots)
    logLine(`${snapshots} file(s) came from drag-and-drop and cannot be re-read; remove and re-add them after a rebuild.`);
}

function refreshButtons() {
  const connected = state.loader !== null;
  els.connect.disabled = state.busy || connected;
  els.disconnect.disabled = state.busy || !connected;
  els.flash.disabled = state.busy || !connected || state.files.length === 0;
}

const terminal = {
  clean() {
    els.log.textContent = "";
  },
  writeLine(data) {
    logLine(data);
  },
  write(data) {
    log(data);
  },
};

async function connect() {
  if (!navigator.serial) {
    setStatus("Web Serial unavailable", "err");
    logLine("This browser has no Web Serial API. Use Chrome or Edge 89 or later, served over http://localhost.");
    return;
  }

  state.busy = true;
  refreshButtons();
  setStatus("connecting");

  try {
    const port = await navigator.serial.requestPort();
    state.transport = new Transport(port, true);
    state.loader = new ESPLoader({
      transport: state.transport,
      baudrate: Number(els.baud.value),
      terminal,
    });

    state.chip = await state.loader.main();
    setStatus(state.chip, "ok");
    logLine(`Connected to ${state.chip}`);

    if (!/esp32-?s3/i.test(state.chip)) {
      logLine(`Warning: expected an ESP32-S3 for the Cardputer, but found ${state.chip}. Check the board before flashing.`);
    }
  } catch (error) {
    await teardown();
    setStatus("not connected", "err");
    logLine("Connect failed: " + (error && error.message ? error.message : error));
    logLine("If no port appeared, check the cable, then hold G0 and tap reset to force download mode.");
  } finally {
    state.busy = false;
    refreshButtons();
  }
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

// esptool-js's own after("hard_reset") only releases RTS (sets it false) - it
// never asserts it first. That assumes the bootloader-entry handshake left
// RTS asserted true, but the classic entry sequence it also ships (ClassicReset)
// ends with RTS already false, so the "release" is a no-op: no edge ever
// reaches EN and the chip keeps running the old firmware until it's power
// cycled by hand. Pulse RTS true -> false ourselves so a real reset edge is
// guaranteed regardless of what state the handshake left it in.
async function pulseHardReset() {
  if (!state.transport) return;
  await state.transport.setDTR(false);
  await state.transport.setRTS(true);
  await sleep(120);
  await state.transport.setRTS(false);
}

async function teardown() {
  try {
    if (state.transport) await state.transport.disconnect();
  } catch (error) {
    logLine("Disconnect warning: " + (error && error.message ? error.message : error));
  }
  state.transport = null;
  state.loader = null;
  state.chip = null;
}

async function disconnect() {
  state.busy = true;
  refreshButtons();
  await teardown();
  setStatus("not connected");
  logLine("Disconnected.");
  state.busy = false;
  refreshButtons();
}

async function flash() {
  if (!state.loader || state.files.length === 0) return;

  const addresses = state.files.map((entry) => entry.address);
  if (new Set(addresses).size !== addresses.length) {
    logLine("Two files share the same offset. Fix the offsets before flashing.");
    return;
  }

  state.busy = true;
  refreshButtons();
  setStatus("flashing");

  try {
    await refreshFromDisk();

    for (const entry of state.files) {
      if (entry.fill) entry.fill.style.width = "0%";
    }

    await state.loader.writeFlash({
      fileArray: state.files.map((entry) => ({ data: entry.data, address: entry.address })),
      flashSize: "keep",
      flashMode: "keep",
      flashFreq: "keep",
      eraseAll: els.erase.checked,
      compress: true,
      reportProgress(index, written, total) {
        const entry = state.files[index];
        if (entry && entry.fill) {
          entry.fill.style.width = Math.round((written / total) * 100) + "%";
        }
      },
    });

    await pulseHardReset();
    logLine("Flash complete. The device should reboot into the new firmware.");
    await teardown();
    setStatus("done", "ok");
  } catch (error) {
    setStatus("failed", "err");
    logLine("Flash failed: " + (error && error.message ? error.message : error));
    logLine("Nothing was necessarily lost. Reconnect and retry; if it keeps failing, hold G0 and tap reset first.");
  } finally {
    state.busy = false;
    refreshButtons();
  }
}

function wireDropZone() {
  els.drop.addEventListener("click", pickFiles);

  els.drop.addEventListener("dragover", (event) => {
    event.preventDefault();
    els.drop.classList.add("over");
  });

  els.drop.addEventListener("dragleave", () => els.drop.classList.remove("over"));

  els.drop.addEventListener("drop", async (event) => {
    event.preventDefault();
    els.drop.classList.remove("over");
    if (event.dataTransfer && event.dataTransfer.files.length) {
      await addFiles(event.dataTransfer.files);
    }
  });

  els.picker.addEventListener("change", async () => {
    if (els.picker.files.length) await addFiles(els.picker.files);
    els.picker.value = "";
  });
}

function init() {
  for (const id of ["drop", "picker", "files", "connect", "disconnect", "flash", "baud", "erase", "status", "log"]) {
    els[id] = $(id);
  }

  wireDropZone();
  els.connect.addEventListener("click", connect);
  els.disconnect.addEventListener("click", disconnect);
  els.flash.addEventListener("click", flash);

  setStatus("not connected");
  refreshButtons();

  if (!navigator.serial) {
    logLine("Web Serial is not available in this browser. Chrome or Edge 89+ is required.");
  }
  if (location.protocol === "file:") {
    logLine("Opened from file://. Module imports and Web Serial need a served origin - run a local server and use http://localhost.");
  }
}

document.addEventListener("DOMContentLoaded", init);
