#!/bin/sh
# Run project tools with a local interpreter and locked Python dependencies.
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export UV_CACHE_DIR="$project_root/utils/.pack/uv-cache"
export UV_PYTHON_INSTALL_DIR="$project_root/.python"
export UV_PYTHON_PREFERENCE=only-managed
export UV_PROJECT_ENVIRONMENT="$project_root/.venv"

if command -v uv >/dev/null 2>&1; then
    uv_command=$(command -v uv)
else
    uv_command="$project_root/utils/.pack/uv-tools/uv"
    if [ ! -x "$uv_command" ]; then
        mkdir -p "$project_root/utils/.pack/uv-tools"
        installer="$project_root/utils/.pack/uv-tools/install.sh"
        if command -v curl >/dev/null 2>&1; then
            curl -fLsS https://astral.sh/uv/install.sh -o "$installer"
        elif command -v wget >/dev/null 2>&1; then
            wget -q https://astral.sh/uv/install.sh -O "$installer"
        else
            echo 'curl or wget is required to download uv.' >&2
            exit 1
        fi
        UV_INSTALL_DIR="$project_root/utils/.pack/uv-tools" UV_NO_MODIFY_PATH=1 sh "$installer"
    fi
fi

# Do not let an unrelated activated environment select the project's Python.
unset VIRTUAL_ENV PYTHONHOME PYTHONPATH
export PYTHONNOUSERSITE=1
export IKIGAI_PROJECT_PYTHON=1

cd "$project_root"
case "${1-}" in
    --tests)
        shift
        if [ "$#" -eq 0 ]; then set -- run_vs.py --help; fi
        exec "$uv_command" run --locked --group tests --extra content python "$@"
        ;;
    --content)
        shift
        if [ "$#" -eq 0 ]; then set -- run_vs.py --help; fi
        exec "$uv_command" run --locked --extra content python "$@"
        ;;
    *)
        if [ "$#" -eq 0 ]; then
            set -- run_vs.py --help
        fi
        exec "$uv_command" run --locked python "$@"
        ;;
esac
