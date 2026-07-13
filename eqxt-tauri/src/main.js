import { drawUnit } from "./rack-chrome.js";
import { drawKnob, drawScope } from "./rack-controls.js";

const { invoke } = window.__TAURI__.core;

const SAMPLE = `Preamp: -6.5 dB
Filter 1: ON PK Fc 1000 Hz Gain -3 dB Q 1.41
Filter 2: ON HP Fc 80 Hz
Filter 3: ON LSC Fc 100 Hz Gain 3 dB Q 0.7
GraphicEQ: 25 -3; 40 -2; 63 0; 100 2; 160 1; 250 0
Copy: L=R R=L
# 내 프로필`;

let lines = [];

const $ = (s) => document.querySelector(s);
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
function status(m) { $("#status").textContent = m; }
function isDark() { return !document.body.classList.contains("cream"); }

function tokens() {
  const cs = getComputedStyle(document.body);
  const g = (n) => cs.getPropertyValue(n).trim();
  return {
    card: g("--card"), accent: g("--accent"), accent2: g("--accent2"),
    text: g("--text"), mutedText: g("--muted"), focusRing: g("--focus"),
    danger: g("--danger"), graphGridMinor: g("--grid-minor"),
    monoFontFamily: "DM Mono, monospace",
  };
}

const KIND_TYPE = { Preamp: "preamp", Biquad: "biquad", GraphicEq: "graphiceq", Comment: "comment", Raw: "text", Blank: "spacer" };
const LABELS = { preamp: "PREAMP", biquad: "FILTER", graphiceq: "GRAPHIC", comment: "NOTE", text: "AUX" };

function unitHeight(line) {
  switch (line.kind) {
    case "GraphicEq": return 100;
    case "Comment": case "Raw": return 42;
    default: return 76; // Preamp, Biquad
  }
}

function setupCanvas(canvas, w, h) {
  const dpr = window.devicePixelRatio || 1;
  canvas.width = Math.round(w * dpr);
  canvas.height = Math.round(h * dpr);
  canvas.style.height = h + "px";
  const ctx = canvas.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, h);
  return ctx;
}

// A dark LCD well with green phosphor segment text.
function lcd(ctx, x, y, text, tk, align = "left") {
  ctx.save();
  ctx.font = '11px "DM Mono", monospace';
  ctx.textBaseline = "middle";
  ctx.textAlign = align;
  ctx.shadowColor = tk.accent2;
  ctx.shadowBlur = 5;
  ctx.fillStyle = tk.accent2;
  ctx.fillText(text, x, y);
  ctx.restore();
}

function fmtFreq(f) { return f >= 1000 ? (f / 1000) + "k" : "" + f; }

function renderUnit(line) {
  const canvas = document.createElement("canvas");
  canvas.className = "rack-unit";
  $("#rack").appendChild(canvas);
  const w = canvas.clientWidth || $("#rack").clientWidth;
  const h = unitHeight(line);
  const ctx = setupCanvas(canvas, w, h);
  const tk = tokens();
  const dark = isDark();
  const type = KIND_TYPE[line.kind] || "text";
  const label = LABELS[type] || "AUX";
  const enabled = line.kind === "Comment" ? true : (line.enabled !== false);

  drawUnit(ctx, 0, 0, w, h, {
    dark, type, enabled, selected: false, focused: false,
    command: JSON.stringify(line).slice(0, 24), label, tokens: tk,
    rowHeight: 36, borderRadius: 3,
  });

  const ctrlLeft = 30;   // clear the left ear + status LEDs
  const ctrlRight = w - 26;

  if (line.kind === "Preamp") {
    drawKnob(ctx, ctrlLeft, 6, 64, h - 12, { ratio: clamp((line.gain_db + 24) / 48, 0, 1), bipolar: true, enabled: true }, tk);
    lcd(ctx, ctrlLeft + 72, h / 2, `${line.gain_db} dB`, tk);
  } else if (line.kind === "Biquad") {
    const g = line.gain_db != null ? line.gain_db : 0;
    drawKnob(ctx, ctrlLeft, 6, 64, h - 12, { ratio: clamp((g + 24) / 48, 0, 1), bipolar: line.gain_db != null, enabled }, tk);
    lcd(ctx, ctrlLeft + 74, h / 2 - 9, `${line.ftype}  Fc ${fmtFreq(line.freq_hz)}Hz`, tk);
    const parts = [];
    if (line.gain_db != null) parts.push(`G ${line.gain_db}dB`);
    if (line.q != null) parts.push(`Q ${line.q}`);
    if (parts.length) lcd(ctx, ctrlLeft + 74, h / 2 + 9, parts.join("   "), tk);
  } else if (line.kind === "GraphicEq") {
    renderGeq(ctx, line, ctrlLeft, 8, ctrlRight - ctrlLeft, h - 16, tk, dark);
  } else if (line.kind === "Comment") {
    lcd(ctx, ctrlLeft, h / 2, "# " + line.text, tk);
  } else if (line.kind === "Raw") {
    lcd(ctx, ctrlLeft, h / 2, line.text, tk);
  }
}

