#!/usr/bin/env bash

set -exou pipefail

./tools/dnf5lock \
    --output=test.json \
    --arch=x86_64 \
    --config=$(pwd)/centos/stream-9/etc/dnf/dnf.conf \
    --repodir=$(pwd)/centos/stream-9/etc/yum.repos.d \
    bash
