const fs = require('fs');
const { spawn, execSync } = require('child_process');

// tar -xO --file=cactus-recovery-media-20201206-1-x86_64.tar opt/cactus-recovery-media/rootfs.squashfs

module.exports = (config) => (/** @type {Express.Request} */ req, /** @type {Express.Response} */ res, next) => {
    console.log("Cactus!" + req.params.rootfile);
    let pkgfile = `${config.rootPath}/cactus/${req.params.arch}/cactus-recovery-media-${req.params.rootfile}-1-${req.params.arch}.tar`;
    if (fs.existsSync(pkgfile)) {
        //Find out how big the package file is
        const tarLs = spawn('tar', ['-tvO', `--file=${pkgfile}`, 'opt/cactus-recovery-media/rootfs.squashfs']);
        tarLs.stdout.setEncoding("utf-8");
        tarLs.on('close', (code) => {
            let size = tarLs.stdout.read().toString().split(" ")[2];
            console.log("size: " + size);
            res.set('Content-Length', size);

            //Send over the package file
            const tar = spawn('tar', ['-xO', `--file=${pkgfile}`, 'opt/cactus-recovery-media/rootfs.squashfs']);
            tar.stdout.pipe(res, {
                end: false
            });
            tar.on('close', (code) => {
                res.end();
            });
        });
    } else {
        next();
    }
}