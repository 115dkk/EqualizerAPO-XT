// rack-chrome.js — Qt "rack" skin faceplate chrome translated to canvas 2D.
// Render translation of RackChrome::paintCardChrome + helpers (design finalized
// in Qt; this reproduces it, does not redesign).

export function drawUnit(ctx, x, y, w, h, opts) {
  const {
    dark = false,
    type = 'normal',
    enabled = true,
    selected = false,
    focused = false,
    command = '',
    label = '',
    tokens = {},
    rowHeight = 24,
    borderRadius = 4,
  } = opts;

  if (type === 'spacer') return;

  ctx.save();
  ctx.imageSmoothingEnabled = true;

  const r = {
    left: x + 1,
    top: y + 1,
    right: x + w - 1,
    bottom: y + h - 1,
  };
  r.width = r.right - r.left;
  r.height = r.bottom - r.top;

  const radius = Math.max(2, borderRadius - 1);
  const kEarWidth = 20;
  const kNameplateWidth = 78;
  const kNameplateHeight = 22;

  beginRoundedRectPath(ctx, r.left, r.top, r.width, r.height, radius);
  ctx.clip();

  // Brushed sheen
  const sheenGrad = ctx.createLinearGradient(r.left, r.top, r.left, r.bottom);
  if (dark) {
    sheenGrad.addColorStop(0, 'rgba(255,255,255,0.086)');
    sheenGrad.addColorStop(0.12, 'rgba(255,255,255,0.035)');
    sheenGrad.addColorStop(0.55, 'rgba(255,255,255,0)');
    sheenGrad.addColorStop(1, 'rgba(0,0,0,0.18)');
  } else {
    sheenGrad.addColorStop(0, 'rgba(255,255,255,0.43)');
    sheenGrad.addColorStop(0.5, 'rgba(255,255,255,0)');
    sheenGrad.addColorStop(1, 'rgba(0,0,0,0.10)');
  }
  ctx.fillStyle = sheenGrad;
  ctx.fillRect(r.left, r.top, r.width, r.height);

  // Per-type finish tint
  if (type === 'include') {
    ctx.fillStyle = dark ? 'rgba(0,0,0,0.25)' : 'rgba(0,0,0,0.11)';
    ctx.fillRect(r.left, r.top, r.width, r.height);
  } else if (type === 'vst') {
    ctx.fillStyle = dark ? 'rgba(34,20,6,0.20)' : 'rgba(74,50,14,0.07)';
    ctx.fillRect(r.left, r.top, r.width, r.height);
  }

  // Brushing texture
  paintBrushing(ctx, r, dark, hashString(command));

  // Rack ears + machined groove
  const leftEarX = r.left;
  const rightEarX = r.right - kEarWidth;
  const earFillAlpha = dark ? 0.20 : 0.08;
  ctx.fillStyle = `rgba(0,0,0,${earFillAlpha})`;
  ctx.fillRect(leftEarX, r.top, kEarWidth, r.height);
  ctx.fillRect(rightEarX, r.top, kEarWidth, r.height);

  ctx.strokeStyle = dark ? 'rgba(0,0,0,0.47)' : 'rgba(0,0,0,0.24)';
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(leftEarX + kEarWidth, r.top);
  ctx.lineTo(leftEarX + kEarWidth, r.bottom);
  ctx.stroke();
  ctx.beginPath();
  ctx.moveTo(rightEarX, r.top);
  ctx.lineTo(rightEarX, r.bottom);
  ctx.stroke();

  ctx.strokeStyle = dark ? 'rgba(255,255,255,0.10)' : 'rgba(255,255,255,0.47)';
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(leftEarX + kEarWidth + 1, r.top);
  ctx.lineTo(leftEarX + kEarWidth + 1, r.bottom);
  ctx.stroke();
  ctx.beginPath();
  ctx.moveTo(rightEarX + 1, r.top);
  ctx.lineTo(rightEarX + 1, r.bottom);
  ctx.stroke();

  // Seating seam + bezel chamfer
  ctx.strokeStyle = dark ? 'rgba(0,0,0,0.35)' : 'rgba(0,0,0,0.16)';
  ctx.lineWidth = 1;
  drawRoundedRect(ctx, r.left + 0.5, r.top + 0.5, r.width - 1, r.height - 1, radius);

  ctx.strokeStyle = dark ? 'rgba(255,255,255,0.14)' : 'rgba(255,255,255,0.59)';
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(r.left + radius, r.top + 1.5);
  ctx.lineTo(r.right - radius, r.top + 1.5);
  ctx.stroke();

  ctx.strokeStyle = dark ? 'rgba(255,255,255,0.063)' : 'rgba(255,255,255,0.31)';
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(r.left + 1.5, r.top + radius);
  ctx.lineTo(r.left + 1.5, r.bottom - radius);
  ctx.stroke();

  ctx.strokeStyle = dark ? 'rgba(0,0,0,0.55)' : 'rgba(0,0,0,0.27)';
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(r.left + radius, r.bottom - 1.5);
  ctx.lineTo(r.right - radius, r.bottom - 1.5);
  ctx.stroke();

  ctx.strokeStyle = dark ? 'rgba(0,0,0,0.24)' : 'rgba(0,0,0,0.12)';
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(r.right - 1.5, r.top + radius);
  ctx.lineTo(r.right - 1.5, r.bottom - radius);
  ctx.stroke();

  // Module groove under control strip when opened
  if (r.height >= rowHeight + 26) {
    const grooveY = r.top + rowHeight;
    ctx.strokeStyle = dark ? 'rgba(0,0,0,0.43)' : 'rgba(0,0,0,0.22)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(leftEarX + kEarWidth + 2, grooveY);
    ctx.lineTo(rightEarX - 2, grooveY);
    ctx.stroke();

    ctx.strokeStyle = dark ? 'rgba(255,255,255,0.094)' : 'rgba(255,255,255,0.43)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(leftEarX + kEarWidth + 2, grooveY + 1);
    ctx.lineTo(rightEarX - 2, grooveY + 1);
    ctx.stroke();
  }

  // Corner screws
  const seed = hashString(command);
  const screwR = 4.0;
  const screwPositions = [
    { x: r.left + 10, y: r.top + 9 },
    { x: r.right - 10, y: r.top + 9 },
    { x: r.left + 10, y: r.bottom - 9 },
    { x: r.right - 10, y: r.bottom - 9 },
  ];
  const screwCount = r.height >= 40 ? 4 : 2;
  for (let i = 0; i < screwCount; i++) {
    const slotAngle = (seed + i * 73) % 180;
    drawScrew(ctx, screwPositions[i].x, screwPositions[i].y, screwR, slotAngle, dark);
  }

  // Status LEDs: green power (enabled), amber select (selected)
  const litGreen = tokens.accent2 || '#4CAF50';
  drawLed(ctx, r.left + 10, r.top + 21, 3.0, litGreen, enabled, dark);

  if (r.height >= 44) {
    const litAmber = tokens.accent || '#FF9800';
    drawLed(ctx, r.left + 10, r.top + 31.5, 2.4, litAmber, selected, dark);
  }

  // Include: patchbay jacks on right ear
  if (type === 'include') {
    drawJack(ctx, r.right - 10, r.top + 21, dark);
    if (r.height >= 46) {
      drawJack(ctx, r.right - 10, r.top + 32.5, dark);
    }
  }

  // VST: brass nameplate (if width >= 320)
  if (type === 'vst' && w >= 320) {
    const plateX = r.right - kEarWidth - 8 - kNameplateWidth;
    const plateY = r.top + (rowHeight - kNameplateHeight) / 2;
    drawBrassNameplate(ctx, plateX, plateY, kNameplateWidth, kNameplateHeight, dark);
  }

  // Engraved unit designation up left ear (rotate -90) when tall enough
  if (r.height >= 96 && label) {
    ctx.save();
    ctx.translate(r.left + 10, r.top + r.height / 2);
    ctx.rotate(-Math.PI / 2);
    const mutedText = tokens.mutedText || '#808080';
    engraveText(
      ctx,
      { x: -r.height / 2 + 10, y: -12, width: r.height - 20, height: 24 },
      label,
      mutedText,
      dark,
      { fontSize: 8, bold: true }
    );
    ctx.restore();
  }

  // Powered-down overlay film
  if (!enabled) {
    ctx.fillStyle = dark ? 'rgba(0,0,0,0.31)' : 'rgba(255,252,244,0.47)';
    ctx.globalAlpha = 1;
    beginRoundedRectPath(ctx, r.left, r.top, r.width, r.height, radius);
    ctx.fill();
  }

  // Keyboard focus ring
  if (focused) {
    ctx.strokeStyle = tokens.focusRing || '#FFB74D';
    ctx.globalAlpha = 190 / 255;
    ctx.lineWidth = 1.5;
    drawRoundedRect(ctx, r.left + 1.5, r.top + 1.5, r.width - 3, r.height - 3, radius - 1);
    ctx.globalAlpha = 1;
  }

  ctx.restore();
}

