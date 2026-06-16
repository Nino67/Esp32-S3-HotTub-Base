const badge = document.getElementById('connBadge');
const stateView = document.getElementById('stateView');
const commandInput = document.getElementById('commandInput');
const sendBtn = document.getElementById('sendBtn');

let socket;

function setBadge(text, status) {
  badge.textContent = text;
  badge.dataset.status = status;
}

function connect() {
  const proto = window.location.protocol === 'https:' ? 'wss' : 'ws';
  socket = new WebSocket(`${proto}://${window.location.host}/ws`);

  socket.addEventListener('open', () => {
    setBadge('connected', 'ok');
    socket.send(JSON.stringify({ command: 'get_state' }));
  });

  socket.addEventListener('close', () => {
    setBadge('reconnecting', 'warn');
    window.setTimeout(connect, 1500);
  });

  socket.addEventListener('message', (event) => {
    stateView.textContent = event.data;
  });

  socket.addEventListener('error', () => {
    setBadge('error', 'bad');
  });
}

sendBtn.addEventListener('click', () => {
  if (socket && socket.readyState === WebSocket.OPEN) {
    socket.send(commandInput.value);
  }
});

connect();
