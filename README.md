# btc-hashrate

`btc-hashrate` is a C++20 command-line tool that estimates the average Bitcoin network hashrate over a user-specified number of days.

It uses `bitcoin-cli` to gather block header data and calculates hashrate based on average difficulty and time between blocks.

## Features

* Estimates hashrate using actual block header data
* Supports floating-point input for precise day ranges
* Displays readable summaries including block count, time intervals, difficulty, and hashrate

## Requirements

* `bitcoin-cli` (must be in `$PATH` and connected to a full node)
* C++20 compiler (Clang/GCC)
* Boost libraries (JSON)

## Build

```sh
mkdir build && cd build
cmake ..
make
```

## Usage

```sh
./btc-hashrate [days]
```

* `days` – optional; floating-point number of days to measure. Defaults to 1.

### Example

```sh
./btc-hashrate 3.5
```

## Output

```
Days: 3.5
Blocks: 504
Expected Time: 3d:12h:0m:0s
Actual Time:   4d:1h:49m:26s
Averge Block Time: 11.65m
Median Block Time: 8.26m
Std Dev: 11.39m
Average Difficulty: 126,411,437,451,912.0
Estimated Hashrate: 777.0 EH/s
```

## License

MIT

