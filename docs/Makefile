# Minimal Sphinx makefile: `make html`, `make livehtml`, `make clean`, or any other Sphinx builder name.
SPHINXOPTS    ?=
SPHINXBUILD   ?= sphinx-build
SOURCEDIR     = .
BUILDDIR      = _build

help:
	@$(SPHINXBUILD) -M help "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)

livehtml:
	sphinx-autobuild "$(SOURCEDIR)" "$(BUILDDIR)/html" --watch ../terraingen --ignore "*/_doxygen/*" --ignore "*/_build/*" $(SPHINXOPTS) $(O)

clean:
	rm -rf "$(BUILDDIR)" _doxygen

.PHONY: help livehtml clean Makefile

%: Makefile
	@$(SPHINXBUILD) -M $@ "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)
