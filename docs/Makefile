# The documentation site: `make build` or `make serve` from docs/. Needs doxygen, the packages of
# requirements.txt, and TerrainGen built with Python in build/.
PYTHON ?= python
ROOT := $(abspath ..)

.PHONY: build serve clean

build:
	cd $(ROOT) && $(PYTHON) -m mkdocs build

serve:
	cd $(ROOT) && $(PYTHON) -m mkdocs serve

# Also mkdoxy's Doxygen output, which it reruns only when the headers change.
clean:
	rm -rf $(ROOT)/site $(ROOT)/.mkdoxy
