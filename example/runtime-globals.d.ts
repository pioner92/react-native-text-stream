// RN 0.86/Hermes installs these globals, but its generated TypeScript entry does not
// declare them. Keep this example-only surface narrow instead of importing all DOM/Node APIs.
export {};

declare global {
  class TextEncoder {
    encode(input?: string): Uint8Array<ArrayBuffer>;
  }
  const performance: { now(): number };
}
