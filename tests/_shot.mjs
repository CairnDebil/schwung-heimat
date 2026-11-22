import { chromium } from 'playwright';
import path from 'path';
const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1280, height: 900 } });
await p.goto('file://' + path.resolve('src/web/web_ui.html'));
await p.waitForTimeout(1500);
const el = await p.$('[aria-labelledby="hMods"]'); await el.screenshot({ path: '/tmp/claude-0/mods.png' });
const el2 = await p.$('[aria-labelledby="hPlay"]'); await el2.screenshot({ path: '/tmp/claude-0/play.png' });
await b.close();
