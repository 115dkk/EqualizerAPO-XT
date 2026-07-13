// rack-controls.js — Qt "rack" skin knob + phosphor scope translated to canvas.
// Render translation of RackChrome::paintKnob and paintGraphicEqPlot.

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function rgba(r, g, b, a = 255) {
  return `rgba(${r},${g},${b},${a / 255})`;
}

function parseColor(color) {
  const value = String(color).trim();

  let match = /^#([0-9a-f]{6})$/i.exec(value);
  if (match) {
    const n = parseInt(match[1], 16);
    return {
      r: (n >> 16) & 255,
      g: (n >> 8) & 255,
      b: n & 255
    };
  }

  match = /^#([0-9a-f]{3})$/i.exec(value);
  if (match) {
    return {
      r: parseInt(match[1][0] + match[1][0], 16),
      g: parseInt(match[1][1] + match[1][1], 16),
      b: parseInt(match[1][2] + match[1][2], 16)
    };
  }

  match = /^rgba?\(\s*([\d.]+)\s*,\s*([\d.]+)\s*,\s*([\d.]+)/i.exec(value);
  if (match) {
    return {
      r: clamp(Math.round(Number(match[1])), 0, 255),
      g: clamp(Math.round(Number(match[2])), 0, 255),
      b: clamp(Math.round(Number(match[3])), 0, 255)
    };
  }

  throw new TypeError(`Unsupported color: ${color}`);
}

function rgbToHsl(r, g, b) {
  r /= 255;
  g /= 255;
  b /= 255;

  const max = Math.max(r, g, b);
  const min = Math.min(r, g, b);
  const l = (max + min) / 2;

  if (max === min) {
    return { h: 0, s: 0, l };
  }

  const d = max - min;
  const s = l > 0.5 ? d / (2 - max - min) : d / (max + min);

  let h;
  if (max === r) {
    h = (g - b) / d + (g < b ? 6 : 0);
  } else if (max === g) {
    h = (b - r) / d + 2;
  } else {
    h = (r - g) / d + 4;
  }

  return { h: h / 6, s, l };
}

function hslToRgb(h, s, l) {
  if (s === 0) {
    const gray = Math.round(l * 255);
    return { r: gray, g: gray, b: gray };
  }

  const hueToRgb = (p, q, t) => {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1 / 6) return p + (q - p) * 6 * t;
    if (t < 1 / 2) return q;
    if (t < 2 / 3) return p + (q - p) * (2 / 3 - t) * 6;
    return p;
  };

  const q = l < 0.5 ? l * (1 + s) : l + s - l * s;
  const p = 2 * l - q;

  return {
    r: Math.round(hueToRgb(p, q, h + 1 / 3) * 255),
    g: Math.round(hueToRgb(p, q, h) * 255),
    b: Math.round(hueToRgb(p, q, h - 1 / 3) * 255)
  };
}

function toHex(r, g, b) {
  const channel = value => clamp(Math.round(value), 0, 255)
    .toString(16)
    .padStart(2, "0");

  return `#${channel(r)}${channel(g)}${channel(b)}`;
}

function lighten(hex, n) {
  const { r, g, b } = parseColor(hex);
  const hsl = rgbToHsl(r, g, b);
  const rgb = hslToRgb(hsl.h, hsl.s, clamp(hsl.l * (n / 100), 0, 1));
  return toHex(rgb.r, rgb.g, rgb.b);
}

function darken(hex, n) {
  const { r, g, b } = parseColor(hex);
  const hsl = rgbToHsl(r, g, b);
  const rgb = hslToRgb(hsl.h, hsl.s, clamp(hsl.l / (n / 100), 0, 1));
  return toHex(rgb.r, rgb.g, rgb.b);
}

function withAlpha(color, alpha) {
  const { r, g, b } = parseColor(color);
  return rgba(r, g, b, alpha);
}

function isDarkColor(color) {
  const { r, g, b } = parseColor(color);
  return rgbToHsl(r, g, b).l < 0.5;
}

function circlePath(ctx, cx, cy, radius) {
  ctx.beginPath();
  ctx.arc(cx, cy, radius, 0, Math.PI * 2);
}