function renderGeq(ctx, line, x, y, w, h, tk, dark) {
  const pts = line.points || [];
  const fmin = 20, fmax = 24000, dbRange = 12;
  const xOf = (f) => x + (Math.log10(clamp(f, fmin, fmax) / fmin) / Math.log10(fmax / fmin)) * w;
  const yOf = (db) => y + h / 2 - (clamp(db, -dbRange, dbRange) / dbRange) * (h / 2);
  const curve = pts.map((p) => [xOf(p[0]), yOf(p[1])]);
  const plot = { x, y, w, h };
  drawScope(ctx, plot, curve, tk, { dark, powered: line.enabled !== false, plotRect: plot, zeroY: yOf(0), vertical: [], horizontal: [] });
}

async function renderMonitor() {
  const canvas = $("#monitor");
  const w = canvas.clientWidth, h = canvas.clientHeight;
  const ctx = setupCanvas(canvas, w, h);
  const tk = tokens();
  const dark = isDark();
  const plot = { x: 36, y: 10, w: w - 46, h: h - 30 };
  const fmin = 20, fmax = 24000, dbRange = 24;
  const xOf = (f) => plot.x + (Math.log10(clamp(f, fmin, fmax) / fmin) / Math.log10(fmax / fmin)) * plot.w;
  const yOf = (db) => plot.y + plot.h / 2 - (clamp(db, -dbRange, dbRange) / dbRange) * (plot.h / 2);

  let resp = [];
  try { resp = await invoke("frequency_response", { lines, sampleRate: 48000, numPoints: 220 }); }
  catch (e) { status("응답 계산 오류: " + e); }

  const curve = resp.map((p) => [xOf(p[0]), yOf(p[1])]);
  const zeroY = yOf(0);
  const vertical = [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]
    .filter((f) => f <= fmax)
    .map((f) => ({ pos: xOf(f), major: f === 100 || f === 1000 || f === 10000, label: fmtFreq(f) }));
  const horizontal = [-24, -12, 0, 12, 24].map((db) => ({ pos: yOf(db), major: db === 0, label: "" + db }));

  drawScope(ctx, { x: 0, y: 0, w, h }, curve, tk, { dark, powered: true, plotRect: plot, zeroY, vertical, horizontal });
}

async function parseAndRender() {
  const text = $("#config-input").value;
  try {
    lines = await invoke("load_config_text", { text });
  } catch (e) { status("파싱 오류: " + e); return; }
  $("#rack").innerHTML = "";
  for (const line of lines) {
    if (line.kind === "Blank") continue;
    renderUnit(line);
  }
  await renderMonitor();
  const units = lines.filter((l) => l.kind !== "Blank").length;
  status(`장착 유닛 ${units}대 · 필터 ${lines.filter((l) => l.kind === "Biquad").length}대`);
}

function rerender() {
  if (!lines.length) return;
  $("#rack").innerHTML = "";
  for (const line of lines) { if (line.kind !== "Blank") renderUnit(line); }
  renderMonitor();
}

window.addEventListener("DOMContentLoaded", () => {
  $("#btn-sample").addEventListener("click", () => {
    $("#config-input").value = SAMPLE;
    status("샘플 장착됨 — PARSE");
  });
  $("#btn-parse").addEventListener("click", parseAndRender);
  $("#btn-finish").addEventListener("click", () => {
    document.body.classList.toggle("cream");
    rerender();
  });
});

window.addEventListener("resize", rerender);
