#!/bin/sh

git pull
make clean
make all
cd dist
./usbrelay-gui