function roundedRectPath(ctx, x, y, w, h, radius) {
  const r = Math.max(0, Math.min(radius, Math.abs(w) / 2, Math.abs(h) / 2));
  const right = x + w;
  const bottom = y + h;

  ctx.beginPath();
  ctx.moveTo(x + r, y);
  ctx.lineTo(right - r, y);
  ctx.quadraticCurveTo(right, y, right, y + r);
  ctx.lineTo(right, bottom - r);
  ctx.quadraticCurveTo(right, bottom, right - r, bottom);
  ctx.lineTo(x + r, bottom);
  ctx.quadraticCurveTo(x, bottom, x, bottom - r);
  ctx.lineTo(x, y + r);
  ctx.quadraticCurveTo(x, y, x + r, y);
  ctx.closePath();
}

function strokeLine(ctx, x1, y1, x2, y2) {
  ctx.beginPath();
  ctx.moveTo(x1, y1);
  ctx.lineTo(x2, y2);
  ctx.stroke();
}

function strokePolyline(ctx, curve) {
  ctx.beginPath();
  ctx.moveTo(curve[0][0], curve[0][1]);

  for (let i = 1; i < curve.length; i++) {
    ctx.lineTo(curve[i][0], curve[i][1]);
  }

  ctx.stroke();
}

export function drawKnob(ctx, x, y, w, h, state, tokens) {
  ctx.save();

  const dark = isDarkColor(tokens.card);
  const innerX = x + 4;
  const innerY = y + 4;
  const innerW = w - 8;
  const innerH = h - 8;
  const side = Math.min(innerW, innerH);
  const cx = innerX + innerW / 2;
  const cy = innerY + innerH / 2;
  const scaleRadius = side / 2;
  const bodyRadius = scaleRadius - 9;

  const pointAt = (ratio, radius) => {
    const angle = (135 + 270 * ratio) * Math.PI / 180;
    return [
      cx + Math.cos(angle) * radius,
      cy + Math.sin(angle) * radius
    ];
  };

  const inkBase = tokens.mutedText;
  const inkStrong = tokens.text;
  const inkAlpha = state.enabled ? 235 : 90;
  const minorAlpha = state.enabled ? 165 : 70;

  ctx.lineCap = "butt";

  for (let i = 0; i <= 10; i++) {
    const ratio = i / 10;
    const centerTick = state.bipolar && i === 5;
    const major = i === 0 || i === 10 || centerTick;
    const ink = centerTick
      ? tokens.accent
      : major
        ? inkStrong
        : inkBase;

    const innerR = centerTick ? bodyRadius + 2 : bodyRadius + 3.5;
    const outerR = centerTick
      ? scaleRadius + 2
      : major
        ? scaleRadius + 0.5
        : scaleRadius - 1.5;

    const start = pointAt(ratio, innerR);
    const end = pointAt(ratio, outerR);

    ctx.strokeStyle = withAlpha(ink, major ? inkAlpha : minorAlpha);
    ctx.lineWidth = centerTick ? 3 : major ? 2 : 1;
    strokeLine(ctx, start[0], start[1], end[0], end[1]);
  }

  if (state.bipolar) {
    const minusAt = pointAt(-0.07, scaleRadius - 2.5);
    const plusAt = pointAt(1.07, scaleRadius - 2.5);

    ctx.font = "bold 11px sans-serif";
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";

    ctx.fillStyle = dark
      ? rgba(0, 0, 0, 170)
      : rgba(255, 255, 255, 200);
    ctx.fillText("-", minusAt[0], minusAt[1] + 1);
    ctx.fillText("+", plusAt[0], plusAt[1] + 1);

    ctx.fillStyle = withAlpha(inkStrong, inkAlpha);
    ctx.fillText("-", minusAt[0], minusAt[1]);
    ctx.fillText("+", plusAt[0], plusAt[1]);
  }

  const bodyGradient = ctx.createRadialGradient(
    cx - bodyRadius * 0.4,
    cy - bodyRadius * 0.4,
    0,
    cx,
    cy,
    bodyRadius * 2.2
  );

  if (dark) {
    bodyGradient.addColorStop(0, lighten(tokens.card, 190));
    bodyGradient.addColorStop(0.6, lighten(tokens.card, 115));
    bodyGradient.addColorStop(1, darken(tokens.card, 160));
  } else {
    bodyGradient.addColorStop(0, rgba(255, 255, 255));
    bodyGradient.addColorStop(0.6, rgba(222, 215, 198));
    bodyGradient.addColorStop(1, rgba(168, 159, 140));
  }

  circlePath(ctx, cx, cy, bodyRadius);
  ctx.fillStyle = bodyGradient;
  ctx.fill();
  ctx.strokeStyle = dark
    ? rgba(0, 0, 0, 200)
    : rgba(126, 117, 98);
  ctx.lineWidth = 1;
  ctx.stroke();

  const capRadius = bodyRadius - 3.5;

  circlePath(ctx, cx, cy, capRadius);
  ctx.strokeStyle = rgba(0, 0, 0, dark ? 90 : 50);
  ctx.lineWidth = 1;
  ctx.stroke();

  ctx.beginPath();
  ctx.arc(
    cx,
    cy,
    capRadius,
    -120 * Math.PI / 180,
    -60 * Math.PI / 180
  );
  ctx.strokeStyle = rgba(255, 255, 255, dark ? 70 : 150);
  ctx.lineWidth = 1.2;
  ctx.lineCap = "square";
  ctx.stroke();

  let pointerColor;
  if (!state.enabled) {
    pointerColor = withAlpha(inkBase, 130);
  } else if (state.dragging || state.hovered) {
    pointerColor = tokens.accent;
  } else {
    pointerColor = dark
      ? rgba(242, 236, 220)
      : rgba(46, 41, 34);
  }

  const pointerBase = pointAt(state.ratio, bodyRadius * 0.28);
  const pointerTip = pointAt(state.ratio, bodyRadius - 1.8);

  ctx.lineCap = "round";
  ctx.strokeStyle = rgba(
    0,
    0,
    0,
    state.enabled ? (dark ? 150 : 90) : 50
  );
  ctx.lineWidth = 3.6;
  strokeLine(
    ctx,
    pointerBase[0],
    pointerBase[1],
    pointerTip[0],
    pointerTip[1]
  );

  ctx.strokeStyle = pointerColor;
  ctx.lineWidth = 2.4;
  strokeLine(
    ctx,
    pointerBase[0],
    pointerBase[1],
    pointerTip[0],
    pointerTip[1]
  );

  if (state.enabled && (state.hovered || state.dragging)) {
    circlePath(ctx, cx, cy, bodyRadius + 0.8);
    ctx.strokeStyle = withAlpha(tokens.accent, 90);
    ctx.lineWidth = 1.4;
    ctx.stroke();
  }

  if (state.focused) {
    circlePath(ctx, cx, cy, scaleRadius + 2);
    ctx.strokeStyle = withAlpha(tokens.focusRing, 180);
    ctx.lineWidth = 1;
    ctx.stroke();
  }

  if (!state.enabled) {
    circlePath(ctx, cx, cy, bodyRadius);
    ctx.fillStyle = dark
      ? rgba(0, 0, 0, 90)
      : rgba(255, 252, 244, 130);
    ctx.fill();
  }

  ctx.restore();
}

