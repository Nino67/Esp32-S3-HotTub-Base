import test from 'node:test';
import assert from 'node:assert/strict';
import { createChartManager, createSampleTracker } from '../littlefs/www/js/chart_manager.js';

const start = 1700000000000;

test('tracks received samples and detects sequence and timing gaps', () => {
  const tracker = createSampleTracker();
  assert.deepEqual(tracker.accept({ sample_seq: 10, sample_time_ms: start }, start), {
    timestamp: start / 1000, previousTimestamp: null, gap: false, reset: false,
  });
  assert.equal(tracker.accept({ sample_seq: 10, sample_time_ms: start }, start + 1000), null);
  assert.equal(tracker.accept({ sample_seq: 11, sample_time_ms: start + 1000 }, start + 1000).gap, false);
  assert.equal(tracker.accept({ sample_seq: 13, sample_time_ms: start + 2000 }, start + 2000).gap, true);
  assert.equal(tracker.accept({ sample_seq: 14, sample_time_ms: start + 7000 }, start + 7000).gap, true);
  tracker.disconnect();
  assert.equal(tracker.accept({ sample_seq: 15, sample_time_ms: start + 8000 }, start + 8000).gap, true);
});

test('handles unsynchronized clock, sequence wrap, and backward clock changes', () => {
  const tracker = createSampleTracker();
  tracker.disconnect();
  assert.equal(tracker.accept({ sample_seq: 0xffffffff, sample_time_ms: 0 }, start).gap, false);
  assert.equal(tracker.accept({ sample_seq: 0, sample_time_ms: 0 }, start + 1000).gap, false);
  assert.equal(tracker.accept({ sample_seq: 1, sample_time_ms: start - 1000 }, start + 2000).reset, true);
});

test('draws explicit null gaps without turning null into zero', () => {
  let chart;
  globalThis.window = {
    addEventListener() {},
    uPlot: class {
      constructor(options, data) {
        this.options = options;
        this.data = data;
        this.root = { style: {} };
        chart = this;
      }
      setData(data) { this.data = data; }
      setSize() {}
    },
  };
  globalThis.requestAnimationFrame = () => {};
  const manager = createChartManager();
  manager.createChart({
    id: 'test',
    container: { parentElement: null, clientWidth: 300, clientHeight: 200 },
    title: '',
    seriesLabels: ['waterTemp'],
  });
  manager.addPoint('test', 1, { waterTemp: 25 });
  manager.addPoint('test', 2, {});
  manager.addPoint('test', 3, { waterTemp: 26 });
  assert.equal(chart.options.series[1].spanGaps, false);
  assert.deepEqual(chart.data, [[1, 2, 3], [25, null, 26]]);
  manager.clear('test');
  assert.deepEqual(chart.data, [[], []]);
});
