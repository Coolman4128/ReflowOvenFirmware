import { rm, mkdir, cp } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const thisFile = fileURLToPath(import.meta.url);
const scriptsDir = dirname(thisFile);
const root = resolve(scriptsDir, '..', '..');
const distDir = resolve(root, 'frontend', 'dist');
const firmwareDir = resolve(root, 'main', 'webui');

if (!existsSync(distDir)) {
  throw new Error(`Build output not found: ${distDir}`);
}

await rm(firmwareDir, { recursive: true, force: true });
await mkdir(firmwareDir, { recursive: true });
await cp(distDir, firmwareDir, { recursive: true });

console.log(`Copied frontend dist to ${firmwareDir}`);
