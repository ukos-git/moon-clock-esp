# Moon Phase Clock documentation build targets.
#
# From the repo root:  make -C docs html / media

DOCS     := docs
VENV     := docs/.venv
SPHINX   := $(VENV)/bin/sphinx-build
BUILD    := docs/_build

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
