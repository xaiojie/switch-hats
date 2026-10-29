// Windows may assign a loopback port that Chromium reserves (for example 10080).
// Keep browser fixtures above its reserved low ports without changing browser security.
async function listenForBrowser(server, host='127.0.0.1') {
  for (let attempt=0;attempt<64;attempt++) {
    try {
      await new Promise((resolve,reject)=>{
        const failed=error=>{server.off('listening',ready);reject(error);};
        const ready=()=>{server.off('error',failed);resolve();};
        server.once('error',failed);server.once('listening',ready);
        server.listen(30000+require('node:crypto').randomInt(20000),host);
      });
      return;
    } catch(error) { if(error.code!=='EADDRINUSE')throw error; }
  }
  throw Error('Unable to allocate a browser-safe loopback port');
}
module.exports={listenForBrowser};