export function drawScope(ctx, rect, curve, tokens, opts) {
  ctx.save();

  const dark = opts.dark;
  const powered = opts.powered;
  const plotRect = opts.plotRect;

  const glassTop = dark
    ? rgba(4, 6, 5)
    : rgba(10, 14, 11);
  const glassBottom = dark
    ? rgba(10, 15, 12)
    : rgba(17, 22, 16);
  const bezel = dark
    ? rgba(5, 8, 7)
    : rgba(74, 68, 56);
  const bezelLip = dark
    ? rgba(57, 66, 74)
    : rgba(107, 99, 84);

  const gridMinor = dark
    ? tokens.graphGridMinor
    : rgba(37, 67, 55);
  const gridMajor = lighten(gridMinor, 168);

  const phosphor = dark
    ? tokens.accent2
    : lighten(tokens.accent2, 195);

  const segOff = dark
    ? rgba(58, 107, 81)
    : rgba(47, 107, 77);

  const rx = rect.x + 0.5;
  const ry = rect.y + 0.5;
  const rw = rect.w - 1;
  const rh = rect.h - 1;
  const right = rx + rw;
  const bottom = ry + rh;

  roundedRectPath(ctx, rx, ry, rw, rh, 2);
  ctx.clip();

  const ground = ctx.createLinearGradient(rx, ry, rx, bottom);
  ground.addColorStop(0, glassTop);
  ground.addColorStop(1, glassBottom);
  ctx.fillStyle = ground;
  ctx.fillRect(rx, ry, rw, rh);

  const plotLeft = plotRect.x;
  const plotTop = plotRect.y;
  const plotRight = plotRect.x + plotRect.w;
  const plotBottom = plotRect.y + plotRect.h;

  if (powered) {
    const plotCenterX = plotRect.x + plotRect.w / 2;
    const plotCenterY = plotRect.y + plotRect.h / 2;
    const glowRadius = plotRect.w * 0.55;

    const backgroundGlow = ctx.createRadialGradient(
      plotCenterX,
      plotCenterY,
      0,
      plotCenterX,
      plotCenterY,
      glowRadius
    );
    backgroundGlow.addColorStop(
      0,
      withAlpha(phosphor, dark ? 12 : 14)
    );
    backgroundGlow.addColorStop(1, withAlpha(phosphor, 0));

    ctx.fillStyle = backgroundGlow;
    ctx.fillRect(
      plotRect.x,
      plotRect.y,
      plotRect.w,
      plotRect.h
    );
  }

  const gridAlpha = powered ? 255 : 150;
  ctx.lineWidth = 1;
  ctx.lineCap = "square";

  for (const line of opts.vertical) {
    ctx.strokeStyle = withAlpha(
      line.major ? gridMajor : gridMinor,
      gridAlpha
    );
    strokeLine(ctx, line.pos, plotTop, line.pos, plotBottom);
  }

  for (const line of opts.horizontal) {
    ctx.strokeStyle = withAlpha(
      line.major ? gridMajor : gridMinor,
      gridAlpha
    );
    strokeLine(ctx, plotLeft, line.pos, plotRight, line.pos);
  }

  const zeroInRange =
    opts.zeroY >= plotTop &&
    opts.zeroY <= plotBottom;

  if (zeroInRange) {
    ctx.strokeStyle = withAlpha(
      powered ? phosphor : segOff,
      powered ? 145 : 80
    );
    ctx.lineWidth = 1;
    strokeLine(ctx, plotLeft, opts.zeroY, plotRight, opts.zeroY);

    ctx.strokeStyle = withAlpha(
      powered ? phosphor : segOff,
      powered ? 60 : 40
    );

    for (
      let dashX = Math.trunc(plotLeft + 4);
      dashX < plotRight - 2;
      dashX += 7
    ) {
      strokeLine(
        ctx,
        dashX,
        opts.zeroY - 2,
        dashX,
        opts.zeroY + 2
      );
    }
  }

  if (curve.length >= 2) {
    const base = clamp(opts.zeroY, plotTop, plotBottom);

    ctx.beginPath();
    ctx.moveTo(curve[0][0], base);

    for (const point of curve) {
      ctx.lineTo(point[0], point[1]);
    }

    ctx.lineTo(curve[curve.length - 1][0], base);
    ctx.closePath();
    ctx.fillStyle = withAlpha(phosphor, powered ? 22 : 10);
    ctx.fill();

    ctx.lineCap = "round";
    ctx.lineJoin = "round";

    if (powered) {
      ctx.strokeStyle = withAlpha(phosphor, 26);
      ctx.lineWidth = 6;
      strokePolyline(ctx, curve);

      ctx.strokeStyle = withAlpha(phosphor, 70);
      ctx.lineWidth = 3;
      strokePolyline(ctx, curve);

      ctx.strokeStyle = phosphor;
      ctx.lineWidth = 1.6;
      strokePolyline(ctx, curve);
    } else {
      ctx.strokeStyle = withAlpha(segOff, 200);
      ctx.lineWidth = 1.4;
      strokePolyline(ctx, curve);
    }
  }

  roundedRectPath(ctx, rx, ry, rw, rh, 2);
  ctx.strokeStyle = bezel;
  ctx.lineWidth = 1;
  ctx.lineCap = "square";
  ctx.lineJoin = "miter";
  ctx.stroke();

  ctx.strokeStyle = bezelLip;
  ctx.lineWidth = 1;
  strokeLine(ctx, rx + 2, bottom, right - 2, bottom);

  ctx.restore();
}
