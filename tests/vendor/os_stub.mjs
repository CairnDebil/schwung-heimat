/* Minimal stand-in for QuickJS's `os` module, used by the UI test only. */
import fs from 'node:fs';
export function readdir(p) { try { return [fs.readdirSync(p), 0]; } catch (e) { return [[], -1]; } }
export function stat(p) { try { const s = fs.statSync(p); return [{ mode: s.mode }, 0]; } catch (e) { return [null, -1]; } }
