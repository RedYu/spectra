from argparse import ArgumentParser
from pathlib import Path
import gzip
import shutil


SCRIPT_DIR = Path(__file__).resolve().parent
ROOT = SCRIPT_DIR.parent

SRC_DIR = ROOT / "web_src"
DIST_DIR = ROOT / "spiffs_data" / "www"

MINIFIED_EXTENSIONS = {
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
        import htmlmin

        return htmlmin.minify(
            source,
            remove_comments=True,
            remove_empty_space=True,
            reduce_empty_attributes=True,
        )

    if extension == ".css":
        import rcssmin

        return rcssmin.cssmin(source)

    if extension == ".js":
        import rjsmin

        return rjsmin.jsmin(source)

    raise ValueError(
        f"Unsupported extension: {extension}"
    )


def gzip_file(
    source_path: Path,
    output_path: Path,
) -> None:
    source_data = source_path.read_bytes()

    if source_path.suffix.lower() in MINIFIED_EXTENSIONS:
        source_text = source_data.decode("utf-8")
        source_data = minify_content(
            source_text,
            source_path.suffix.lower(),
        ).encode("utf-8")

    output_data = gzip.compress(
        source_data,
        compresslevel=9,
        mtime=0,
    )

    output_path.parent.mkdir(
        parents=True,
        exist_ok=True,
    )
    output_path.write_bytes(output_data)

    log_result(
        output_path.name,
        source_path.stat().st_size,
        len(output_data),
    )


def copy_file(
    source_path: Path,
    output_path: Path,
) -> None:
    output_path.parent.mkdir(
        parents=True,
        exist_ok=True,
    )
    shutil.copy2(
        source_path,
        output_path,
    )

    print(
        f"Copied: {source_path.relative_to(SRC_DIR)}"
    )


def directory_size(
    directory: Path,
) -> int:
    return sum(
        file_path.stat().st_size
        for file_path in directory.rglob("*")
        if file_path.is_file()
    )


def parse_arguments():
    parser = ArgumentParser(
        description="Build Spectra SPIFFS web resources."
    )
    parser.add_argument(
        "--gzip",
        action="store_true",
        help="Minify text and emit only gzip-compressed resources.",
    )

    return parser.parse_args()


def main() -> None:
    arguments = parse_arguments()
    mode = "gzip" if arguments.gzip else "original"

    print("Building Spectra web resources...\n")
    print(f"Source: {SRC_DIR}")
    print(f"Output: {DIST_DIR}")
    print(f"Mode:   {mode}\n")

    if not SRC_DIR.exists():
        raise FileNotFoundError(
            f"Source directory does not exist: {SRC_DIR}"
        )

    if DIST_DIR.exists():
        shutil.rmtree(DIST_DIR)

    DIST_DIR.mkdir(
        parents=True,
        exist_ok=True,
    )

    processed = 0

    for source_path in sorted(SRC_DIR.rglob("*")):
        if not source_path.is_file():
            continue

        relative_path = source_path.relative_to(SRC_DIR)

        if arguments.gzip:
            output_path = DIST_DIR / Path(
                str(relative_path) + ".gz"
            )
            gzip_file(
                source_path,
                output_path,
            )
        else:
            copy_file(
                source_path,
                DIST_DIR / relative_path,
            )

        processed += 1

    print(
        f"\nSource total: {format_size(directory_size(SRC_DIR))}"
    )
    print(
        f"Output total: {format_size(directory_size(DIST_DIR))}"
    )
    print(
        f"\nProcessed {processed} web resource(s)."
    )
    print("Web resources built successfully.")


if __name__ == "__main__":
    main()
