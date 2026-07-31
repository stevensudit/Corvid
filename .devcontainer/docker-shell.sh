#!/usr/bin/env bash

# This connects to the single running Docker instance, shelling to zsh.

set -euo pipefail

mapfile -t containers < <(docker ps --format '{{.Names}}')

case ${#containers[@]} in
    0)
        echo "No running Docker containers." >&2
        exit 1
        ;;
    1)
        exec docker exec -it "${containers[0]}" zsh
        ;;
    *)
        echo "Expected one running container, found ${#containers[@]}:" >&2
        printf '  %s\n' "${containers[@]}" >&2
        exit 1
        ;;
esac
