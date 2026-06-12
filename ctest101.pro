QT       += core gui widgets network serialport

TARGET = digital_cluster
TEMPLATE = app

SOURCES += \
    main.cpp \
    mainwindow.cpp

HEADERS  += \
    mainwindow.h \
    timestamplabel.h

FORMS    += \
    mainwindow.ui

# 리소스 파일
RESOURCES += \
    resources.qrc

# =========================
# 표준/최적화/경고 설정
# =========================

# C++17 이상: std::clamp 등 사용 가능
CONFIG += c++17

# 권장 최적화 (디버그/릴리즈 별로 qmake가 적절히 지정하지만, 여기선 조금 더 명시)
QMAKE_CXXFLAGS_RELEASE += -O2
QMAKE_CXXFLAGS_DEBUG   += -O0 -g

# 경고 강화 (선택)
QMAKE_CXXFLAGS += -Wall -Wextra

# 일부 배포 환경에서 필요한 경우 XCB 등은 시스템 패키지로 설치 필요
# (Ubuntu) sudo apt install libxcb1 libxcb-xinerama0

