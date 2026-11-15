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

.PHONY: html media clean

## html: build the site (0 warnings is the bar; use -W for acceptance)
html:
	$(SPHINX) -b html $(DOCS) $(BUILD)/html

## media: regenerate screenshots + annotations + device composites
##        (the moonmedia builder; MEDIA_VIDEOS=1 includes the mp4 set)
media:
	$(SPHINX) -b media $(DOCS) $(BUILD)/media

## clean: remove generated build output (code/gen/ is gitignored too)
clean:
	rm -rf $(BUILD) $(DOCS)/code/gen
