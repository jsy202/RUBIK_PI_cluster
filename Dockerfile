# syntax=docker/dockerfile:1.6

########################
# 1) Build stage (Ubuntu 20.04)
#    - TARGETPLATFORM 별로 실제 타겟 아키텍처로 빌드되도록 설정
########################
FROM --platform=$TARGETPLATFORM ubuntu:20.04 AS build
ARG TARGETPLATFORM
ENV DEBIAN_FRONTEND=noninteractive

# Qt 빌드에 필요한 패키지 설치
# - widgets/gui/core/network/serialport 모듈 대응
RUN apt-get update && apt-get install -y \
    build-essential \
    qtbase5-dev \
    qt5-qmake \
    qtbase5-dev-tools \
    libqt5serialport5-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
# 소스 복사
COPY . /src

# qmake -> make
# 실행 파일명은 .pro의 TARGET (여기서는 digital_cluster)
RUN qmake ctest101.pro && make -j"$(nproc)"

########################
# 2) Runtime stage (경량 Debian bullseye)
########################
FROM --platform=$TARGETPLATFORM debian:bullseye-slim AS runtime
ENV DEBIAN_FRONTEND=noninteractive

# Qt 앱 실행에 필요한 최소 런타임 라이브러리
RUN apt-get update && apt-get install -y --no-install-recommends \
    libqt5widgets5 \
    libqt5gui5 \
    libqt5core5a \
    libqt5network5 \
    libqt5serialport5 \
    libxcb1 \
    libx11-6 \
    libgl1 \
    libglib2.0-0 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# 빌드 산출물/리소스 복사
# (resources.qrc가 바이너리에 패킹되지만, 코드에서 직접 images/ 참조하면 필요)
COPY --from=build /src/digital_cluster /app/digital_cluster
COPY --from=build /src/images /app/images

# 기본 실행 파일
ENTRYPOINT ["/app/digital_cluster"]

