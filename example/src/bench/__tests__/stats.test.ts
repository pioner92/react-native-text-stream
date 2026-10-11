import { latencyStats, median, percentile, type Point } from '../stats';

describe('median', () => {
  it('averages the two middle values of an even count', () => {
    expect(median([10, 30])).toBe(20);
    expect(median([4, 1, 3, 2])).toBe(2.5);
  });

  it('takes the middle value of an odd count, ignoring nulls', () => {
    expect(median([5, null, 1, 3])).toBe(3);
    expect(median([null])).toBeNull();
  });
});

describe('percentile', () => {
  it('interpolates between ranks', () => {
    const values = Array.from({ length: 20 }, (_, i) => i + 1); // 1…20
    expect(percentile(values, 0)).toBe(1);
    expect(percentile(values, 1)).toBe(20);
    expect(percentile(values, 0.95)).toBeCloseTo(19.05);
  });
});

describe('latencyStats', () => {
  const sent: Point[] = [
    [0, 100],
    [10, 200],
    [20, 300],
  ];

  it('measures every byte of a run that was fully shown', () => {
    const shown: Point[] = [
      [5, 100],
      [18, 200],
      [40, 300],
    ];
    expect(latencyStats(sent, shown, 300)).toEqual({
      complete: true,
      ttfgMs: 5,
      completionMs: 40,
      latP50Ms: 8,
      latP95Ms: expect.closeTo(18.8),
      latMaxMs: 20,
    });
  });

  it('reports no latencies for a run whose tail was never shown', () => {
    // Two fast bytes and a tail that never arrives: percentiles over what was shown would look good.
    const shown: Point[] = [
      [1, 100],
      [11, 200],
    ];
    expect(latencyStats(sent, shown, 300)).toEqual({
      complete: false,
      ttfgMs: 1,
      completionMs: null,
      latP50Ms: null,
      latP95Ms: null,
      latMaxMs: null,
    });
  });
});
