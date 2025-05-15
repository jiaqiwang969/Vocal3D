# Makefile for compiling the analysis document

# Variables
TEXFILE = 01_vtl3d_fem_core_analysis.tex
PDFFILE = $(TEXFILE:.tex=.pdf)
AUXFILES = $(TEXFILE:.tex=.aux) \
           $(TEXFILE:.tex=.log) \
           $(TEXFILE:.tex=.out) \
           $(TEXFILE:.tex=.toc) \
           $(TEXFILE:.tex=.nav) \
           $(TEXFILE:.tex=.snm)

# Compiler
LATEX = xelatex

# Default target
all: doc

# Target to compile the LaTeX document using xelatex
# Runs twice to ensure cross-references are correct
doc: $(PDFFILE)

$(PDFFILE): $(TEXFILE)
	@echo "Compiling $(TEXFILE) with $(LATEX)..."
	$(LATEX) $(TEXFILE)
	@echo "Compiling $(TEXFILE) again for cross-references..."
	$(LATEX) $(TEXFILE)
	@echo "PDF generated: $(PDFFILE)"

# Target to clean auxiliary files and the final PDF
clean:
	@echo "Cleaning auxiliary and output files..."
	rm -f $(AUXFILES) $(PDFFILE)

# Phony targets are not files
.PHONY: doc clean all 