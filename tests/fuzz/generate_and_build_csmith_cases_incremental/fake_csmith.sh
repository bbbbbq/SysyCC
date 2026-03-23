#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 2 ]] || [[ "$1" != "--output" ]]; then
    echo "usage: $0 --output <file>" >&2
    exit 1
fi

output_file="$2"

cat > "${output_file}" <<'EOF'
int main(void) {
    return 0;
}
EOF
