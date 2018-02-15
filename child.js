const {RawBuffer: {RawBuffer}} = imports;
const smiggles = require('smiggles');

smiggles.bind({RawBuffer});

const rawBufferSymbol = Symbol();

onthreadmessage = arrayBuffer => {
  const rawBuffer = new RawBuffer(arrayBuffer); // internalize

  if (onmessage !== null) {
    const m = smiggles.deserialize(arrayBuffer);
    if (typeof m === 'object' && m !== null) {
      m[rawBufferSymbol] = rawBuffer; // bind storage lifetime
    }
    onmessage(m);
  }
};
onmessage = null;
postMessage = (m, transferList) => {
  const arrayBuffer = smiggles.serialize(m, transferList);
  new RawBuffer(arrayBuffer).toAddress(); // externalize
  postThreadMessage(arrayBuffer);
};

// user code

require(process.argv[2]);