export function drawScrew(ctx, cx, cy, radius, slotDegrees, dark) {
  ctx.save();
  ctx.imageSmoothingEnabled = true;

  const focalX = cx - radius * 0.35;
  const focalY = cy - radius * 0.35;
  const bodyGrad = ctx.createRadialGradient(focalX, focalY, 0, cx, cy, radius * 2.1);

  if (dark) {
    bodyGrad.addColorStop(0, 'rgb(154, 164, 172)');
    bodyGrad.addColorStop(0.55, 'rgb(78, 87, 94)');
    bodyGrad.addColorStop(1, 'rgb(35, 40, 44)');
  } else {
    bodyGrad.addColorStop(0, 'rgb(255, 255, 252)');
    bodyGrad.addColorStop(0.55, 'rgb(196, 189, 174)');
    bodyGrad.addColorStop(1, 'rgb(142, 134, 118)');
  }

  ctx.fillStyle = bodyGrad;
  ctx.strokeStyle = dark ? 'rgba(0,0,0,0.78)' : 'rgb(107, 98, 82)';
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.arc(cx, cy, radius, 0, 2 * Math.PI);
  ctx.fill();
  ctx.stroke();

  const rad = (slotDegrees * Math.PI) / 180;
  const dirX = Math.cos(rad);
  const dirY = Math.sin(rad);
  const ax = cx - dirX * (radius - 1.2);
  const ay = cy - dirY * (radius - 1.2);
  const bx = cx + dirX * (radius - 1.2);
  const by = cy + dirY * (radius - 1.2);

  ctx.strokeStyle = dark ? 'rgba(10,12,14,0.90)' : 'rgba(60,54,44,0.86)';
  ctx.lineWidth = 1.4;
  ctx.lineCap = 'round';
  ctx.beginPath();
  ctx.moveTo(ax, ay);
  ctx.lineTo(bx, by);
  ctx.stroke();

  ctx.strokeStyle = dark ? 'rgba(255,255,255,0.24)' : 'rgba(255,255,255,0.67)';
  ctx.lineWidth = 0.8;
  ctx.lineCap = 'round';
  ctx.beginPath();
  ctx.moveTo(ax, ay + 1);
  ctx.lineTo(bx, by + 1);
  ctx.stroke();

  ctx.restore();
}

