BUILDDIR     ?= build
BUILDDIR_REL ?= build-release
CMAKE        ?= cmake
NINJA        ?= ninja
CTEST        ?= ctest
CLANG_FORMAT ?= clang-format
CLANG_TIDY   ?= clang-tidy
DOCKER       ?= docker
COMPOSE      ?= docker-compose

.PHONY: all build release test lint format format-check docker docker-up docker-down clean help

all: build

## build — Debug build with AddressSanitizer
build:
	@mkdir -p $(BUILDDIR)
	$(CMAKE) -GNinja -DCMAKE_BUILD_TYPE=Debug -B $(BUILDDIR)
	$(NINJA) -C $(BUILDDIR)

## release — Release build (optimised, no ASAN)
release:
	@mkdir -p $(BUILDDIR_REL)
	$(CMAKE) -GNinja -DCMAKE_BUILD_TYPE=Release -B $(BUILDDIR_REL)
	$(NINJA) -C $(BUILDDIR_REL)

## test — Run all unit tests
test: build
	$(CTEST) --test-dir $(BUILDDIR) --output-on-failure -j$$(nproc)

## test-integration — Run integration tests (requires: make docker-up first)
test-integration: build
	$(CTEST) --test-dir $(BUILDDIR) --output-on-failure -L integration -j$$(nproc)

## lint — Run clang-tidy over all source files
lint: build
	$(NINJA) -C $(BUILDDIR) clang-tidy

## format — Auto-format all C++ source files with clang-format
format:
	find . \( -name "*.cpp" -o -name "*.h" \) \
	  -not -path "./$(BUILDDIR)/*" \
	  -not -path "./$(BUILDDIR_REL)/*" \
	  -not -path "./_deps/*" \
	  | xargs $(CLANG_FORMAT) -i

## format-check — Check formatting without modifying files (for CI)
format-check:
	find . \( -name "*.cpp" -o -name "*.h" \) \
	  -not -path "./$(BUILDDIR)/*" \
	  -not -path "./$(BUILDDIR_REL)/*" \
	  -not -path "./_deps/*" \
	  | xargs $(CLANG_FORMAT) --dry-run --Werror

## docker — Build the dbmesh Docker image
docker:
	$(DOCKER) build -f docker/Dockerfile -t dbmesh:dev .

## docker-up — Start local dev environment (MySQL primary + replica)
docker-up:
	$(COMPOSE) up -d

## docker-down — Stop local dev environment
docker-down:
	$(COMPOSE) down

## docker-test-up — Start integration test databases only
docker-test-up:
	$(COMPOSE) -f tests/docker-compose.yml up -d

## docker-test-down — Stop integration test databases
docker-test-down:
	$(COMPOSE) -f tests/docker-compose.yml down -v

## clean — Remove build directories
clean:
	rm -rf $(BUILDDIR) $(BUILDDIR_REL)

## help — Show this help
help:
	@grep -E '^## ' Makefile | sed 's/## /  /'
