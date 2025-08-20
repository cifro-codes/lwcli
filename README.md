# lwcli

This is an experimental TUI (FTXUI) wallet (with mouse support) used to test the ability of [lwsf](https://code.leeclagett.com/lwsf) in replacing the standard backend for the Monero wallet api. This wallet can use either backend with a runtime option, but once the wallet file is created it is "bound" to that particular backend due to file format.

This was _not_ billed/paid for by Monero CCS, as I'm not sure if there are real users desiring this sort of wallet. I may ask for CCS contributions in maintaining this wallet if there are users.

## Building
```bash
git clone https://github.com/monero-project/monero.git
git clone https://github.com/cifro-codes/lwcli.git
cd lwcli && mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DMONERO_SOURCE_DIR=../../monero ..
make -j$(nproc)
./src/lwcli -h
```
