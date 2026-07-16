#!/bin/sh

cd "$(dirname "$0")" || exit 1
git pull
make clean
make all
cd dist
./usbrelay-gui
