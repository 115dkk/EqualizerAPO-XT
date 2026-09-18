"""CommentCard: the Comment row (a note the engine skips) and the collapsed raw Text row (comment_normal, text_normal)."""
from rowlib import *

CSS = '''
  .note { display: flex; align-items: center; gap: 12px; height: 34px; }
  .hash { display: inline-flex; align-items: center; justify-content: center; width: 24px; height: 24px; font-family: var(--mono); font-weight: 700; font-size: 14px; color: var(--muted); }
  .txt { flex: 1; font-size: 14px; }
  .skin-list .crow + .crow { margin-top: 8px; }
  /* studio: unlit glass, no ornament; the note is ink only (a transparent line edit) */
  .skin-studio .txt { color: var(--text); }
  /* minimal: the source-comment register; the whole line sinks to the secondary ink */
  .skin-minimal .note { height: 28px; }
  .skin-minimal .hash, .skin-minimal .txt { color: var(--muted); font-family: var(--mono); font-size: 13px; }
  /* soft: the note is a warm memo paper, the hash a small sticker (the warm amber pastel, warmer never an alarm) */
  .skin-soft .hash { width: 34px; height: 34px; border-radius: 10px; background: var(--warning); color: var(--on-accent); }
  .skin-soft .txt { height: 34px; display: flex; align-items: center; padding: 0 14px; border-radius: 999px; background: color-mix(in srgb, var(--warning) 18%, var(--surface)); border: 1px solid color-mix(in srgb, var(--warning) 45%, var(--border)); }
  /* rack: a Dymo embossed label tape: glossy black strip, raised ivory type on both finishes */
  .skin-rack .hash { color: var(--muted); font-size: 12px; }
  .skin-rack .txt { height: 26px; display: flex; align-items: center; padding: 0 12px; border-radius: 2px; background: linear-gradient(180deg, #2a2a2a, #0d0d0d); color: #f1e9d8; font-weight: 700; font-size: 12.5px;
    text-shadow: 0 1px 0 rgba(255,255,255,.25), 0 -1px 0 rgba(0,0,0,.6); box-shadow: inset 0 1px 0 rgba(255,255,255,.12), 0 1px 1px rgba(0,0,0,.6); }
  /* matrix: the remark posting lives in cells: a sunken designation cell and a sunken line cell */
  .skin-matrix .hash { width: 28px; height: 26px; border: 1px solid var(--border); background: var(--graph); color: var(--text); font-size: 12px; }
  .skin-matrix .txt { height: 26px; display: flex; align-items: center; padding: 0 10px; border: 1px solid var(--border); background: var(--graph); font-family: var(--mono); font-size: 12.5px; }
'''
NOTE = 'Living room preset - tuned by ear'
RAW = 'plain note line without a command'

def comment_body(skin):
    return f'<div class="note"><span class="hash">#</span><span class="txt">{NOTE}</span></div>'

frames = []
for s in SKINS:
    inner = row(s, 'comment', 13, 'Comment', comment_body(s), summary=NOTE, power=False)
    inner += row(s, 'text', 21, 'Text', '', summary=RAW, collapsed=True, pict='TXT')
    frames.append(frame(s, inner))
page('CommentCard', 'Rows', 1000, 'The Comment row (no power, the note in the body) and the collapsed raw Text row, five skins', CSS, frames)
