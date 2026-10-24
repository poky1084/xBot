let ws;
let isConnected = false;
const RECONNECT_INTERVAL = 5000;

function connect() {
  ws = new WebSocket('ws://localhost:4444');

  // Add connection handler
  ws.onopen = () => {
    console.log('WebSocket connected');
    isConnected = true;
  };

  // Add message handler
  ws.onmessage = async (event) => {
    let message;
    try {
        message = JSON.parse(event.data);
    } catch (error) {
        //console.error('Error parsing message:', error);
        return;
    }

    //console.log('Received message:', message);

    if (!isConnected || ws.readyState !== WebSocket.OPEN) {
      //console.error('WebSocket is not connected, cannot send response');
      return;
    }

    const response = {
      id: message.id,
      response: await sendRequest(message.payload.url, message.payload.options)
    };
    ws.send(JSON.stringify(response));
  };

  // Optional: Add error handler
  ws.onerror = (error) => {
    //console.error('WebSocket error:', error);
    isConnected = false;
  };

  // Add close handler
  ws.onclose = () => {
    console.log('Originals WebSocket disconnected');
    isConnected = false;
    // Schedule reconnection
    setTimeout(connect, RECONNECT_INTERVAL);
  };
}

// Initial connection
connect();

async function sendRequest(url, options) {
  try {
	  if (options.body && typeof options.body === 'object') {
      options.body = JSON.stringify(options.body);
    }
    const response = await fetchPlus(url, options);
    // Check if response is ok (status in 200-299 range)
    if (!response.ok) {
      throw new Error(`HTTP error! status: ${response.status}`);
    }
    
    // Parse and return the JSON response
    const data = await response.json();
    return data;
  } catch (error) {
    //console.error('Request failed:', error);
    throw error; // Re-throw to let caller handle the error
  }
}

function fetchPlus(url, options = {}, retries = 5) {
    return fetch(url, {
        signal: AbortSignal.timeout(5000),
        ...options,
        redirect: 'follow'
    })
        .then(res => {
            if (res.ok) {
                return res
            }

            throw new Error(`Request error: ${res.status}`)
        }).catch(async error => {
            if (['AbortError', 'TimeoutError'].includes(error.name) && retries > 0) {
                console.log(`Timeout error: Retrying fetch. ${retries} retries left.`, error.name)
                await new Promise(res => setTimeout(res, 500))
                return fetchPlus(url, options, retries - 1)
            }

            if (retries > 0) {
                console.log(`Network error: Retrying fetch. ${retries} retries left.`)
                await new Promise(res => setTimeout(res, 500))
                return fetchPlus(url, options, retries - 1)
            }

            throw new Error(error.message)
        })
}