export function drawLed(ctx, cx, cy, radius, litHex, lit, dark) {
  ctx.save();
  ctx.imageSmoothingEnabled = true;

  ctx.strokeStyle = dark ? 'rgba(0,0,0,0.75)' : 'rgba(70,62,50,0.75)';
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.arc(cx, cy, radius + 1.2, 0, 2 * Math.PI);
  ctx.stroke();

  if (lit) {
    const haloGrad = ctx.createRadialGradient(cx, cy, 0, cx, cy, radius * 3.2);
    haloGrad.addColorStop(0, hexToRgba(litHex, 110 / 255));
    haloGrad.addColorStop(1, hexToRgba(litHex, 0));
    ctx.fillStyle = haloGrad;
    ctx.beginPath();
    ctx.arc(cx, cy, radius * 3.2, 0, 2 * Math.PI);
    ctx.fill();
  }

  const focalX = cx - radius * 0.3;
  const focalY = cy - radius * 0.3;
  const domeGrad = ctx.createRadialGradient(focalX, focalY, 0, cx, cy, radius * 1.6);

  if (lit) {
    const litColor = lighten(litHex, 150);
    domeGrad.addColorStop(0, litColor);
    domeGrad.addColorStop(1, darken(litHex, 125));
  } else {
    const offColor = darken(litHex, 330);
    domeGrad.addColorStop(0, lighten(offColor, 140));
    domeGrad.addColorStop(1, offColor);
  }

  ctx.fillStyle = domeGrad;
  ctx.beginPath();
  ctx.arc(cx, cy, radius, 0, 2 * Math.PI);
  ctx.fill();

  const specX = cx - radius * 0.35;
  const specY = cy - radius * 0.35;
  const specRadius = radius * 0.3;
  const specAlpha = lit ? 170 / 255 : dark ? 28 / 255 : 60 / 255;
  ctx.fillStyle = `rgba(255,255,255,${specAlpha})`;
  ctx.beginPath();
  ctx.arc(specX, specY, specRadius, 0, 2 * Math.PI);
  ctx.fill();

  ctx.restore();
}

