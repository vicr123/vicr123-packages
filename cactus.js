const util = require('util');
const fs = require('fs');
const { spawn } = require('child_process');
const execFile = util.promisify(require('child_process').execFile);

// tar -xO --file=cactus-recovery-media-20201206-1-x86_64.tar opt/cactus-recovery-media/rootfs.squashfs

module.exports = (config) => async (/** @type {Express.Request} */ req, /** @type {Express.Response} */ res, next) => {
    let pkgfile = `${config.rootPath}/cactus/${req.params.arch}/cactus-recovery-media-${req.params.rootfile}-1-${req.params.arch}.tar`;

    //Sanitize user input
    if (!["x86_64"].includes(req.params.arch) || !(req.params.rootfile == "latest" || /^\d{6}$/.test(req.params.rootfile))) {
        next();
        return;
    }

    if (req.params.rootfile == "latest") {
        //Discover the latest rootfs package
        let { stdout: tarLs } = await execFile('tar', ['-tv', '--wildcards', `--file=${config.rootPath}/cactus/${req.params.arch}/cactus-core.db.tar.gz`, "cactus-recovery-media-????????-?/", "--exclude", "desc"]);
        let filename = tarLs.split(" ").filter(part => part !== "")[5];
        filename = filename.substr(0, filename.length - 2); //Remove the trailing /
        pkgfile = `${config.rootPath}/cactus/${req.params.arch}/${filename}-${req.params.arch}.tar`;
    }

    if (fs.existsSync(pkgfile)) {
        //Find out how big the package file is
        let { stdout: tarLs } = await execFile('tar', ['-tv', `--file=${pkgfile}`, 'opt/cactus-recovery-media/rootfs.squashfs']);

        let size = tarLs.split(" ")[2];
        res.set('Content-Length', size);

        //Send over the package file
        const tar = spawn('tar', ['-xO', `--file=${pkgfile}`, 'opt/cactus-recovery-media/rootfs.squashfs']);
        tar.stdout.pipe(res, {
            end: false
        });
        tar.on('close', (code) => {
            res.end();
        });
    } else {
        next();
    }
}