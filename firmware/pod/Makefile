# Project Name
TARGET = DroneVoice

# Sources
CPP_SOURCES = DroneVoice.cpp

# ReverbSc lives in the LGPL part of DaisySP
USE_DAISYSP_LGPL = 1

# Library locations. The defaults assume this repo is cloned next to
# DaisyExamples (same parent folder). Override on the command line if not:
#   make LIBDAISY_DIR=/path/to/libDaisy DAISYSP_DIR=/path/to/DaisySP
LIBDAISY_DIR ?= ../../../DaisyExamples/libDaisy
DAISYSP_DIR ?= ../../../DaisyExamples/DaisySP

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
