# QT-based Digital Cluster on RUBIK PI 3

This project implements the poster flow:

`Sensor Data -> Arduino -> Serial Bus -> Debian/RUBIK PI 3 -> Qt Program -> CSV File -> MATLAB/Python Analysis`

The Qt 5 application receives RPM values from a serial port, updates a digital cluster UI, and logs end-to-end latency from input reception to widget paint completion.

## Build

```bash
qmake ctest101.pro
make -j"$(nproc)"
```

Docker build:

```bash
docker build -t digital-cluster .
```

## Run On Board

Default serial settings are 9600 baud, 8N1. If `CLUSTER_SERIAL_PORT` is not set, the app uses the first detected serial port.
For latency measurement, RPM is displayed immediately by default so `T_frame` matches the first paint of the received value.

```bash
CLUSTER_SERIAL_PORT=/dev/ttyACM0 CLUSTER_BAUD=9600 ./digital_cluster
```

Optional visual animation can be enabled for demonstrations, but it should be disabled for latency measurements:

```bash
CLUSTER_ANIMATE=1 CLUSTER_SERIAL_PORT=/dev/ttyACM0 ./digital_cluster
```

For Docker with X11 and serial device:

```bash
xhost +local:docker
docker run --rm -it \
  -e DISPLAY="$DISPLAY" \
  -e CLUSTER_SERIAL_PORT=/dev/ttyACM0 \
  -e CLUSTER_BAUD=9600 \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  --device=/dev/ttyACM0 \
  digital-cluster
```

## Simulation Mode

Use this when the RUBIK PI 3 or Arduino sensor input is not connected. It generates periodic RPM samples and still produces latency CSV logs.

```bash
CLUSTER_SIMULATE=1 ./digital_cluster
```

## CSV Log

Each run writes:

`latency_metrics_YYYYMMDD_hhmmss.csv`

Columns:

- `seq`: received sample sequence
- `source`: `serial` or `simulation`
- `t_in_ms`: application input timestamp
- `t_frame_ms`: Qt label paint completion timestamp
- `e2e_latency_ms`: `t_frame_ms - t_in_ms`
- `raw_rpm`: received RPM
- `rpm`: corrected/displayed RPM

## Analysis

Python:

```bash
python3 scripts/analyze_latency.py latency_metrics_YYYYMMDD_hhmmss.csv --out-dir analysis_output
```

MATLAB:

```matlab
analyze_latency("latency_metrics_YYYYMMDD_hhmmss.csv")
```

The analysis reports mean latency, jitter as standard deviation, worst latency, percentiles, and generates trend/distribution plots when plotting libraries are available.
