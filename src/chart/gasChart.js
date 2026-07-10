const { ChartJSNodeCanvas } = require('chartjs-node-canvas');

const GAS_THRESHOLD = parseInt(process.env.GAS_THRESHOLD || '400', 10);

const WIDTH  = 600;
const HEIGHT = 300;

const renderer = new ChartJSNodeCanvas({ width: WIDTH, height: HEIGHT, backgroundColour: '#1a1a2e' });

/**
 * Generates a gas level line chart as a PNG buffer.
 * @param {Array<{gas_level: number, created_at: Date}>} readings
 * @returns {Promise<Buffer>}
 */
async function generateGasChart(readings) {
  const labels = readings.map(r => {
    const d = new Date(r.created_at);
    return d.toLocaleTimeString('id-ID', { timeZone: 'Asia/Jakarta', hour: '2-digit', minute: '2-digit', second: '2-digit' });
  });

  const data = readings.map(r => r.gas_level);

  const config = {
    type: 'line',
    data: {
      labels,
      datasets: [
        {
          label: 'Level Gas',
          data,
          borderColor: '#00d4ff',
          backgroundColor: 'rgba(0, 212, 255, 0.1)',
          borderWidth: 2,
          pointRadius: 3,
          pointBackgroundColor: '#00d4ff',
          fill: true,
          tension: 0.3
        },
        {
          label: `Ambang Bahaya (${GAS_THRESHOLD})`,
          data: new Array(readings.length).fill(GAS_THRESHOLD),
          borderColor: '#ff4444',
          borderWidth: 1.5,
          borderDash: [6, 3],
          pointRadius: 0,
          fill: false
        }
      ]
    },
    options: {
      responsive: false,
      plugins: {
        legend: {
          labels: { color: '#e0e0e0', font: { size: 11 } }
        },
        title: {
          display: true,
          text: '📊 Riwayat Level Gas (30 Data Terakhir)',
          color: '#ffffff',
          font: { size: 14, weight: 'bold' }
        }
      },
      scales: {
        x: {
          ticks: { color: '#aaaaaa', font: { size: 9 }, maxRotation: 45 },
          grid:  { color: 'rgba(255,255,255,0.08)' }
        },
        y: {
          ticks: { color: '#aaaaaa', font: { size: 10 } },
          grid:  { color: 'rgba(255,255,255,0.08)' },
          title: { display: true, text: 'Level Gas (ADC)', color: '#aaaaaa' }
        }
      }
    }
  };

  return renderer.renderToBuffer(config);
}

module.exports = { generateGasChart };
