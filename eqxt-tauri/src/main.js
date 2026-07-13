const { invoke } = window.__TAURI__.core;

const SAMPLE_RATE = 48000;
const NUM_POINTS = 200;
const DB_RANGE = 24;

const SAMPLE = `Preamp: -6.5 dB
Filter 1: ON PK Fc 1000 Hz Gain -3 dB Q 1.41
Filter 2: ON HP Fc 80 Hz
Filter 3: ON LSC Fc 100 Hz Gain 3 dB Q 0.7
GraphicEQ: 25 -3; 40 -2; 63 0; 100 2; 160 1; 250 0
Copy: L=R R=L
# 내 프로필`;

let lines = [];
let lastResp = [];

const $ = (s) => document.querySelector(s);
function status(msg) { $("#status").textContent = msg; }
function escapeHtml(s) { return String(s).replace(/[&<>]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;" }[c])); }

function sparkline(points) {
  if (!points || !points.length) return "";
  const w = 96, h = 22;
  const gains = points.map((p) => p[1]);
  const min = Math.min(-6, ...gains), max = Math.max(6, ...gains);
  const n = points.length;
  const pts = points.map((p, i) => {
    const x = (n > 1 ? i / (n - 1) : 0) * w;
    const y = h - ((p[1] - min) / (max - min || 1)) * h;
    return `${x.toFixed(1)},${y.toFixed(1)}`;
  }).join(" ");
  return `<svg width="${w}" height="${h}" viewBox="0 0 ${w} ${h}" preserveAspectRatio="none"><polyline points="${pts}" fill="none" stroke="currentColor" stroke-width="1.5"/></svg>`;
}

function cardFor(line) {
  const el = document.createElement("div");
  el.className = "card card-" + line.kind.toLowerCase();
  let body = "";
  switch (line.kind) {
    case "Preamp":
      body = `<span class="tag">Preamp</span><span class="val"><b>${line.gain_db}</b> dB</span>`;
      break;
    case "Biquad": {
      const parts = [`Fc <b>${line.freq_hz}</b> Hz`];
      if (line.gain_db != null) parts.push(`Gain <b>${line.gain_db}</b> dB`);
      if (line.q != null) parts.push(`Q <b>${line.q}</b>`);
      if (line.bw_oct != null) parts.push(`BW <b>${line.bw_oct}</b>`);
      const idx = line.index != null ? `#${line.index}` : "";
      body = `<span class="tag">Filter ${idx}</span>`
        + `<span class="dot ${line.enabled ? "on" : "off"}" title="${line.enabled ? "ON" : "OFF"}"></span>`
        + `<span class="chip">${escapeHtml(line.ftype)}</span>`
        + `<span class="val">${parts.join(" · ")}</span>`;
      break;
    }
    case "GraphicEq":
      body = `<span class="tag">GraphicEQ</span><span class="val">${line.points.length} points</span>`
        + `<span class="spark">${sparkline(line.points)}</span>`;
      break;
    case "Comment":
      body = `<span class="val comment"># ${escapeHtml(line.text)}</span>`;
      break;
    case "Blank":
      el.classList.add("blank");
      body = `<span class="val muted">·</span>`;
      break;
    case "Raw":
      body = `<span class="tag raw">raw</span><span class="val mono">${escapeHtml(line.text)}</span>`;
      break;
    default:
      body = `<span class="val mono">${escapeHtml(JSON.stringify(line))}</span>`;
  }
  el.innerHTML = body;
  return el;
}

function renderCards() {
  const list = $("#filter-list");
  list.innerHTML = "";
  for (const line of lines) list.appendChild(cardFor(line));
  $("#filter-count").textContent = lines.filter((l) => l.kind !== "Blank").length;
}

function drawGraph(resp) {
  const canvas = $("#graph");
  if (!canvas) return;
  const dpr = window.devicePixelRatio || 1;
  const rect = canvas.getBoundingClientRect();
  if (rect.width === 0 || rect.height === 0) return;
  canvas.width = Math.round(rect.width * dpr);
  canvas.height = Math.round(rect.height * dpr);
  const ctx = canvas.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  const W = rect.width, H = rect.height;
  ctx.clearRect(0, 0, W, H);

  const fmin = 20, fmax = SAMPLE_RATE / 2;
  const xOf = (f) => (Math.log10(f / fmin) / Math.log10(fmax / fmin)) * W;
  const yOf = (db) => H / 2 - (db / DB_RANGE) * (H / 2);

  ctx.font = "10px 'Cascadia Code', monospace";
  ctx.textBaseline = "alphabetic";

  // vertical grid (frequencies)
  ctx.strokeStyle = "#232b35";
  ctx.fillStyle = "#6b7480";
  ctx.lineWidth = 1;
  for (const f of [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]) {
    if (f > fmax) continue;
    const px = xOf(f);
    ctx.beginPath(); ctx.moveTo(px, 0); ctx.lineTo(px, H); ctx.stroke();
    ctx.fillText(f >= 1000 ? f / 1000 + "k" : "" + f, px + 3, H - 4);
  }
  // horizontal grid (dB)
  for (const db of [-24, -12, 12, 24]) {
    const py = yOf(db);
    ctx.beginPath(); ctx.moveTo(0, py); ctx.lineTo(W, py); ctx.stroke();
    ctx.fillText(db + "", 3, py - 2);
  }
  // 0 dB baseline
  ctx.strokeStyle = "#3a4553";
  ctx.beginPath(); ctx.moveTo(0, yOf(0)); ctx.lineTo(W, yOf(0)); ctx.stroke();

  if (!resp || !resp.length) return;

  // response curve
  ctx.strokeStyle = "#3ad6c5";
  ctx.lineWidth = 2;
  ctx.beginPath();
  resp.forEach((p, i) => {
    const px = xOf(p[0]);
    const py = yOf(Math.max(-DB_RANGE, Math.min(DB_RANGE, p[1])));
    if (i) ctx.lineTo(px, py); else ctx.moveTo(px, py);
  });
  ctx.stroke();
}

async function updateGraph() {
  try {
    lastResp = await invoke("frequency_response", { lines, sampleRate: SAMPLE_RATE, numPoints: NUM_POINTS });
    drawGraph(lastResp);
    $("#graph-info").textContent = `${SAMPLE_RATE / 1000}kHz · ${lastResp.length}pt`;
  } catch (e) {
    $("#graph-info").textContent = "(계산 오류)";
    status("응답 계산 오류: " + e);
  }
}

async function parse() {
  const text = $("#config-input").value;
  try {
    lines = await invoke("load_config_text", { text });
    renderCards();
    await updateGraph();
    status(`파싱됨 · ${lines.length}줄 · 필터 ${lines.filter((l) => l.kind === "Biquad").length}개`);
  } catch (e) {
    status("파싱 오류: " + e);
  }
}

async function serialize() {
  if (!lines.length) { status("먼저 파싱하세요"); return; }
  try {
    const text = await invoke("save_config_text", { lines });
    $("#config-input").value = text;
    status("직렬화됨 (round-trip 확인용)");
  } catch (e) {
    status("직렬화 오류: " + e);
  }
}

window.addEventListener("DOMContentLoaded", () => {
  $("#btn-sample").addEventListener("click", () => {
    $("#config-input").value = SAMPLE;
    status("샘플 로드됨 — '파싱 →'을 누르세요");
  });
  $("#btn-parse").addEventListener("click", parse);
  $("#btn-serialize").addEventListener("click", serialize);
});

window.addEventListener("resize", () => drawGraph(lastResp));
