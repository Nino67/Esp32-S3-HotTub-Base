

const badge = document.getElementById('connBadge');
const stateView = document.getElementById('stateView');
const commandInput = document.getElementById('commandInput');
const sendBtn = document.getElementById('sendBtn');

let socket;

function setBadge(text, status) {
  badge.textContent = text;
  badge.dataset.status = status;
}

function hardwareInit() {
  setBadge('initializing', 'warn');

  // add event listener for button 'sendBtn' to send command to the server
  sendBtn.addEventListener('click', () => {
    if (socket && socket.readyState === WebSocket.OPEN) {
      // crc32_json_wrapper(JSON.parse(commandInput.value), commandInput.value, 1024, null); 
      socket.send(commandInput.value);
      console.log('Sending:', commandInput.value);
    }
  });

  // connect to the server
  connect();

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
    console.log('Received:', event.data);
  });

  socket.addEventListener('error', () => {
    setBadge('error', 'bad');
  });
}

hardwareInit();