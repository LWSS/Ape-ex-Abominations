#!/usr/bin/env bash

cd build/
make -j$(nproc)
cd ..
sudo bash extract.sh
sleep 2
sudo bash inject.sh
