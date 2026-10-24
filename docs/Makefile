# The documentation site: `make build` or `make serve` from docs/. Needs doxygen, the packages of
# requirements.txt, and TerrainGen built with Python in build/.
PYTHON ?= python
ROOT := $(abspath ..)

.PHONY: build serve gallery gallery-check clean

build:
	cd $(ROOT) && $(PYTHON) -m mkdocs build

serve:
	cd $(ROOT) && $(PYTHON) -m mkdocs serve

# Renders the gallery figures into _src/images/gallery, to commit when their look should change.
gallery:
	cd $(ROOT) && $(PYTHON) docs/gallery/gallery.py

# Renders every gallery figure small and throws it away: a check that the scripts still run.
gallery-check:
	cd $(ROOT) && out=$$(mktemp -d) && $(PYTHON) docs/gallery/gallery.py --quick --output-dir $$out && rm -rf $$out

# Also mkdoxy's Doxygen output, which it reruns only when the headers change.
clean:
	rm -rf $(ROOT)/site $(ROOT)/.mkdoxy
