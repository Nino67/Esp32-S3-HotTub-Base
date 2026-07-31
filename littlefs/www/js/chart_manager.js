/**
 * @file chart_manager.js
 * @brief Simple manager for multiple uPlot charts.
 */

const COLOR_PALETTE = ['#4caf50', '#2196f3', '#ff9800', '#9c27b0', '#f44336'];

function getSeriesOptions(seriesLabels) {
  return seriesLabels.map((label, index) => ({
    label,
    stroke: COLOR_PALETTE[index % COLOR_PALETTE.length],
    width: 2,
    spanGaps: true,
  }));
}

function createChartOptions(title, seriesLabels, width, height) {
  return {
    title,
    width,
    height,
    scales: {
      x: {
        time: true,
      },
    },
    series: [
      {
        value: (u, v) => new Date(v * 1000).toLocaleTimeString(),
        label: 'Time',
      },
      ...getSeriesOptions(seriesLabels),
    ],
    axes: [
      {
        stroke: '#888',
        grid: {
          show: true,
          stroke: '#eee',
        },
      },
      {
        stroke: '#888',
        grid: {
          show: true,
          stroke: '#eee',
        },
      },
    ],
    legend: {
      show: true,
      live: true,
    },
  };
}

function getContainerSize(container) {
  const width = Math.max(container.clientWidth, 360);
  const height = Math.max(container.clientHeight, 320);
  return { width, height };
}

function createDataBuffer(seriesCount) {
  return Array.from({ length: seriesCount + 1 }, () => []);
}

export function createChartManager() {
  const charts = new Map();

  function createChart({ id, container, title, seriesLabels, maxPoints = 60 }) {
    if (!container) {
      throw new Error('Chart container element is required');
    }
    if (!window.uPlot) {
      throw new Error('uPlot is not available on the window object');
    }
    if (charts.has(id)) {
      throw new Error(`Chart with id '${id}' already exists`);
    }

    const data = createDataBuffer(seriesLabels.length);
    const { width, height } = getContainerSize(container);
    const opts = createChartOptions(title, seriesLabels, width, height);
    const chart = new window.uPlot(opts, data, container);

    // Ensure chart root fills the container after initial render.
    requestAnimationFrame(() => {
      const rect = container.getBoundingClientRect();
      chart.setSize({
        width: Math.max(rect.width, 360),
        height: Math.max(rect.height, 320),
      });
    });

    const entry = { chart, data, seriesLabels, maxPoints };
    if (typeof window.ResizeObserver === 'function') {
      entry.resizeObserver = new ResizeObserver((entries) => {
        for (const entryItem of entries) {
          if (entryItem.target === container) {
            const rect = entryItem.contentRect;
            chart.setSize({
              width: Math.max(rect.width, 360),
              height: Math.max(rect.height, 320),
            });
          }
        }
      });
      entry.resizeObserver.observe(container);
    }

    charts.set(id, entry);
    return id;
  }

  function addPoint(id, x, valueMap) {
    const entry = charts.get(id);
    if (!entry) {
      throw new Error(`Chart with id '${id}' not found`);
    }

    const { chart, data, seriesLabels, maxPoints } = entry;
    data[0].push(x);

    for (let index = 0; index < seriesLabels.length; index += 1) {
      const label = seriesLabels[index];
      const value = valueMap[label];
      data[index + 1].push(value == null ? null : value);
    }

    while (data[0].length > maxPoints) {
      data.forEach((seriesArray) => seriesArray.shift());
    }

    chart.setData(data);
  }

  return {
    createChart,
    addPoint,
  };
}
