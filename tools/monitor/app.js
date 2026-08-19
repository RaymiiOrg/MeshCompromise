const MAX_VISIBLE_LINES = 2000;
const TRIM_BATCH = 500;

const state = {
  port: null,
  reader: null,
  readableClosed: null,
  writer: null,
  busy: false,
  lines: [],
  pending: "",
};

const els = {};

function $(id) {
  return document.getElementById(id);
}

function setStatus(text, kind) {
  els.status.textContent = text;
  els.status.className = "status" + (kind ? " " + kind : "");
}

function refreshButtons() {
  const connected = state.port !== null;
  els.connect.disabled = state.busy || connected;
  els.disconnect.disabled = state.busy || !connected;
  els.sendline.disabled = !connected;
  els.send.disabled = !connected;
}

function escapeHtml(text) {
  return text.replace(/[&<>]/g, (ch) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;" })[ch]);
}

const ANSI_ESCAPE_RE = /\x1b\[([0-9;]*)m/g;
const ANSI_FOREGROUND = {
  30: "#5c6675",
  31: "#ff6b6b",
  32: "#3ddc84",
  33: "#ffd166",
  34: "#6fb3ff",
  35: "#d891ff",
  36: "#5eead4",
  37: "#e8eef6",
  90: "#6b7484",
  91: "#ff8787",
  92: "#79f2a6",
  93: "#ffe28a",
  94: "#9cc9ff",
  95: "#e5b3ff",
  96: "#8ff0e0",
  97: "#ffffff",
};

function ansiToSegments(line) {
  const segments = [];
  let lastIndex = 0;
  let fg = null;
  let bold = false;
  let match;

  ANSI_ESCAPE_RE.lastIndex = 0;
  while ((match = ANSI_ESCAPE_RE.exec(line))) {
    if (match.index > lastIndex) segments.push({ text: line.slice(lastIndex, match.index), fg, bold });

    const codes = match[1].length ? match[1].split(";").map(Number) : [0];
    for (const code of codes) {
      if (code === 0) {
        fg = null;
        bold = false;
      } else if (code === 1) {
        bold = true;
      } else if (code === 22) {
        bold = false;
      } else if (code === 39) {
        fg = null;
      } else if (ANSI_FOREGROUND[code] !== undefined) {
        fg = ANSI_FOREGROUND[code];
      }
    }
    lastIndex = ANSI_ESCAPE_RE.lastIndex;
  }
  if (lastIndex < line.length) segments.push({ text: line.slice(lastIndex), fg, bold });
  return segments;
}

function highlightedEscapedText(text, needle) {
  if (!needle) return escapeHtml(text);

  const lower = text.toLowerCase();
  const lowerNeedle = needle.toLowerCase();
  if (!lower.includes(lowerNeedle)) return escapeHtml(text);

  let result = "";
  let index = 0;
  while (index < text.length) {
    const at = lower.indexOf(lowerNeedle, index);
    if (at === -1) {
      result += escapeHtml(text.slice(index));
      break;
    }
    result += escapeHtml(text.slice(index, at));
    result += '<span class="hit">' + escapeHtml(text.slice(at, at + needle.length)) + "</span>";
    index = at + needle.length;
  }
  return result;
}

function renderLine(line) {
  const needle = els.filter.value;

  return ansiToSegments(line)
    .map((segment) => {
      const html = highlightedEscapedText(segment.text, needle);
      if (!segment.fg && !segment.bold) return html;
      const style = (segment.fg ? "color:" + segment.fg + ";" : "") + (segment.bold ? "font-weight:700;" : "");
      return '<span style="' + style + '">' + html + "</span>";
    })
    .join("");
}

function scrollToBottom() {
  els.log.scrollTop = els.log.scrollHeight;
}

function visibleSlice() {
  return state.lines.length > MAX_VISIBLE_LINES ? state.lines.slice(-MAX_VISIBLE_LINES) : state.lines;
}

function updateStats() {
  const shown = Math.min(state.lines.length, MAX_VISIBLE_LINES);
  els.stats.textContent =
    shown < state.lines.length ? shown + " / " + state.lines.length + " lines" : state.lines.length + " lines";
}

function renderAll() {
  const preserved = els.autoscroll.checked ? null : els.log.scrollTop;

  els.log.innerHTML = visibleSlice().map(renderLine).join("\n");
  updateStats();

  if (preserved === null) scrollToBottom();
  else els.log.scrollTop = preserved;
}

function appendLine(line) {
  state.lines.push(line);

  if (state.lines.length > MAX_VISIBLE_LINES + TRIM_BATCH) {
    els.log.innerHTML = visibleSlice().map(renderLine).join("\n");
  } else {
    const rendered = renderLine(line);
    els.log.insertAdjacentHTML("beforeend", (els.log.textContent.length ? "\n" : "") + rendered);
  }

  updateStats();
  if (els.autoscroll.checked) scrollToBottom();
}

async function pumpReadLoop() {
  const decoder = new TextDecoderStream();
  state.readableClosed = state.port.readable.pipeTo(decoder.writable);
  state.reader = decoder.readable.getReader();

  try {
    for (;;) {
      const { value, done } = await state.reader.read();
      if (done) break;
      if (!value) continue;

      state.pending += value;
      const parts = state.pending.split("\n");
      state.pending = parts.pop();
      for (const part of parts) appendLine(part.replace(/\r$/, ""));
    }
  } catch (error) {
    setStatus("read error", "err");
    appendLine("[monitor] read error: " + (error && error.message ? error.message : error));
  }
}

async function connect() {
  if (!navigator.serial) {
    setStatus("Web Serial unavailable", "err");
    appendLine("[monitor] This browser has no Web Serial API. Use Chrome or Edge 89 or later, served over http://localhost.");
    return;
  }

  state.busy = true;
  refreshButtons();
  setStatus("connecting");

  try {
    const port = await navigator.serial.requestPort();
    await port.open({ baudRate: Number(els.baud.value) });
    state.port = port;

    if (port.writable) state.writer = port.writable.getWriter();

    setStatus("connected", "ok");
    appendLine("[monitor] Connected at " + els.baud.value + " baud.");
    pumpReadLoop();
  } catch (error) {
    setStatus("not connected", "err");
    appendLine("[monitor] Connect failed: " + (error && error.message ? error.message : error));
    state.port = null;
  } finally {
    state.busy = false;
    refreshButtons();
  }
}

async function disconnect() {
  state.busy = true;
  refreshButtons();

  try {
    if (state.reader) {
      await state.reader.cancel();
      state.reader.releaseLock();
    }
    if (state.readableClosed) await state.readableClosed.catch(() => {});
    if (state.writer) {
      state.writer.releaseLock();
      state.writer = null;
    }
    if (state.port) await state.port.close();
  } catch (error) {
    appendLine("[monitor] Disconnect warning: " + (error && error.message ? error.message : error));
  }

  state.port = null;
  state.reader = null;
  state.readableClosed = null;
  setStatus("not connected");
  state.busy = false;
  refreshButtons();
}

async function sendLine() {
  const text = els.sendline.value;
  if (!text || !state.writer) return;

  try {
    await state.writer.write(new TextEncoder().encode(text + "\n"));
    els.sendline.value = "";
  } catch (error) {
    appendLine("[monitor] Send failed: " + (error && error.message ? error.message : error));
  }
}

function clearLog() {
  state.lines = [];
  state.pending = "";
  els.log.innerHTML = "";
  els.stats.textContent = "0 lines";
}

function stripAnsi(text) {
  ANSI_ESCAPE_RE.lastIndex = 0;
  return text.replace(ANSI_ESCAPE_RE, "");
}

function logText() {
  return stripAnsi(state.lines.join("\n") + (state.pending ? "\n" + state.pending : ""));
}

function flashLabel(button, message) {
  if (button.dataset.label === undefined) button.dataset.label = button.textContent;
  button.textContent = message;
  clearTimeout(Number(button.dataset.timer));
  button.dataset.timer = String(setTimeout(() => (button.textContent = button.dataset.label), 1500));
}

function selectAllLog() {
  const range = document.createRange();
  range.selectNodeContents(els.log);
  const selection = window.getSelection();
  selection.removeAllRanges();
  selection.addRange(range);
  flashLabel(els.selectall, "Selected");
}

async function copyLog() {
  const text = logText();
  if (text.length === 0) {
    flashLabel(els.copy, "Nothing to copy");
    return;
  }

  try {
    await navigator.clipboard.writeText(text);
    flashLabel(els.copy, `Copied ${state.lines.length} lines`);
  } catch (error) {
    selectAllLog();
    flashLabel(els.copy, "Blocked - press Ctrl+C");
  }
}

function downloadLog() {
  const blob = new Blob([logText()], { type: "text/plain" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  const stamp = new Date().toISOString().replace(/[:.]/g, "-");
  a.href = url;
  a.download = "meshcompromise-serial-" + stamp + ".log";
  document.body.appendChild(a);
  a.click();
  a.remove();
  URL.revokeObjectURL(url);
}

function init() {
  for (const id of ["connect", "disconnect", "baud", "autoscroll", "status", "log", "filter", "stats", "clear", "download",
                    "selectall", "copy", "sendline", "send"]) {
    els[id] = $(id);
  }

  els.connect.addEventListener("click", connect);
  els.disconnect.addEventListener("click", disconnect);
  els.clear.addEventListener("click", clearLog);
  els.autoscroll.addEventListener("change", () => {
    if (els.autoscroll.checked) scrollToBottom();
  });
  els.download.addEventListener("click", downloadLog);
  els.selectall.addEventListener("click", selectAllLog);
  els.copy.addEventListener("click", copyLog);
  els.filter.addEventListener("input", renderAll);
  els.send.addEventListener("click", sendLine);
  els.sendline.addEventListener("keydown", (event) => {
    if (event.key === "Enter") sendLine();
  });

  window.addEventListener("beforeunload", () => {
    if (state.port) disconnect();
  });

  setStatus("not connected");
  refreshButtons();

  if (!navigator.serial) {
    appendLine("[monitor] Web Serial is not available in this browser. Chrome or Edge 89+ is required.");
  }
  if (location.protocol === "file:") {
    appendLine("[monitor] Opened from file://. Web Serial needs a served origin - run a local server and use http://localhost.");
  }
}

document.addEventListener("DOMContentLoaded", init);
