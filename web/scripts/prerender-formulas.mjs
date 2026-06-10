import katex from 'katex';
import { writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { FORMULAS } from '../src/formulas.mjs';

const __dirname = dirname(fileURLToPath(import.meta.url));

const INLINE = new Set(['L', 'indiff', 'mu_pm', 'v_exec', 'half_spread', 'lnL']);

const out = {};
for (const [key, tex] of Object.entries(FORMULAS)) {
  out[key] = katex.renderToString(tex, {
    displayMode: !INLINE.has(key),
    throwOnError: true,
    output: 'htmlAndMathml',
  });
}

const target = join(__dirname, '..', 'src', 'prerendered-formulas.json');
writeFileSync(target, JSON.stringify(out));
console.log(`Prerendered ${Object.keys(out).length} formulas -> ${target}`);
