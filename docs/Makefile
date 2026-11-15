# Moon Phase Clock documentation build targets.
#
# Run from anywhere:   make -C docs html / media
# (paths anchor to this file's own directory, so `make -C docs` works —
#  plain relative-to-cwd paths break the moment -C chdirs into docs/)

SELF     := $(lastword $(MAKEFILE_LIST))
HERE     := $(abspath $(dir $(SELF)))
DOCS     := $(HERE)
VENV     := $(HERE)/.venv
SPHINX   := $(VENV)/bin/sphinx-build
BUILD    := $(HERE)/_build

.PHONY: html media serve clean

## html: build the site (0 warnings is the bar; use -W for acceptance)
html:
	$(SPHINX) -b html $(DOCS) $(BUILD)/html

## serve: browse the built site at http://127.0.0.1:$(DOCS_PORT)/ (one
##        origin, so furo's theme toggle persists across pages — under
##        file:// Firefox partitions storage per document and the pick
##        is lost at every navigation; see _static/theme_sync.js).
##        Port 8000 is taken on this bench (a resident vllm-server),
##        hence 8123; override: make -C docs serve DOCS_PORT=9000
DOCS_PORT ?= 8123
serve:
	python3 -m http.server $(DOCS_PORT) --bind 127.0.0.1 --directory $(BUILD)/html

## media: regenerate screenshots + annotations + device composites
##        (the moonmedia builder; MEDIA_VIDEOS=1 includes the mp4 set)
media:
	$(SPHINX) -b media $(DOCS) $(BUILD)/media

## clean: remove generated build output (code/gen/ is gitignored too)
clean:
	rm -rf $(BUILD) $(DOCS)/code/gen
