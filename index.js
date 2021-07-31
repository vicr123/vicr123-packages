const express = require('express');
const serveIndex = require('serve-index');
const process = require('process');
const fs = require('fs');

let config = JSON.parse(fs.readFileSync("config.json"));

const cactusDetect = require('./cactus')(config);

let app = express();
app.use('/cactus/rootfs/:arch/:rootfile.squashfs', cactusDetect("opt/cactus-recovery-media/rootfs.squashfs"));
app.use('/cactus/rootfs/:arch/:rootfile.squashfs.sig', cactusDetect("opt/cactus-recovery-media/rootfs.squashfs.sig"));
app.use('/', express.static(config.rootPath));
app.use('/', serveIndex(config.rootPath, {
    stylesheet: "directory.css",
    template: require('./directory.js')
}));

let port = config.port;
if (process.argv.length > 2) {
    port = parseInt(process.argv[2]);
}
app.listen(port, function() {
    console.log(`Listening on port ${port}`);
});
