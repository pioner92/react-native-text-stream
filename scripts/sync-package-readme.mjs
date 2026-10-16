import { readFile, writeFile, access } from 'node:fs/promises';

// Keep the repository README as the single source for the npm package page.
const root = new URL('../', import.meta.url);
const source = await readFile(new URL('README.md', root), 'utf8');
const destination = new URL('packages/react-native-text-stream/README.md', root);
const repository = 'https://github.com/pioner92/react-native-text-stream/blob/master/';
const assets = 'https://raw.githubusercontent.com/pioner92/react-native-text-stream/master/';
const localTargets = new Set();

function absoluteLink(target, image) {
  if (/^(?:[a-z][a-z\d+.-]*:|\/\/|#)/i.test(target)) return target;
  localTargets.add(target);
  return new URL(target, image ? assets : repository).href;
}

// Preserve code examples, including Mermaid, verbatim; rewrite only prose links.
const expected = source.split(/(^```[^\n]*\n[\s\S]*?^```[^\n]*(?:\n|$))/gm)
  .map((section, index) => index % 2 ? section : section
    .replace(/(!?)\[([^\]\n]*)\]\(([^\s)]+)\)/g,
      (_, image, label, target) => `${image}[${label}](${absoluteLink(target, Boolean(image))})`)
    .replace(/\b(src|href)=(['"])([^'"]+)\2/g,
      (_, attribute, quote, target) => `${attribute}=${quote}${absoluteLink(target, attribute === 'src')}${quote}`))
  .join('');

// A link to a missing repository file should fail packaging rather than ship broken docs.
await Promise.all([...localTargets].map(target => access(new URL(target, root))));

if (process.argv.includes('--check')) {
  if (await readFile(destination, 'utf8') !== expected) {
    throw new Error('Package README is out of sync. Run node scripts/sync-package-readme.mjs.');
  }
  console.log('Package README matches the repository README with absolute links.');
} else {
  await writeFile(destination, expected);
  console.log('Synced package README from the repository README.');
}
