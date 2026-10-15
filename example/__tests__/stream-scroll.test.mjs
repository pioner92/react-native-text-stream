import { test } from 'node:test';
import assert from 'node:assert/strict';
import { StreamScrollFollow } from '../src/StreamScrollFollow.ts';

test('content growth and programmatic scroll events keep the stream attached', () => {
  const state = new StreamScrollFollow();
  state.observe(0, 400, 1000);
  assert.equal(state.shouldFollow, true);
  state.observe(600, 400, 1800);
  assert.equal(state.shouldFollow, true);
});

test('a gesture suspends pending follow work before the first scroll event', () => {
  const state = new StreamScrollFollow();
  state.beginGesture();
  assert.equal(state.shouldFollow, false);
  state.observe(600, 400, 1000);
  assert.equal(state.shouldFollow, false);
  state.observe(500, 400, 1000);
  state.endGesture();
  assert.equal(state.shouldFollow, false);
  state.observe(500, 400, 1200);
  assert.equal(state.shouldFollow, false);
});

test('returning to the bottom resumes only after the gesture ends', () => {
  const state = new StreamScrollFollow();
  state.beginGesture();
  state.observe(200, 400, 1000);
  state.observe(580, 400, 1000);
  assert.equal(state.shouldFollow, false);
  state.endGesture();
  assert.equal(state.shouldFollow, true);
});

test('momentum and negative bounce offsets do not steal the reader position', () => {
  const state = new StreamScrollFollow();
  state.beginGesture(); state.observe(200, 400, 1000); state.endGesture();
  state.beginGesture(); state.observe(-20, 400, 1000); state.endGesture();
  assert.equal(state.shouldFollow, false);
  state.resume();
  assert.equal(state.shouldFollow, true);
  state.beginGesture(); state.reset();
  assert.equal(state.shouldFollow, true);
});
