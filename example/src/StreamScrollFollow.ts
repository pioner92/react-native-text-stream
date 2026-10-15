/** Small state machine: no React updates or allocations for incoming stream chunks. */
export class StreamScrollFollow {
  following = true;
  interacting = false;

  get shouldFollow() { return this.following && !this.interacting; }

  reset() { this.following = true; this.interacting = false; }

  beginGesture() { this.interacting = true; this.following = false; }

  observe(y: number, viewport: number, content: number) {
    // Programmatic scroll events must not detach an active follower as content grows.
    if (this.interacting || !this.following) {
      this.following = content - viewport - Math.max(0, y) <= 32;
    }
  }

  endGesture() { this.interacting = false; }

  resume() { this.following = true; this.interacting = false; }
}
