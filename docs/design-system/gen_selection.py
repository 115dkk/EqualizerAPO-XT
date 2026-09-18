"""SelectionCards: the Device, Channel and Stage rows (device_normal, channel_normal, stage_normal): the five skins'
'pressable shape' for a toggled option."""
from rowlib import *

CSS = '''
  .opts { display: flex; align-items: center; gap: 8px; flex-wrap: wrap; }
  .opt { display: inline-flex; align-items: center; justify-content: center; height: 32px; padding: 0 16px; box-sizing: border-box; font-weight: 600; color: var(--text); white-space: nowrap; }
  .opt.q { color: var(--muted); font-weight: 500; }
  .lane { display: flex; align-items: center; gap: 10px; margin: 4px 0; }
  .lane .cap { min-width: 84px; }
  .arrow { width: 14px; height: 10px; color: var(--muted); }
  .skin-list .crow + .crow { margin-top: 8px; }
  /* studio: lit glass chips, the engaged chip glows from within */
  .skin-studio .opt { background: color-mix(in srgb, var(--card-hover) 80%, transparent); border: 1px solid var(--border); border-top-color: rgba(255,255,255,.10); border-radius: var(--radius); }
  .skin-studio .opt.on { background: color-mix(in srgb, var(--accent) 38%, var(--card)); border-color: color-mix(in srgb, var(--accent) 70%, transparent); box-shadow: inset 0 0 12px color-mix(in srgb, var(--accent) 25%, transparent); }
  .skin-studio .opt.q { background: transparent; border-color: transparent; }
  .skin-studio .opt.master { font-weight: 700; }
  /* minimal: hairline boxes, engaged = the inverted block */
  .skin-minimal .opt { background: transparent; border: 1px solid var(--border); border-radius: 0; font-family: var(--mono); font-weight: 500; }
  .skin-minimal .opt.master { text-transform: uppercase; letter-spacing: 1px; font-weight: 700; }
  .skin-minimal .opt.on { background: var(--text); color: var(--surface); border-color: var(--text); }
  .skin-minimal .opt.q { border-color: transparent; border-bottom-color: var(--border); }
  .skin-minimal .lane .cap { font-family: var(--mono); }
  /* soft: stadium pills, ON = pastel fill, OFF = sunken quiet pill */
  .skin-soft .opt { background: var(--sunken); border: 1px solid var(--border); border-radius: 999px; }
  .skin-soft .opt.on { background: var(--accent); color: var(--on-accent); border-color: transparent; }
  .skin-soft .opt.master { background: var(--card); font-weight: 700; }
  .skin-soft .opt.q { background: transparent; border: 1px dotted var(--border); }
  .skin-soft .lane .cap { text-transform: none; letter-spacing: 0; font-size: 12px; font-weight: 500; }
  /* rack: latching caps; engaged = latch-down with the amber backlight */
  .skin-rack .opt { background: linear-gradient(180deg, #2c333a, #1b2126); border: 1px solid #11161a; border-top-color: #3e474f; border-radius: 3px;
    text-transform: uppercase; letter-spacing: 1px; font-size: 11px; font-weight: 700; }
  [data-theme="light"] .skin-rack .opt { background: linear-gradient(180deg, var(--card-hover), var(--card)); border-color: var(--seam); border-top-color: #fff8; }
  .skin-rack .opt.on { background: linear-gradient(180deg, color-mix(in srgb, var(--accent) 22%, #1b2126), color-mix(in srgb, var(--accent) 35%, #2c333a)); border-color: var(--accent); border-top-color: #11161a; color: var(--accent); padding-top: 1px; box-shadow: inset 0 2px 3px rgba(0,0,0,.5); }
  .skin-rack .opt.q { background: color-mix(in srgb, var(--card) 70%, black); color: var(--muted); }
  .skin-rack .opt.master.on { border-color: var(--accent2); color: var(--accent2); background: linear-gradient(180deg, color-mix(in srgb, var(--accent2) 18%, #1b2126), color-mix(in srgb, var(--accent2) 28%, #2c333a)); }
  /* matrix: cells; engaged = accent-tinted cell with the accent rule */
  .skin-matrix .opt { background: var(--bg); border: 1px solid var(--border); border-radius: 0; }
  .skin-matrix .opt.on { background: color-mix(in srgb, var(--accent) 22%, var(--bg)); border-color: var(--accent); }
  .skin-matrix .opt.master { text-transform: uppercase; letter-spacing: 1px; font-size: 11px; }
  .skin-matrix .opt.q { border-color: transparent; font-family: var(--mono); text-transform: uppercase; letter-spacing: 1px; font-size: 11px; }
'''
ARROW = '<svg class="arrow" viewBox="0 0 14 10" fill="none" stroke="currentColor" stroke-width="1.5"><path d="M1 5h11"/><path d="M8 1.5L12 5 8 8.5"/></svg>'

def device(skin):
    return ('<div class="opts"><span class="opt master">All devices</span><span class="opt q">Show all (+1)</span>'
            '<span class="opt on">Speakers</span><span class="opt">Headphones</span><span class="opt on">Microphone</span></div>')

def channel(skin):
    return '<div class="opts"><span class="opt master">ALL</span><span class="opt on">L</span><span class="opt on">R</span><span class="opt q">Add channel</span></div>'

def stage(skin):
    return (f'<div class="lane"><span class="cap">Playback</span><span class="opt on">Pre-mix</span>{ARROW}<span class="opt on">Post-mix</span></div>'
            f'<div class="lane"><span class="cap">Recording</span><span class="opt">Capture</span></div>')

frames = []
for s in SKINS:
    inner = (row(s, 'device', 10, 'Device', device(s)) + row(s, 'channel', 12, 'Channel', channel(s), chans=('L', 'R'))
             + row(s, 'stage', 14, 'Stage', stage(s)))
    frames.append(frame(s, inner))
page('SelectionCards', 'Rows', 1900, 'Device, Channel and Stage rows: the pressable option shape and its engaged state, five skins', CSS, frames)
