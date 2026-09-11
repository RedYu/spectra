from pathlib import Path
import gzip
import shutil

import htmlmin
import rcssmin
import rjsmin


SCRIPT_DIR = Path(__file__).resolve().parent
ROOT = SCRIPT_DIR.parent

SRC_DIR = ROOT / "spiffs_data" / "www"
DIST_DIR = SRC_DIR / "dist"

SUPPORTED_EXTENSIONS = {
    ".html",
    ".css",
    ".js",
}


def format_size(size: int) -> str:
    if size < 1024:
        return f"{size} B"

    return f"{size / 1024:.2f} KB"


def log_result(
    filename: str,
    original_size: int,
    final_size: int,
) -> None:
    saved = original_size - final_size

    percent = (
        saved / original_size * 100
        if original_size > 0
        else 0
    )

    print(
        f"{filename}: "
        f"{format_size(original_size)} -> "
        f"{format_size(final_size)} "
        f"({percent:.1f}% smaller)"
    )


def minify_content(
    source: str,
    extension: str,
) -> str:

    if extension == ".html":
        return htmlmin.minify(
            source,
            remove_comments=True,
            remove_empty_space=True,
            reduce_empty_attributes=True,
        )

    if extension == ".css":
        return rcssmin.cssmin(source)

    if extension == ".js":
        return rjsmin.jsmin(source)

    raise ValueError(
        f"Unsupported extension: {extension}"
    )


def minify_file(
    source_path: Path,
    output_path: Path,
) -> None:

    source = source_path.read_text(
        encoding="utf-8"
    )

    result = minify_content(
        source,
        source_path.suffix.lower(),
    )

    with output_path.open(
        "w",
        encoding="utf-8",
        newline="\n",
    ) as file:
        file.write(result)

    log_result(
        source_path.name,
        len(source.encode("utf-8")),
        len(result.encode("utf-8")),
    )


def create_gzip(
    source_path: Path,
) -> None:

    gzip_path = Path(
        str(source_path) + ".gz"
    )

    with source_path.open("rb") as source_file:
        with gzip.open(
            gzip_path,
            "wb",
            compresslevel=9,
        ) as gzip_file:

            shutil.copyfileobj(
                source_file,
                gzip_file
            )

    log_result(
        gzip_path.name,
        source_path.stat().st_size,
        gzip_path.stat().st_size,
    )


def directory_size(
    directory: Path,
) -> int:
    total = 0

    for file_path in directory.rglob("*"):
        if file_path.is_file():
            total += file_path.stat().st_size

    return total


def source_directory_size() -> int:
    total = 0

    for file_path in SRC_DIR.iterdir():
        if file_path.is_file():
            total += file_path.stat().st_size

    return total


def main() -> None:
    print("Building Spectra web resources...\n")

    print(f"Source: {SRC_DIR}")
    print(f"Output: {DIST_DIR}\n")

    if not SRC_DIR.exists():
        raise FileNotFoundError(
            f"Source directory does not exist: {SRC_DIR}"
        )

    source_size = source_directory_size()

    if DIST_DIR.exists():
        shutil.rmtree(DIST_DIR)

    DIST_DIR.mkdir(
        parents=True,
        exist_ok=True
    )

    processed = 0

    for source_path in SRC_DIR.iterdir():
        if not source_path.is_file():
            continue

        if source_path.suffix.lower() not in SUPPORTED_EXTENSIONS:
            continue

        output_path = (
            DIST_DIR /
            source_path.name
        )

        minify_file(
            source_path,
            output_path,
        )

        create_gzip(
            output_path
        )

        processed += 1

    for source_path in SRC_DIR.iterdir():
        if not source_path.is_file():
            continue

        if source_path.suffix.lower() in SUPPORTED_EXTENSIONS:
            continue

        shutil.copy2(
            source_path,
            DIST_DIR / source_path.name
        )

        print(
            f"Copied: {source_path.name}"
        )

    dist_size = directory_size(DIST_DIR)

    gzip_size = sum(
        file_path.stat().st_size
        for file_path in DIST_DIR.glob("*.gz")
    )

    print(
        f"\nSource total: {format_size(source_size)}"
    )

    print(
        f"Dist total:   {format_size(dist_size)}"
    )

    print(
        f"Gzip total:   {format_size(gzip_size)}"
    )

    print(
        f"\nProcessed {processed} web resource(s)."
    )

    print(
        "Web resources built successfully."
    )


if __name__ == "__main__":
    main()
