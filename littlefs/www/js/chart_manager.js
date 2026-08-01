/**
 * @file chart_manager.js
 * @brief Simple manager for multiple uPlot charts.
 */

const COLOR_PALETTE = ['#4caf50', '#2196f3', '#ff9800', '#9c27b0', '#f44336'];

function toFiniteNumber(value) {
  const num = typeof value === 'number' ? value : Number(value);
  return Number.isFinite(num) ? num : null;
}

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
    class: 'hot-tub-uplot',
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
        value: (u, v) => (Number.isFinite(v) ? new Date(v * 1000).toLocaleTimeString() : '--:--:--'),
        label: 'Time',
      },
      ...getSeriesOptions(seriesLabels),
    ],
    axes: [
      {
        space: 56,
        stroke: '#888',
        grid: {
          show: true,
          stroke: '#666',
        },
      },
      {
        size: 58,
        stroke: '#888',
        grid: {
          show: true,
          stroke: '#666',
        },
        values: (u, splits) => splits.map((value) => `${value.toFixed(1)} C`),
      },
    ],
    legend: {
      show: false,
      live: true,
    },
  };
}

function createTopLegend(container, seriesLabels) {
  const parent = container.parentElement;
  if (!parent) {
    return null;
  }

  const existing = parent.querySelector('.chart-top-legend');
  if (existing) {
    existing.remove();
  }

  const legend = document.createElement('div');
  legend.className = 'chart-top-legend';

  seriesLabels.forEach((label, index) => {
    const item = document.createElement('span');
    item.className = 'chart-top-legend__item';

    const swatch = document.createElement('span');
    swatch.className = 'chart-top-legend__swatch';
    swatch.style.backgroundColor = COLOR_PALETTE[index % COLOR_PALETTE.length];

    const text = document.createElement('span');
    text.className = 'chart-top-legend__label';
    text.textContent = label;

    item.appendChild(swatch);
    item.appendChild(text);
    legend.appendChild(item);
  });

  parent.insertBefore(legend, container);
  return legend;
}

function getContainerSize(container) {
  const width = Math.max(container.clientWidth, 220);
  const height = Math.max(container.clientHeight, 200);
  return { width, height };
}

function createDataBuffer(seriesCount) {
  return Array.from({ length: seriesCount + 1 }, () => []);
}

function getSafeSizeFromRect(rect) {
  const width = Math.max(Math.floor(rect.width || 0), 220);
  const height = Math.max(Math.floor(rect.height || 0), 200);
  return { width, height };
}

export function createChartManager() {
  const charts = new Map();

  function resizeAllCharts() {
    charts.forEach((entry) => {
      const nextSize = getContainerSize(entry.container);
      if (entry.lastSize.width === nextSize.width && entry.lastSize.height === nextSize.height) {
        return;
      }

      entry.lastSize = nextSize;
      entry.chart.setSize(nextSize);
      entry.chart.root.style.width = `${nextSize.width}px`;
      entry.chart.root.style.height = `${nextSize.height}px`;
    });
  }

  window.addEventListener('resize', resizeAllCharts);
  window.addEventListener('orientationchange', resizeAllCharts);

  function createChart({ id, container, title, seriesLabels, maxPoints = 100 }) {
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
    const topLegend = createTopLegend(container, seriesLabels);
    const opts = createChartOptions(title, seriesLabels, width, height);
    const chart = new window.uPlot(opts, data, container);

    const entry = { chart, container, data, seriesLabels, maxPoints, lastSize: { width, height }, topLegend };

    function applySize(nextSize) {
      const prev = entry.lastSize;
      if (prev.width === nextSize.width && prev.height === nextSize.height) {
        return;
      }

      entry.lastSize = nextSize;
      chart.setSize(nextSize);
      chart.root.style.width = `${nextSize.width}px`;
      chart.root.style.height = `${nextSize.height}px`;
    }

    if (typeof window.ResizeObserver === 'function') {
      entry.resizeObserver = new ResizeObserver((entries) => {
        for (const entryItem of entries) {
          if (entryItem.target === container) {
            applySize(getSafeSizeFromRect(entryItem.contentRect));
          }
        }
      });
      entry.resizeObserver.observe(container);
    }

    // Ensure chart root matches final layout after initial paint.
    requestAnimationFrame(() => {
      applySize(getContainerSize(container));
    });

    charts.set(id, entry);
    return id;
  }

  function addPoint(id, x, valueMap) {
    const entry = charts.get(id);
    if (!entry) {
      throw new Error(`Chart with id '${id}' not found`);
    }

    const { chart, data, seriesLabels, maxPoints } = entry;
    const xValue = toFiniteNumber(x);
    if (xValue == null) {
      return;
    }

    data[0].push(xValue);

    for (let index = 0; index < seriesLabels.length; index += 1) {
      const label = seriesLabels[index];
      data[index + 1].push(toFiniteNumber(valueMap[label]));
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
