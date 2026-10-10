# Minimal Sphinx makefile: `make build`, `make serve`, `make clean`, or any other Sphinx builder name.
SPHINXOPTS    ?=
SPHINXBUILD   ?= sphinx-build
SOURCEDIR     = .
BUILDDIR      = _build

help:
	@$(SPHINXBUILD) -M help "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)

build:
	@$(SPHINXBUILD) -M html "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)

serve:
	sphinx-autobuild "$(SOURCEDIR)" "$(BUILDDIR)/html" --watch ../terraingen --ignore "*/_doxygen/*" --ignore "*/_build/*" $(SPHINXOPTS) $(O)

clean:
	rm -rf "$(BUILDDIR)" _doxygen

.PHONY: help build serve clean Makefile

%: Makefile
	@$(SPHINXBUILD) -M $@ "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)
