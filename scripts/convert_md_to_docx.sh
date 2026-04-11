#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/convert_md_to_docx.sh [-o OUTPUT_DIR]

Convert all Markdown files in the repository to .docx files using pandoc.
The output directory defaults to "word-docx" and mirrors the source tree.
EOF
}

out_dir="word-docx"

while [[ $# -gt 0 ]]; do
    case "$1" in
        -o|--out-dir)
            if [[ $# -lt 2 ]]; then
                echo "missing value for $1" >&2
                usage >&2
                exit 1
            fi
            out_dir="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown argument: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if ! command -v pandoc >/dev/null 2>&1; then
    echo "pandoc is not installed or not on PATH" >&2
    exit 1
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

mkdir -p "$out_dir"

converted=0

while IFS= read -r -d '' md_file; do
    rel_path="${md_file#./}"
    rel_dir="$(dirname "$rel_path")"
    base_name="$(basename "$rel_path" .md)"
    if [[ "$rel_dir" == "." ]]; then
        target_dir="$out_dir"
    else
        target_dir="$out_dir/$rel_dir"
    fi
    target_file="$target_dir/$base_name.docx"

    mkdir -p "$target_dir"
    pandoc "$md_file" -o "$target_file"
    echo "converted: $md_file -> $target_file"
    converted=$((converted + 1))
done < <(
    find . \
        -path "./.git" -prune -o \
        -path "./build" -prune -o \
        -path "./word-docx" -prune -o \
        -path "./plots" -prune -o \
        -path "./experiments" -prune -o \
        -path "./.venv-plot" -prune -o \
        -path "./.cache" -prune -o \
        -path "./.mplconfig" -prune -o \
        -name '.#*' -prune -o \
        -name "*.md" -print0
)

if [[ "$converted" -eq 0 ]]; then
    echo "no markdown files found"
else
    echo "done: converted $converted markdown files to docx under $out_dir/"
fi