export function drawJack(ctx, cx, cy, dark) {
  ctx.save();
  ctx.imageSmoothingEnabled = true;

  const focalX = cx - 1.4;
  const focalY = cy - 1.4;
  const flangeGrad = ctx.createRadialGradient(focalX, focalY, 0, cx, cy, 7.5);

  if (dark) {
    flangeGrad.addColorStop(0, 'rgb(168, 177, 184)');
    flangeGrad.addColorStop(0.6, 'rgb(85, 94, 100)');
    flangeGrad.addColorStop(1, 'rgb(38, 43, 47)');
  } else {
    flangeGrad.addColorStop(0, 'rgb(255, 255, 252)');
    flangeGrad.addColorStop(0.6, 'rgb(192, 185, 170)');
    flangeGrad.addColorStop(1, 'rgb(134, 126, 110)');
  }

  ctx.fillStyle = flangeGrad;
  ctx.strokeStyle = dark ? 'rgba(0,0,0,0.82)' : 'rgb(96, 88, 72)';
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.arc(cx, cy, 4.6, 0, 2 * Math.PI);
  ctx.fill();
  ctx.stroke();

  ctx.fillStyle = 'rgb(8, 9, 10)';
  ctx.strokeStyle = 'rgba(0,0,0,0.86)';
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.arc(cx, cy, 2.1, 0, 2 * Math.PI);
  ctx.fill();
  ctx.stroke();

  const specX = cx - 2.5;
  const specY = cy - 2.7;
  ctx.fillStyle = dark ? 'rgba(255,255,255,0.27)' : 'rgba(255,255,255,0.59)';
  ctx.beginPath();
  ctx.arc(specX, specY, 0.9, 0, 2 * Math.PI);
  ctx.fill();

  ctx.restore();
}

// ── helpers ──

function paintBrushing(ctx, r, dark, seed) {
  const baseAlpha = dark ? 4 : 5;
  const inkHex = dark ? '#FFFFFF' : '#604028';

  for (let y = r.top + 2; y < r.bottom - 1; y += 2) {
    const h = Math.imul(seed ^ Math.round(y * 7), 2654435761) >>> 0;
    const polish = ((h >>> 8) % 11) === 0;
    const brushAlpha = (baseAlpha + (h % 7) + (polish ? 6 : 0)) / 255;
    ctx.strokeStyle = hexToRgba(inkHex, brushAlpha);
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(r.left + 2, y);
    ctx.lineTo(r.right - 2, y);
    ctx.stroke();
  }
}

function drawBrassNameplate(ctx, x, y, w, h, dark) {
  ctx.save();
  ctx.imageSmoothingEnabled = false;

  const brassGrad = ctx.createLinearGradient(x, y, x, y + h);
  if (dark) {
    brassGrad.addColorStop(0, '#D6B26A');
    brassGrad.addColorStop(0.5, '#A88546');
    brassGrad.addColorStop(1, '#866730');
  } else {
    brassGrad.addColorStop(0, '#E8C886');
    brassGrad.addColorStop(0.5, '#C4A05C');
    brassGrad.addColorStop(1, '#9A7A3C');
  }

  ctx.fillStyle = brassGrad;
  ctx.fillRect(x, y, w, h);

  ctx.strokeStyle = '#5A4416';
  ctx.lineWidth = 1;
  ctx.strokeRect(x + 0.5, y + 0.5, w - 1, h - 1);

  ctx.font = 'bold 10px monospace';
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  const cx = x + w / 2;
  const cy = y + h / 2;

  ctx.fillStyle = 'rgba(255, 240, 200, 0.63)';
  ctx.fillText('VST', cx, cy + 1);

  ctx.fillStyle = '#3A2A0C';
  ctx.fillText('VST', cx, cy);

  const rivetX1 = x + w * 0.25;
  const rivetX2 = x + w * 0.75;
  const rivetY = y + h / 2;
  ctx.fillStyle = '#5A4416';
  ctx.beginPath();
  ctx.arc(rivetX1, rivetY, 1.2, 0, 2 * Math.PI);
  ctx.fill();
  ctx.beginPath();
  ctx.arc(rivetX2, rivetY, 1.2, 0, 2 * Math.PI);
  ctx.fill();

  ctx.restore();
}

function engraveText(ctx, rect, text, color, dark, opts = {}) {
  const { fontSize = 10, bold = false } = opts;

  ctx.font = (bold ? 'bold ' : '') + fontSize + 'px sans-serif';
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';

  const x = rect.x + rect.width / 2;
  const y = rect.y + rect.height / 2;

  const shadowColor = dark ? 'rgba(0,0,0,0.67)' : 'rgba(255,255,255,0.78)';
  ctx.fillStyle = shadowColor;
  ctx.fillText(text, x, y + 1);

  ctx.fillStyle = color;
  ctx.fillText(text, x, y);
}

