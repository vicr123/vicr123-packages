module.exports = (locals, callback) => {
    let parts = [
        `<html><head><style>${locals.style}</style></head><body>`
    ];

    parts.push(`<div class="header">vicr123 Packages</div><hr />`);
    parts.push(`<div class="dirContainer">`);
    parts.push(`<a href="/"><div class="dirPart">/</div></a>`);
    let currentLink = "/";
    for (let part of locals.directory.split("/").filter(part => part !== "")) {
        currentLink += part + "/";
        parts.push(`<a href="${currentLink}"><div class="dirPart">${part}</div></a>`)
    }
    parts.push(`</div><hr />`);

    //Generate the body
    for (let file of locals.fileList) {
        if (file.name === "..") continue;
        parts.push(`<a href="${file.name}"><div class="fileEntry">${file.name}</div></a>`);
    }

    parts.push("<hr /><footer>vicr123 Packages</footer></body></html>")
    callback(null, parts.join(""));
}