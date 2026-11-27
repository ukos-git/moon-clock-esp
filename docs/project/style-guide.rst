How these docs are written
==========================

.. brief:
   Reader: anyone editing these docs, human or agent.
   They already know: RST and the project.
   After this page they can: write a page that fits the rest of the site.
   Page type: reference.

These rules keep the site readable for its two audiences: newcomers
with school-level background, and developers changing the firmware.
The research behind them, with sources, is in
``docs/research/documentation-practices.md``.

Two layers
----------

The pages under **Start here** (the landing page, :doc:`/how-it-works`,
:doc:`/try-the-simulator`, :doc:`/getting-started`,
:doc:`/using-the-clock`) assume no background beyond school. They never
depend on the in-depth pages to make sense, but they link to them.

The **In depth** and **Reference** pages assume some C and some maths.
They keep every detail, and each one opens with a short summary so a
reader can decide quickly whether it is the right page.

Before you write: the page brief
--------------------------------

Every page starts with a comment under its title that names its
reader and its job. It does not render:

.. code-block:: rst

   .. brief:
      Reader: <who>.
      They already know: <what>.
      After this page they can: <do or understand what>.
      Page type: <tutorial | how-to | explanation | reference>.

If you cannot fill in the third line, the page does not have a job
yet. Keep each page to one type. Tutorials teach by doing, with one
path and a visible result per step. How-to guides solve one task for
someone who already knows the basics. Explanations say why. Reference
pages state facts in a fixed order.

Sentences
---------

- Lead with the answer: the first sentence of a page or paragraph says
  what it gives the reader.
- One idea per sentence; about 20 words on average, split past 25.
- Active voice and present tense, with the actor named.
- Plain "is" and "has", not "serves as" or "boasts".
- One term per concept, the one in :doc:`the glossary </glossary>`.
  Define or link a term the first time it appears on a page.
- Name things by what the reader sees before what the code calls them.
- Use dashes rarely. A full stop, a colon or parentheses usually work
  better.
- No capital-letter emphasis (ONE, NEVER) and little bold: bold is for
  labels on the device and, rarely, the key term of a section.
- Headings in sentence case that say something ("How a frame reaches
  the panel", not "Frame Path").

Words to avoid
--------------

These make text sound inflated or machine-written. Use the plain
alternative or cut the phrase.

.. list-table::
   :header-rows: 1
   :widths: 45 55

   * - Avoid
     - Use
   * - crucial, pivotal, robust, seamless, comprehensive
     - a fact that shows it, or nothing
   * - serves as, stands as, acts as
     - is
   * - showcases, underscores, leverages
     - shows, stresses, uses
   * - simply, just, easy, obviously, of course
     - (cut)
   * - it is worth noting, note that, please note
     - (cut; use a ``note`` box if it matters)
   * - not just X but Y
     - say what it is
   * - in summary, overall, in conclusion
     - end when the content ends
   * - "…, ensuring smooth operation" (an *-ing* tail)
     - a separate sentence with the actual effect

Current state, then history
---------------------------

Pages describe the firmware as it is now. They do not narrate how it
got there ("used to", "was retired", "since 2026-11-13", "the new …").
Changes, removed features and the user decisions behind them go into
:doc:`/project/history`, one entry each, with the commit linked by the
``:commit:`` role. A page may keep one short line pointing there when
a reader of the current version needs it.

Numbers
-------

Every number comes from a named measurement or from the sources. Where
a drift-checked table exists (Kconfig, constants, partitions, binary
sizes), link to it rather than retyping the number in prose.
``docs/tools/check_docs_drift.py`` checks those tables against the
code.

When you rewrite a page, run the preservation guard against the last
commit. It lists every label, link, figure and number your edit
dropped:

.. code-block:: bash

   docs/.venv/bin/python docs/tools/check_rewrite_preserved.py docs/<page>.rst

Figures
-------

Alt text says in one or two sentences what the figure shows. The
caption says how to read it. Longer discussion belongs in the body
text.

Every page ends with two or three links under **Where to go next**,
one layer deeper or sideways.
