# Convenience Makefile for running chroma decoder tests
# Usage:
#   make test          - Verify against baseline
#   make generate      - Generate baseline signatures
#   make quick         - Quick verification test
#   make clean         - Clean test outputs
#   make help          - Show this help

.PHONY: help test generate quick references clean verbose

help:
	@echo "ORC Chroma Decoder Test Suite"
	@echo ""
	@echo "Targets:"
	@echo "  make test          - Verify decoder output against baseline"
	@echo "  make generate      - Generate baseline signatures"
	@echo "  make quick         - Run quick verification tests"
	@echo "  make verbose       - Run tests with verbose output"
	@echo "  make clean         - Clean test outputs and temp files"
	@echo "  make help          - Show this help"
	@echo ""
	@echo "Environment variables:"
	@echo "  DECODER=path       - Path to decoder executable"
	@echo ""
	@echo "Workflow:"
	@echo "  1. make generate   - Create baseline (first time)"
	@echo "  2. make test       - Verify after changes"
	@echo ""

test:
	@echo "Verifying against baseline..."
	./run-tests.sh verify

generate:
	@echo "Generating baseline signatures..."
	./run-tests.sh generate

references: generate

quick:
	@echo "Running quick verification tests..."
	./run-tests.sh verify --quick

verbose:
	@echo "Running tests with verbose output..."
	./run-tests.sh verify --verbose

clean:
	@echo "Cleaning test outputs..."
	rm -rf outputs/* temp/*
	@echo "Done."

.DEFAULT_GOAL := test