function hexToRgba(hex, alpha = 1) {
  if (typeof hex !== 'string') return 'rgba(0,0,0,0)';
  hex = hex.replace('#', '');
  const r = parseInt(hex.substring(0, 2), 16);
  const g = parseInt(hex.substring(2, 4), 16);
  const b = parseInt(hex.substring(4, 6), 16);
  return `rgba(${r},${g},${b},${Math.max(0, Math.min(alpha, 1))})`;
}

function hexToRgb(hex) {
  hex = hex.replace('#', '');
  return {
    r: parseInt(hex.substring(0, 2), 16),
    g: parseInt(hex.substring(2, 4), 16),
    b: parseInt(hex.substring(4, 6), 16),
  };
}

function rgbToHex(r, g, b) {
  return '#' + [r, g, b]
    .map((x) => Math.round(Math.max(0, Math.min(255, x))).toString(16).padStart(2, '0'))
    .join('')
    .toUpperCase();
}

function rgbToHsl(r, g, b) {
  r /= 255;
  g /= 255;
  b /= 255;
  const max = Math.max(r, g, b);
  const min = Math.min(r, g, b);
  let h, s;
  const l = (max + min) / 2;

  if (max === min) {
    h = s = 0;
  } else {
    const d = max - min;
    s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
    switch (max) {
      case r: h = ((g - b) / d + (g < b ? 6 : 0)) / 6; break;
      case g: h = ((b - r) / d + 2) / 6; break;
      case b: h = ((r - g) / d + 4) / 6; break;
    }
  }

  return { h, s, l };
}

function hslToRgb(h, s, l) {
  let r, g, b;

  if (s === 0) {
    r = g = b = l;
  } else {
    const hue2rgb = (p, q, t) => {
      if (t < 0) t += 1;
      if (t > 1) t -= 1;
      if (t < 1 / 6) return p + (q - p) * 6 * t;
      if (t < 1 / 2) return q;
      if (t < 2 / 3) return p + (q - p) * (2 / 3 - t) * 6;
      return p;
    };

    const q = l < 0.5 ? l * (1 + s) : l + s - l * s;
    const p = 2 * l - q;
    r = hue2rgb(p, q, h + 1 / 3);
    g = hue2rgb(p, q, h);
    b = hue2rgb(p, q, h - 1 / 3);
  }

  return {
    r: Math.round(r * 255),
    g: Math.round(g * 255),
    b: Math.round(b * 255),
  };
}

export function lighten(hex, factor) {
  const rgb = hexToRgb(hex);
  const hsl = rgbToHsl(rgb.r, rgb.g, rgb.b);
  hsl.l = Math.min(1, hsl.l * (factor / 100));
  const newRgb = hslToRgb(hsl.h, hsl.s, hsl.l);
  return rgbToHex(newRgb.r, newRgb.g, newRgb.b);
}

export function darken(hex, factor) {
  const rgb = hexToRgb(hex);
  const hsl = rgbToHsl(rgb.r, rgb.g, rgb.b);
  hsl.l = Math.max(0, hsl.l / (factor / 100));
  const newRgb = hslToRgb(hsl.h, hsl.s, hsl.l);
  return rgbToHex(newRgb.r, newRgb.g, newRgb.b);
}

function hashString(str) {
  if (!str) return 0;
  let hash = 0;
  for (let i = 0; i < str.length; i++) {
    hash = Math.imul(hash ^ str.charCodeAt(i), 2654435761) >>> 0;
  }
  return hash;
}

function beginRoundedRectPath(ctx, x, y, w, h, r) {
  ctx.beginPath();
  ctx.moveTo(x + r, y);
  ctx.lineTo(x + w - r, y);
  ctx.quadraticCurveTo(x + w, y, x + w, y + r);
  ctx.lineTo(x + w, y + h - r);
  ctx.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
  ctx.lineTo(x + r, y + h);
  ctx.quadraticCurveTo(x, y + h, x, y + h - r);
  ctx.lineTo(x, y + r);
  ctx.quadraticCurveTo(x, y, x + r, y);
  ctx.closePath();
}

function drawRoundedRect(ctx, x, y, w, h, r) {
  beginRoundedRectPath(ctx, x, y, w, h, r);
  ctx.stroke();
}
