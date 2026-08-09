const FILE_LIST_LIMIT = 16;

const volumeSelector =
    document.getElementById("volume");

const currentPathElement =
    document.getElementById("current-path");

const fileList =
    document.getElementById("file-list");

const statusElement =
    document.getElementById("status");

const backButton =
    document.getElementById("back");

const previousPageButton =
    document.getElementById("previous-page");

const nextPageButton =
    document.getElementById("next-page");

const pageStatusElement =
    document.getElementById("page-status");

let currentPath = "/";
let currentOffset = 0;
let hasMore = false;
let activeRequest = null;

function setStatus(
    message,
    error = false
) {
    statusElement.textContent =
        message;

    statusElement.classList.toggle(
        "error",
        error
    );
}

function joinPath(
    base,
    name
) {
    if (base === "/") {
        return `/${name}`;
    }

    return `${base}/${name}`;
}

function getParentPath(path) {
    if (path === "/") {
        return "/";
    }

    const parts =
        path
            .split("/")
            .filter(Boolean);

    parts.pop();

    return parts.length === 0
        ? "/"
        : `/${parts.join("/")}`;
}

function formatSize(size) {
    if (!Number.isFinite(size) ||
        size < 0) {

        return "-";
    }

    if (size < 1024) {
        return `${size} B`;
    }

    if (size < 1024 * 1024) {
        return `${(
            size / 1024
        ).toFixed(1)} KB`;
    }

    if (size < 1024 * 1024 * 1024) {
        return `${(
            size /
            (1024 * 1024)
        ).toFixed(1)} MB`;
    }

    return `${(
        size /
        (1024 * 1024 * 1024)
    ).toFixed(1)} GB`;
}

function updateNavigation() {
    backButton.disabled =
        currentPath === "/";

    previousPageButton.disabled =
        currentOffset === 0;

    nextPageButton.disabled =
        !hasMore;

    const entryCount =
        fileList.children.length;

    if (entryCount === 0) {
        pageStatusElement.textContent =
            "No entries";

        return;
    }

    const firstEntry =
        currentOffset + 1;

    const lastEntry =
        currentOffset +
        entryCount;

    pageStatusElement.textContent =
        `${firstEntry}-${lastEntry}`;
}

function createFileRow(
    volume,
    entry
) {
    const row =
        document.createElement("tr");

    const nameCell =
        document.createElement("td");

    const typeCell =
        document.createElement("td");

    const sizeCell =
        document.createElement("td");

    const actionCell =
        document.createElement("td");

    nameCell.textContent =
        entry.name;

    nameCell.title =
        entry.name;

    typeCell.textContent =
        entry.type;

    sizeCell.textContent =
        entry.type === "directory"
            ? "-"
            : formatSize(entry.size);

    const button =
        document.createElement("button");

    button.type = "button";

    if (entry.type === "directory") {
        button.textContent =
            "Open";

        button.addEventListener(
            "click",
            () => {
                currentPath =
                    joinPath(
                        currentPath,
                        entry.name
                    );

                currentOffset = 0;
                hasMore = false;

                loadFiles();
            }
        );

    } else {
        button.textContent =
            "Download";

        button.addEventListener(
            "click",
            () => {
                const filePath =
                    joinPath(
                        currentPath,
                        entry.name
                    );

                const query =
                    new URLSearchParams({
                        volume,
                        path: filePath
                    });

                window.location.href =
                    `/api/files/download?` +
                    query.toString();
            }
        );
    }

    actionCell.appendChild(
        button
    );

    row.append(
        nameCell,
        typeCell,
        sizeCell,
        actionCell
    );

    return row;
}

async function loadFiles() {
    if (activeRequest !== null) {
        activeRequest.abort();
    }

    const request =
        new AbortController();

    activeRequest = request;

    const volume =
        volumeSelector.value;

    setStatus(
        "Loading..."
    );

    backButton.disabled = true;
    previousPageButton.disabled = true;
    nextPageButton.disabled = true;

    try {
        const query =
            new URLSearchParams({
                volume,
                path: currentPath,
                offset: String(currentOffset),
                limit: String(FILE_LIST_LIMIT)
            });

        const response =
            await fetch(
                `/api/files?${query.toString()}`,
                {
                    cache: "no-store",
                    signal: request.signal
                }
            );

        let result;

        try {
            result =
                await response.json();

        } catch {
            throw new Error(
                `Invalid server response: HTTP ${response.status}`
            );
        }

        if (!response.ok) {
            throw new Error(
                result.message ||
                `HTTP ${response.status}`
            );
        }

        if ((typeof result.path !== "string") ||
            !Array.isArray(result.entries)) {

            throw new Error(
                "Invalid file-list response"
            );
        }

        /*
         * Ignore a response belonging to an older request.
         */
        if (activeRequest !== request) {
            return;
        }

        fileList.replaceChildren();

        currentPath =
            result.path;

        currentPathElement.textContent =
            currentPath;

        currentPathElement.title =
            currentPath;

        hasMore =
            result.has_more === true;

        for (const entry of result.entries) {
            if ((typeof entry !== "object") ||
                (entry === null) ||
                (typeof entry.name !== "string") ||
                ((entry.type !== "file") &&
                 (entry.type !== "directory"))) {

                continue;
            }

            const row =
                createFileRow(
                    volume,
                    entry
                );

            fileList.appendChild(
                row
            );
        }

        const count =
            fileList.children.length;

        setStatus(
            count === 1
                ? "1 entry"
                : `${count} entries`
        );

        updateNavigation();

    } catch (error) {
        if (error.name === "AbortError") {
            return;
        }

        if (activeRequest === request) {
            fileList.replaceChildren();

            hasMore = false;

            const message =
                error instanceof Error
                    ? error.message
                    : String(error);

            setStatus(
                `Failed to load files: ${message}`,
                true
            );

            updateNavigation();
        }

    } finally {
        if (activeRequest === request) {
            activeRequest = null;
        }
    }
}

backButton.addEventListener(
    "click",
    () => {
        if (currentPath === "/") {
            return;
        }

        currentPath =
            getParentPath(
                currentPath
            );

        currentOffset = 0;
        hasMore = false;

        loadFiles();
    }
);

previousPageButton.addEventListener(
    "click",
    () => {
        if (currentOffset === 0) {
            return;
        }

        currentOffset =
            currentOffset >= FILE_LIST_LIMIT
                ? currentOffset -
                  FILE_LIST_LIMIT
                : 0;

        hasMore = false;

        loadFiles();
    }
);

nextPageButton.addEventListener(
    "click",
    () => {
        if (!hasMore) {
            return;
        }

        currentOffset +=
            FILE_LIST_LIMIT;

        hasMore = false;

        loadFiles();
    }
);

volumeSelector.addEventListener(
    "change",
    () => {
        currentPath = "/";
        currentOffset = 0;
        hasMore = false;

        currentPathElement.textContent =
            currentPath;

        currentPathElement.title =
            currentPath;

        loadFiles();
    }
);

loadFiles();
