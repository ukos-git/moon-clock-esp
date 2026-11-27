# Deep Research: Technical documentation practices for the Moon Phase Clock docs

Date: 2026-11-27 · Depth: exhaustive · Method: Firecrawl search (about 30
queries) and scrape of 50+ pages, primary sources preferred. Scraped content
was treated as data, not instructions.

## Executive Summary

The best-supported advice for this rewrite is not new. Diátaxis, the government
plain-language programmes, the large vendor style guides (Google, Microsoft)
and the usability research from Nielsen Norman Group mostly agree. Work out
what the reader is trying to do before deciding what kind of page to write. Put
the answer first. Use short sentences, ordinary words, active voice and the
present tense. Keep teaching, doing, looking up and understanding on separate
pages, and link between them. Where these sources disagree, the disagreement is
about how strict to be, not about the direction.

Diátaxis gives the clearest model for the page types. Its four kinds of page
(tutorial, how-to guide, reference, explanation) come from two questions: is
the reader acting or thinking, and are they learning or working? Its author
also says not to restructure everything in one go. Improve one page at a time
and let the four-part structure grow from the inside. The main critiques are
worth taking seriously for this project. Hillel Wayne argues that a dense
conceptual model needs a "conceptual overview" before the tutorial, and a moon
clock whose whole selling point is physical correctness has exactly that kind
of model. Others point out that Diátaxis says little about who the reader is,
and the Good Docs Project and DITA both carry types that the four boxes leave
out, troubleshooting among them.

On voice, the strongest practical source is Wikipedia's "Signs of AI writing"
page, together with Google's and Microsoft's tone guidance. The patterns they
describe (inflated significance, "serves as" in place of "is", tacked-on "-ing"
clauses, negative parallelisms, reflexive groups of three, heavy bold, dashes
everywhere, varying words for the sake of variety) are also old advice about
bad writing. A quick count on the current docs finds about 3,600 em dashes in
175,000 words of hand-written RST (about one every 48 words), 143 ISO dates and
149 uses of "retired". Those numbers point to the two biggest style jobs in the
rewrite. Cut the dashes and restructure the sentences they hold together. Move
the project history out of the current-state pages and into a changelog or
decision record.

## Key Findings

1. **Classify pages by the reader's need, using two questions.** Diátaxis sorts
   content by "action or cognition?" and "acquisition or application?", which
   gives tutorial, how-to, reference and explanation
   ([compass](https://diataxis.fr/compass/), [home](https://diataxis.fr/)).
   Canonical, Gatsby and Cloudflare report adopting it
   ([Canonical blog](https://ubuntu.com/blog/diataxis-a-new-foundation-for-canonical-documentation),
   [diataxis.fr](https://diataxis.fr/)).

2. **Restructure incrementally, never with empty scaffolding.** Procida says to
   pick any piece, assess it, make one improvement and publish it, and warns
   against creating empty tutorial, how-to, reference and explanation sections
   ("Don't do that. It's horrible.")
   ([workflow](https://diataxis.fr/how-to-use-diataxis/)). Google's guidance on
   dead docs says the same from the other side: delete what you know is wrong
   first and iterate
   ([Google docguide](https://google.github.io/styleguide/docguide/best_practices.html)).

3. **Tutorials have their own rules.** Show where the learner is going, give a
   visible result at every step, say what they should see, leave out options,
   keep explanation to a sentence and a link, and aim for perfect reliability
   ([tutorials](https://diataxis.fr/tutorials/)). Sequin's rewrite found that
   starting with "how it works" felt like studying for a test, and moved to a
   quickstart of about three minutes
   ([Sequin](https://blog.sequinstream.com/we-fixed-our-documentation-with-the-diataxis-framework/)).

4. **But dense subjects need a conceptual overview first.** Hillel Wayne argues
   the four-type model fits tools, breaks down for frameworks and languages,
   and is missing "conceptual overviews" (and examples) that come _before_ the
   tutorial
   ([Wayne](https://www.hillelwayne.com/post/problems-with-the-4doc-model/)).
   This conflicts with finding 3, and the conflict matters here (see Detailed
   Analysis, theme 2).

5. **Reference is austere and mirrors the product.** "One hardly reads
   reference material; one consults it." It should describe and only describe,
   follow consistent patterns, mirror the structure of the machinery, and be
   generated from the source where possible
   ([reference](https://diataxis.fr/reference/)).

6. **Explanation answers "why" and may hold opinions.** It should provide
   context, design decisions and constraints, weigh alternatives and stay
   bounded. Titles should take an implicit "About..."
   ([explanation](https://diataxis.fr/explanation/)).

7. **Put the most important information first.** The inverted pyramid helps
   every kind of reader because they can stop at any point and still have the
   main point
   ([NN/g inverted pyramid](https://www.nngroup.com/articles/inverted-pyramid/)).
   In Nielsen's 1997 study, 79% of test users scanned and 16% read word by
   word; concise, scannable and objective text together measured 124% better
   usability than promotional text
   ([NN/g how users read](https://www.nngroup.com/articles/how-users-read-on-the-web/)).

8. **Progressive disclosure works for mixed audiences.** Show a few important
   things first and the rest on request. NN/g reports it improves learnability,
   efficiency and error rate, and that the worry about limited mental models is
   groundless. The two hard parts are getting the split right and making the
   path to the deeper layer obvious
   ([NN/g progressive disclosure](https://www.nngroup.com/articles/progressive-disclosure/)).

9. **Link labels carry "information scent".** Readers choose links by their
   estimate of what is behind them, built from the label, its context and prior
   experience. Jargon in a label can hide the right page
   ([NN/g information scent](https://www.nngroup.com/articles/information-scent/)).

10. **Plain language is for experts too.** NN/g found that domain experts also
    prefer plain language
    ([NN/g](https://www.nngroup.com/articles/plain-language-experts/)). GOV.UK
    argues the same, adding that the most specialist readers often have the
    least time
    ([GDS blog](https://insidegovuk.blog.gov.uk/2014/08/04/sentence-length-why-25-words-is-our-limit/)).

11. **Concrete sentence-level rules agree across sources.** Active voice,
    present tense, second person, short sentences, one idea per sentence, verbs
    instead of hidden nominalisations
    ([digital.gov](https://digital.gov/guides/plain-language/writing),
    [digital.gov clear and short](https://digital.gov/guides/plain-language/writing/clear-short),
    [EU How to write clearly](https://publications.europa.eu/resource/cellar/725b7eb0-d92e-11e5-8fea-01aa75ed71a1.0004.03/DOC_1),
    [Google active voice](https://developers.google.com/style/voice)). The EU
    guide suggests about 20 words per sentence on average; GOV.UK sets 25 as
    the point to break a sentence up.

12. **Readability formulas are a weak target.** Redish and Selzer list five
    problems: no research basis for technical writing, poor validity, shorter
    sentences not always clearer, a poor fit with how people process text, and
    blindness to content, organisation and layout. They recommend user testing
    instead
    ([Redish & Selzer 1985](https://redish.net/wp-content/uploads/Redish_Selzer_1985.pdf)).

13. **Write for readers whose first language is not English.** In GitHub's 2017
    survey, nearly a quarter of the open-source community reads and writes
    English less than "very well"
    ([Open Source Survey](https://opensourcesurvey.org/2017/)). Google and
    Microsoft give specific rules: keep "that" and articles, avoid phrasal
    verbs and idioms, don't stack modifiers, define abbreviations, repeat a
    word rather than swap in a synonym
    ([Google global audience](https://developers.google.com/style/translation),
    [Microsoft global writing](https://learn.microsoft.com/en-us/style-guide/global-communications/writing-tips)).

14. **Name the patterns that make text read as machine-written.** Wikipedia's
    AI Cleanup page lists inflated significance and legacy, superficial "-ing"
    analyses, vague attributions, avoiding plain "is" ("serves as"), negative
    parallelisms, the rule of three, "Challenges" and "In summary" sections,
    title case, heavy bold, inline-header lists, emoji in headings, em dashes
    and "elegant variation"
    ([Signs of AI writing](https://en.wikipedia.org/wiki/Wikipedia:Signs_of_AI_writing)).
    It also warns that detectors and human judgement are unreliable, so these
    are reasons to revise, not proof of authorship.

15. **Write timeless current-state docs; put history in a changelog.** Google
    tells writers to document the current version and avoid "now", "new",
    "currently" and "latest" in product docs
    ([Google timeless documentation](https://developers.google.com/style/timeless-documentation)).
    Keep a Changelog says a changelog is curated, for humans, newest first,
    grouped by change type, and that dumping commit logs into it is a bad idea
    ([Keep a Changelog](https://keepachangelog.com/en/1.1.0/)).

16. **Docs live with the code and get trimmed.** Change docs in the same change
    as the code, delete dead docs, prefer good over perfect
    ([Google docguide](https://google.github.io/styleguide/docguide/best_practices.html)).
    Write the Docs adds "incorrect documentation is worse than missing
    documentation" and "ARID" (accept some repetition)
    ([WTD principles](https://www.writethedocs.org/guide/writing/docs-principles/)).
    In GitHub's survey 93% of respondents had met incomplete or outdated docs
    ([Open Source Survey](https://opensourcesurvey.org/2017/)).

17. **Hardware exemplars open with a picture and a parts list.** Adafruit's
    authoring guide requires the first page to be "Overview", with a graphic
    near the top, a paragraph on what the project is, and a parts list, and it
    discourages long runs of text without images
    ([Adafruit guide](https://cdn-learn.adafruit.com/downloads/pdf/creating-great-guides-for-the-adafruit-learning-system.pdf)).
    ESP-IDF's Get Started lists hardware and software needs before any steps
    ([ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/index.html)).
    Raspberry Pi's documentation home is a grid of illustrated cards, with
    "Getting started" first
    ([Raspberry Pi docs](https://www.raspberrypi.com/documentation/)).

18. **Home Assistant's style rules are a close model for this project.** Talk
    to "you", don't call things "advanced", be honest about limitations,
    sentence-case headings that name the subject (not "Overview" or "Good to
    know"), no bold for emphasis, one sentence per source line, and "go easy on
    em dashes", which it calls "a common sign of machine-written text"
    ([HA style](https://contributors.home-assistant.io/writing/reference/style/)).

19. **For school-level science, go concrete first, then fade to abstract.** A
    review in _Educational Psychology Review_ recommends starting with concrete
    materials and fading gradually to abstract symbols
    ([Fyfe et al. 2014](https://link.springer.com/article/10.1007/s10648-014-9249-3)).
    Moon-phase misconceptions are common and stubborn even in adults, the
    commonest being that phases are Earth's shadow
    ([LPI](https://www.lpi.usra.edu/education/pre_service_edu/PhasesMisconceptions.shtml)).

20. **Diátaxis is silent on audience.** Romero argues it has no mechanism for
    "recipient design" and proposes a short documentation brief: who is this
    for and what do they already know
    ([Romero](https://d-romero.com/blog/what-diataxis-is-missing/)).

## Detailed Analysis

### Theme 1: Page types, and what the four boxes leave out

Diátaxis is the dominant model and the most useful one for this rewrite, mainly
because it separates tutorials from how-to guides. The two are easy to confuse.
A tutorial is a lesson: the author takes responsibility for the learner's
success, removes choices and makes every step produce something visible
([tutorials](https://diataxis.fr/tutorials/)). A how-to guide serves someone
who already knows what they want. It should be written "from the perspective of
the user, not of the machinery", may branch, and should start and end at
sensible points instead of being complete
([how-to guides](https://diataxis.fr/how-to-guides/)). Reference describes and
nothing else ([reference](https://diataxis.fr/reference/)). Explanation is the
one type that "might make sense to read in the bath", and the only one where
opinion and alternatives belong
([explanation](https://diataxis.fr/explanation/)).

The model came out of Daniele Procida's work at Divio and was later spun off
under its own name. Wayne checked the history and found no plagiarism the other
way round
([Wayne](https://www.hillelwayne.com/post/problems-with-the-4doc-model/)).
Procida himself presents it as a guide, not a plan. Tom Johnson, who questioned
it as "overly opinionated and arbitrary", reports that Procida told him it is
meant as an analysis of needs, not four rigid buckets
([I'd Rather Be Writing](https://idratherbewriting.com/blog/what-is-diataxis-documentation-framework)).

Other taxonomies add types that matter for hardware. DITA 1.3's technical
content package defines concept, reference, task (general, strict and
machinery), troubleshooting and glossary topics
([OASIS DITA](https://docs.oasis-open.org/dita/dita/v1.3/os/part2-tech-content/archSpec/technicalContent/dita-technicalContent-InformationTypes.html)).
The Good Docs Project's core pack has concept, how-to, README, reference,
release notes, troubleshooting and tutorial templates
([Good Docs templates](https://www.thegooddocsproject.dev/template)).
Troubleshooting, a glossary and release notes are the practical additions. They
don't break Diátaxis (troubleshooting is a kind of how-to, a glossary is
reference, release notes are history), but they deserve their own templates.

### Theme 2: Overview first, or tutorial first?

This is the main tension in the sources. Sequin watched users read its docs and
found that "How Sequin works" as the entry page felt like homework; users only
understood the product once they used it
([Sequin](https://blog.sequinstream.com/we-fixed-our-documentation-with-the-diataxis-framework/)).
Diátaxis agrees: "ruthlessly minimise explanation" in tutorials. Wayne argues
the opposite for subjects with a dense conceptual model. His TLA+ overview
explains the problem and the concepts before any code, for people "getting
ready to learn" or deciding whether to
([Wayne](https://www.hillelwayne.com/post/problems-with-the-4doc-model/)).

The two positions fit together once you separate length from position. A short
conceptual overview (one screen, a picture, the three or four ideas the rest of
the docs rely on) can come first, as long as it is short and leads straight
into something the reader can do. NN/g's inverted pyramid and progressive
disclosure support this: the top layer gives the point and the mental model,
and deeper layers are one obvious click away
([inverted pyramid](https://www.nngroup.com/articles/inverted-pyramid/),
[progressive disclosure](https://www.nngroup.com/articles/progressive-disclosure/)).

For this project the overview has to carry a real idea: the clock computes
where the Sun, Earth and Moon are and renders the lit part of a real lunar
surface, then checks itself against NASA images. A newcomer needs that sentence
and a picture before a build tutorial makes sense. They do not need libration
or terminator-band maths at that point.

### Theme 3: Layering for a mixed audience

The readers range from someone with school physics and no embedded experience
to firmware developers. Three findings shape the layering.

First, readers scan. In Nielsen's study most test users scanned, and concise
plus scannable plus objective text tested far better than promotional text
([NN/g](https://www.nngroup.com/articles/how-users-read-on-the-web/)). The
"objective language" variant alone improved usability by 27%, which surprised
the researchers. That is a direct argument against puffery in project docs.

Second, the layers need good signposts. Information scent says readers pick the
link that seems most likely to hold their answer, and jargon in a label lowers
its scent for people who don't know the term
([NN/g](https://www.nngroup.com/articles/information-scent/)). A sidebar entry
such as "compute-progress" means nothing to a newcomer. "How the clock shows
progress while it draws a new moon" does.

Third, plain language does not insult experts. NN/g's expert study and the
GOV.UK writing team both say specialists want plain text, because they have the
least time ([NN/g](https://www.nngroup.com/articles/plain-language-experts/),
[GDS](https://insidegovuk.blog.gov.uk/2014/08/04/sentence-length-why-25-words-is-our-limit/)).
So the layers should differ in depth and in how much is assumed, not in
register. Advanced pages can use terms like PSRAM or DMA without defining them,
as long as each term links to the glossary the first time it appears.

Home Assistant adds a useful warning: don't label features "advanced", and
don't make the product sound fragile
([HA style](https://contributors.home-assistant.io/writing/reference/style/)).
Labelling a section "for experts" tells newcomers to leave. Label it by subject
instead ("Firmware internals").

### Theme 4: Sentences and words

The government and vendor guides agree closely, and their examples are easy to
borrow.

- Active voice that names who acts. "The server sends an acknowledgment", not
  "an acknowledgment is sent"
  ([Google](https://developers.google.com/style/voice)). Passive is fine when
  the actor is obvious or unimportant, and both Google and the EU guide say so.
- Present tense. digital.gov says other tenses make the reader work harder
  ([digital.gov](https://digital.gov/guides/plain-language/writing)).
- Verbs, not hidden verbs. "We analyze data", not "we conduct an analysis of
  the data" ([digital.gov](https://digital.gov/guides/plain-language/writing);
  the EU guide has the same rule as Hint 6).
- Short sentences and paragraphs. The EU guide suggests about 20 words per
  sentence on average and recommends mixing in short ones. digital.gov suggests
  paragraphs under 150 words and never over 250
  ([EU](https://publications.europa.eu/resource/cellar/725b7eb0-d92e-11e5-8fea-01aa75ed71a1.0004.03/DOC_1),
  [digital.gov](https://digital.gov/guides/plain-language/writing/clear-short)).
- Simple words: "use", not "utilize" or "leverage"; "so", not "consequently"
  ([Google](https://developers.google.com/style/translation)).
- No "simply", "easy" or "just" in procedures. Google lists them as things to
  avoid, along with "please note", exclamation marks and starting every
  sentence with the same phrase
  ([Google tone](https://developers.google.com/style/tone)). The current docs
  use "simply" 17 times.
- Don't change words for variety. The EU guide gives an example where switching
  from "application" to "request" confuses the reader. Wikipedia lists the same
  habit ("elegant variation") as an AI sign
  ([EU](https://publications.europa.eu/resource/cellar/725b7eb0-d92e-11e5-8fea-01aa75ed71a1.0004.03/DOC_1),
  [Wikipedia](https://en.wikipedia.org/wiki/Wikipedia:Signs_of_AI_writing)). In
  technical docs this matters a lot: if "bake", "render" and "compute" all mean
  the same step, readers will assume they are three steps.
- Headings that say something. The EU guide prefers "Mergers need to be
  monitored more carefully" to "Monitoring mergers". HA rejects "Overview" and
  "Good to know" as headings
  ([HA](https://contributors.home-assistant.io/writing/reference/style/)).

A caution on numbers. GOV.UK quotes Ann Wylie for the claim that readers
understand over 90% at 14 words per sentence and under 10% at 43 words
([GDS](https://insidegovuk.blog.gov.uk/2014/08/04/sentence-length-why-25-words-is-our-limit/)).
That figure is secondhand and I could not trace the original study, so treat it
as illustrative. Redish and Selzer's point stands: sentence length is something
you can count, which is why formulas use it, but content, organisation and
layout matter more
([Redish & Selzer](https://redish.net/wp-content/uploads/Redish_Selzer_1985.pdf)).
Use sentence length as a warning light. Don't treat it as a score to hit.

### Theme 5: Writing that sounds human

The Wikipedia page is descriptive, not a style guide, and it says so. Its core
observation is useful for writers anyway. Language models regress to the mean:
they replace specific, unusual facts with generic positive statements that
could fit any topic ("inventor of the first train-coupling device" becomes "a
revolutionary titan of industry")
([Wikipedia](https://en.wikipedia.org/wiki/Wikipedia:Signs_of_AI_writing)). The
cure is the same as for most bad writing: say the specific thing.

The patterns it lists, grouped for a docs writer:

- Content: inflated significance ("stands as a testament", "pivotal", "evolving
  landscape"); superficial "-ing" tails ("..., ensuring reliability", "...,
  highlighting its importance"); vague attribution ("experts argue");
  "Challenges" and "Future outlook" sections that end on a vaguely positive
  note.
- Language: AI vocabulary that clusters ("additionally", "crucial", "enhance",
  "foster", "showcase", "underscore", "vibrant", "delve"); avoiding plain "is"
  and "has" ("serves as", "boasts", "features"); negative parallelisms ("not
  just X, but Y", "not X, but Y"); the rule of three; elegant variation.
- Style: title case in headings, heavy bold, inline-header bullet lists
  ("**Speed:** ..."), emoji in headings, curly quotes, em dashes used where a
  person would use a comma, colon or parentheses.
- Communication leaking into the text: "I hope this helps", "Certainly!", "let
  me know", "In summary".

The page also lists what human writing tends to have more of: plain "is" and
"has", short common verbs ("used", not "utilized"), definite statements, and
occasional qualifiers. That is a useful positive target.

Two caveats. The em-dash sign is labelled "2022 to September 2026" on the page
because newer models were tuned to suppress it, so the dash is now a weaker
signal. The page also warns that humans detect AI text at roughly chance level,
and detectors have real error rates. For this project the question is not "does
this look AI-written" but "does this read well". The patterns are worth
removing either way, because each one is also a known weakness of human prose.
Google says to avoid buzzwords and placeholder phrases; NN/g measured the cost
of promotional language; and Home Assistant names em-dash overuse directly.

### Theme 6: Hardware and maker exemplars

The exemplars share a few structural habits.

- Adafruit: every guide starts with an Overview page that has a picture near
  the top, a paragraph on what the project is and a parts list. Steps are shown
  with pictures beside text (no more than four pictures per block), and build
  steps must survive being printed as a PDF, so video is never the only source.
  Guides link to shared foundation guides instead of repeating them
  ([Adafruit](https://cdn-learn.adafruit.com/downloads/pdf/creating-great-guides-for-the-adafruit-learning-system.pdf)).
- ESP-IDF: Get Started opens with what the chip is, then "What you need" split
  into hardware and software, then installation per OS
  ([ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/index.html)).
  It also contains "Currently, some of the development boards are using USB
  Type C connectors", which is the kind of time-bound phrase Google advises
  against. Even good docs drift.
- Zephyr: board pages follow a fixed directive-driven template whose metadata
  comes from the board's config files, and Kconfig options are cross-referenced
  with a role that links to generated reference
  ([Zephyr guidelines](https://docs.zephyrproject.org/latest/contribute/documentation/guidelines.html)).
  This project's single-sourced Kconfig tables and drift gate already follow
  the same idea.
- Raspberry Pi: the documentation home is a grid of illustrated cards with
  one-line descriptions, Getting started first
  ([Raspberry Pi](https://www.raspberrypi.com/documentation/)).
- Prusa's knowledge base is organised by product first, with search prominent
  ([Prusa](https://help.prusa3d.com/)). I only looked at the home page, so this
  is a light observation.

### Theme 7: Sphinx and furo mechanics

Sphinx's `toctree` supports `:caption:` (which furo shows as sidebar section
headings), `:hidden:`, `:maxdepth:` and `:titlesonly:`
([Sphinx directives](https://www.sphinx-doc.org/en/master/usage/restructuredtext/directives.html)).
The current `index.rst` already uses hidden toctrees with captions, which is
the right base. Sphinx also has `glossary` with the `:term:` role, `seealso`,
and `versionadded` / `versionchanged` / `deprecated`. The version directives
are how Sphinx expects history to appear inside a reference page: one short
marked line, not narrative. Furo supports the standard admonitions and treats
sidebar customisation as unstable, so it is better to shape navigation with
toctrees than with custom sidebar templates
([furo admonitions](https://pradyunsg.me/furo/reference/admonitions/),
[furo sidebar](https://pradyunsg.me/furo/customisation/sidebar/)).

### Theme 8: History, dates and drift

Google's timeless-documentation rule is plain: document the current version,
and keep words like "new", "now" and "currently" for blog posts and release
notes ([Google](https://developers.google.com/style/timeless-documentation)).
Its two stated benefits are less maintenance and not assuming the reader knew
the old version. Keep a Changelog adds where the history should go instead
([Keep a Changelog](https://keepachangelog.com/en/1.1.0/)).

The current docs mix the two. A quick count of the 68 hand-written RST files
(generated source pages excluded) found 143 ISO dates, 149 uses of "retired",
25 of "used to" and 15 of "no longer". Lines such as "the retired boot..." or
"retired with the dusk :commit:`f7740500`" inside glossary and reference
entries make the reader process removed features to understand present ones.
That history is valuable to maintainers, so it should move to a place built for
it, not be deleted.

On drift, Google says to change docs in the same change as the code and delete
dead docs. Write the Docs says incorrect docs are worse than none
([Google](https://google.github.io/styleguide/docguide/best_practices.html),
[WTD](https://www.writethedocs.org/guide/writing/docs-principles/)). This
project already has stronger machinery than most: generated reference tables
and a drift gate. The rewrite should rely on that machinery more. Every number
a reader might act on (sizes, timings, limits) should be generated or checked,
and hand-written prose should avoid restating such numbers.

### Theme 9: Explaining science to school-level readers

Two findings matter. Concreteness fading: start with a concrete, physical
picture, then move step by step to the abstract form, because the concrete
stage gives the symbols meaning
([Fyfe et al.](https://link.springer.com/article/10.1007/s10648-014-9249-3)).
For moon phases the concrete stage is the lamp-and-ball model (or a photo
series), then a top-down diagram, then angles, then the formula. Second,
misconceptions: the commonest is that phases are Earth's shadow, and these
ideas are resistant to change, even in adults
([LPI](https://www.lpi.usra.edu/education/pre_service_edu/PhasesMisconceptions.shtml)).
Explanation pages should name the wrong idea and show why it fails (an eclipse
looks different from a crescent) as well as stating the right one. Google warns
against figurative language for a global audience
([Google tone](https://developers.google.com/style/tone)), so analogies should
be physical and literal ("a ball lit by a lamp") rather than idioms.

## Applying it to this project

This section is the style guide for the rewrite. The rules are written to be
checked, not admired.

### Before writing any page: a four-line brief

Following Romero, write four lines at the top of the draft (as an RST comment,
so it doesn't render):

```rst
.. brief:
   Reader: someone who has the S3 board and has never used ESP-IDF.
   They already know: how to use a terminal a little; school physics.
   After this page they can: flash the clock and see today's moon.
   Page type: tutorial.
```

If you can't fill in the third line, the page doesn't have a job yet.

### Page-type templates

#### Tutorial skeleton

```rst
Build and flash your first clock
================================

In this tutorial we build the firmware for the AMOLED board, flash it, and
watch the clock draw tonight's moon. It takes about 30 minutes.

You need:

- the Waveshare ESP32-S3 Touch AMOLED 1.75 board and a USB-C data cable
- a Linux or macOS computer (Windows: see :doc:`/how-to/windows`)

1. Install ESP-IDF
------------------
<one action>
<exact command>
You should see: <exact expected output, trimmed>

If you see <common error>, you probably <cause>. <one-line fix>.

2. ...

What you built
--------------
<one paragraph naming what the learner did, not "you learned">

Next steps
----------
- :doc:`/how-to/set-up-wifi`: connect the clock to your network
- :doc:`/explanation/how-the-moon-is-drawn`: why the moon looks right
```

Rules: one path, no options, a visible result per step, the exact expected
output, likely errors flagged inline, explanation limited to one sentence and a
link. Test it from a clean machine before publishing
([tutorials](https://diataxis.fr/tutorials/)). The simulator is the best first
tutorial for this project because it needs no hardware and can be made fully
reliable.

#### How-to skeleton

```rst
Flash a board without losing its Wi-Fi settings
===============================================

Use this when you've changed the firmware and want to keep the saved
network on the board.

Before you start: you have a build that finishes, and you know which port
the board is on (:doc:`identify-boards`).

1. ...
2. ...

If the board is the knob, use ``app-flash`` instead: <command>.

Related: :doc:`/reference/partitions`
```

Title as a goal ("Flash a board without losing its Wi-Fi settings"), not a tool
("Using app-flash"). Conditions before instructions ("If the board is the knob,
..."), which is Google's rule. Branches are allowed. No background beyond one
sentence.

#### Explanation skeleton

```rst
Why the clock checks itself against NASA images
===============================================

<two to four sentence answer, the whole point up front>

<concrete picture or figure first>

The idea
--------
<concrete to abstract, one step at a time>

What it is not
--------------
<the common wrong idea, and why it fails>

Trade-offs
----------
<what was chosen, what was rejected, and why; opinions allowed>

Go deeper
---------
- <reference or advanced explanation links>
```

Title should accept an implicit "About" or "Why". Opinion and rejected
alternatives belong here and nowhere else
([explanation](https://diataxis.fr/explanation/)).

#### Reference skeleton

```rst
Partition table (S3)
====================

<one sentence: what this table is and where it comes from>

.. generated table or list-table, with headers and a caption

Limits
------
- <constraint stated as a fact: "The factory partition is 2,097,152 bytes.">

.. versionchanged:: <release or commit>
   <one line, only if a reader of the current version needs it>
```

Same order on every page of a kind (every board page, every screen page, every
Kconfig group). Facts only. Generate the table from source wherever possible.
No "why" beyond a link to an explanation page.

#### Troubleshooting skeleton

```rst
The screen stays black after flashing
=====================================

Symptom: <what the reader sees>
Likely cause: <one cause>
Fix: <steps>

(repeat per cause, most common first)
```

The Good Docs Project and DITA both treat troubleshooting as its own type
([Good Docs](https://www.thegooddocsproject.dev/template),
[DITA](https://docs.oasis-open.org/dita/dita/v1.3/os/part2-tech-content/archSpec/technicalContent/dita-technicalContent-InformationTypes.html)).
This project already has real material for it, such as the brown-out reset that
looks like a deep-sleep wake on the knob.

### Voice and style rules

1. Write to "you" in how-tos and reference; "we" is fine in tutorials, where
   Diátaxis recommends it. Avoid "the user".
2. Lead with the answer. The first sentence of a page says what the page gives
   the reader. The first sentence of a paragraph says what the paragraph is
   about.
3. One idea per sentence. Aim for around 20 words on average; if a sentence
   passes 25, split it or check that it really needs the length.
4. Active voice, present tense, with the actor named. Passive is fine when the
   actor doesn't matter.

   Before: "The finished frame is flushed atomically after the bake is
   completed." After: "When the bake finishes, the display task sends the whole
   frame to the panel in one flush."

5. Use plain "is" and "has".

   Before: "The TF module serves as the hour cache." After: "The TF module is
   the hour cache."

6. Use verbs, not nouns made from verbs.

   Before: "Perform a regeneration of the media after the modification of a
   screen." After: "Regenerate the media after you change a screen."

7. One term per concept, and keep it. Pick "bake" or "render" for the texture
   step, define it in the glossary, and use only that word. Don't change words
   for variety.
8. Define a term or link it to the glossary (`:term:`) the first time it
   appears on each page. Expand abbreviations once per page (PSRAM, DMA, RTC).
9. Name things by what the reader sees before what the code calls them.

   Before: "`UI_SCREEN_SWITCH_FADE` dims, loads, flushes and ramps." After:
   "When you change screens, the display fades to black, swaps the screen, and
   fades back in (`UI_SCREEN_SWITCH_FADE`)."

10. Don't write "simply", "just", "easy", "obviously" or "of course" in
    instructions.
11. Sentence-case headings that say something. "Choose a board", not "Board
    Selection"; never "Overview" or "Notes" alone.
12. Use bold only for UI labels on the device ("tap **Settings**"). Use
    italics, sparingly, for emphasis. No bold lead-ins on bullets.
13. Use em dashes rarely; the current rate is about one every 48 words. Replace
    each with a full stop, a colon, a comma or parentheses, and restructure the
    sentence if that doesn't work. Follow Home Assistant's rule: if a dash
    really is best, write it without spaces.
14. Keep lists for things that really are lists or steps. If the items need
    "because", write a paragraph.
15. Write one sentence per source line in RST. Home Assistant uses this rule
    because diffs then show sentence-level changes.
16. Use descriptive link text: "see
    :doc:`the partition table </reference/partitions>`", not "see here".
17. Put conditions before instructions: "If you use the knob, run ..."
18. Avoid idioms and culture-bound references; nearly a quarter of open-source
    readers are not fluent in English.
19. Give every figure alt text that says what the figure shows, in a sentence
    or two. Today's alt texts are long chains joined by dashes; shorten them
    and put the detail in the caption or body.

### Blacklist of AI-style patterns, with replacements

| Pattern                                             | Example                                                                                                                                                      | Replace with                                                                                                                                |
| --------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------- |
| Inflated significance                               | "a pivotal step that underscores the project's commitment to accuracy"                                                                                       | The fact: "the clock compares every render with NASA's frame for the same hour"                                                             |
| "-ing" tails                                        | "..., ensuring a seamless experience"                                                                                                                        | Delete it, or state the effect as its own sentence with numbers                                                                             |
| "serves as", "stands as", "functions as", "acts as" | "The gate serves as the final check"                                                                                                                         | "The gate is the final check"                                                                                                               |
| "boasts", "features", "offers" meaning "has"        | "The P4 boasts 32 MB of flash"                                                                                                                               | "The P4 has 32 MB of flash"                                                                                                                 |
| Negative parallelism                                | "It's not just a clock, it's a planetarium"                                                                                                                  | Say what it is: "It's a clock that draws the Moon as it looks tonight"                                                                      |
| "Not X but Y" corrections nobody asked for          | "This isn't a hack but a deliberate choice"                                                                                                                  | "We chose this because ..."                                                                                                                 |
| Reflexive rule of three                             | "fast, reliable, and elegant"                                                                                                                                | Keep only the adjectives you can back with a fact, often none                                                                               |
| AI vocabulary                                       | additionally, crucial, pivotal, delve, foster, enhance, showcase, underscore, leverage, robust, seamless, vibrant, intricate, landscape, tapestry, testament | also, important (or cut), look at, help, improve, show, stress, use, reliable (with evidence), smooth, (cut), detailed, field, (cut), proof |
| Vague attribution                                   | "Experts agree that..."                                                                                                                                      | Cite the source, or drop the claim                                                                                                          |
| Summary endings                                     | "In summary, ..." / "Overall, ..."                                                                                                                           | End when the content ends; use a "Next steps" list                                                                                          |
| "Challenges and future" sections                    | "Despite these challenges, the project continues to evolve"                                                                                                  | A dated roadmap issue, or nothing                                                                                                           |
| Elegant variation                                   | bake / render / compute / generate for one step                                                                                                              | One term, defined in the glossary                                                                                                           |
| Placeholder politeness                              | "Please note that...", "It is worth noting that..."                                                                                                          | Delete; use a `note` admonition if it matters                                                                                               |
| Chat leftovers                                      | "Here is...", "I hope this helps", "Let me know"                                                                                                             | Delete                                                                                                                                      |
| Title Case Headings                                 | "Building The Firmware"                                                                                                                                      | "Build the firmware"                                                                                                                        |
| Emoji in headings or bullets                        | "🌙 How it works"                                                                                                                                            | "How it works"                                                                                                                              |
| Bold everywhere                                     | "**ALL** frames are **atomic**"                                                                                                                              | "Every frame reaches the panel in one piece"                                                                                                |
| Inline-header bullets                               | "- **Speed:** the bake takes..."                                                                                                                             | A table, or a plain sentence                                                                                                                |

The count in the current docs for some of these is already low ("serves as"
once, "crucial", "robust" and "ensures" zero in the hand-written RST). The real
targets are the dashes, "simply", "not just" (7) and the history narration.

### Landing page recipe

Based on Adafruit, Raspberry Pi and NN/g, in this order:

1. A title that says what the thing is ("A desk clock that shows tonight's
   Moon"), and a photo or render of the clock on a desk.
2. Two or three sentences: what it does, what makes it different (the moon is
   computed and checked against NASA images), and which boards it runs on.
3. Three doors, as cards or a short list, each named by the reader's goal:
   - "Try it on your computer": the simulator tutorial
   - "Build one": parts list, then the build tutorial
   - "Understand how it works": the conceptual overview, then the science
4. A one-line pointer for contributors: "Working on the firmware? Start with
   the architecture overview."
5. Nothing else above the fold. No feature lists, badges, history or changelog
   excerpts.

The README follows the same pattern in miniature: what it is, the hero picture,
the fastest way to see it working, and a link to the docs site. Make a README
and Google's README guidance both put "what it is and how to use it" first,
with status and links after ([makeareadme](https://www.makeareadme.com/),
[Google READMEs](https://google.github.io/styleguide/docguide/READMEs.html)).

### Rules for numbers, dates and history

1. Current state first, always. A page describes the firmware as it is at the
   commit the docs were built from. No "now", "new", "currently", "no longer",
   "used to", "was retired" or "since <date>" in tutorials, how-tos, reference
   or explanation.
2. History goes in one of two places:
   - a changelog page (Keep a Changelog style: newest first, grouped as Added,
     Changed, Removed, Fixed, with commit or release anchors), written for
     people, never pasted from `git log`;
   - a decisions page for choices a reader might question (for example the
     burn-in trade-off, or removing the spin), one entry per decision: the
     context, the decision, the consequences, and the date or commit. (This is
     the common "architecture decision record" pattern; I did not research it
     for this report.)
3. In a current-state page, history may appear only as a single
   `versionchanged` or `deprecated` line, or a "Why it works this way" link to
   the decisions page, and only when a reader of the current version needs it
   (for example, an old build directory that has to be deleted).
4. Glossary entries define the current term. Removed terms either go or move to
   the changelog. A glossary line that starts with "the retired ..." is a bug.
5. Numbers a reader acts on (sizes, timings, pins, partition offsets) come from
   generated fragments or are covered by the drift gate. Prose refers to the
   table ("see the partition table") instead of retyping the number. When prose
   must contain a number, give the unit and, for sizes, both the exact and the
   rounded form once ("2,097,152 bytes (2 MiB)").
6. Dates use ISO 8601 (2026-11-27), only in changelog, decisions and research
   pages. Google also asks for unambiguous date formats.
7. User decisions are recorded once, in the decisions page. Other pages state
   the resulting behaviour.

### A beginner-to-advanced navigation structure

In general terms, with toctree captions as the sidebar headings:

1. **Start here.** The landing page, a one-screen "What this clock is" overview
   (the conceptual overview Wayne recommends), and the two tutorials: run the
   simulator, then build and flash one board.
2. **Build your own.** Parts and boards (what to buy, with photos), printing
   and assembling the housing, first-time Wi-Fi setup. Tutorial or how-to per
   page, aimed at someone with no embedded background.
3. **Using the clock.** What each screen shows and how to get to it, settings,
   alarms, sleep. User-facing reference, written for someone holding the
   device.
4. **How-to guides.** Short task pages for people who already have a working
   setup: identify boards, flash without losing settings, change a setting
   default, regenerate the docs media, run the image gate, add a screen.
5. **Troubleshooting.** Symptom-first pages: black screen, reboot loop, no
   Wi-Fi, wrong time, brown-out on the knob.
6. **How it works: the science.** Explanation, ordered concrete to abstract:
   why the Moon has phases (lamp and ball), what libration is, how the clock
   draws the Moon, how it is checked against NASA images.
7. **How it works: the firmware.** Explanation for developers: architecture,
   display pipeline and its rules (atomic frames), sleep and power, the porting
   story for each board. Each page opens with a short "what you need to know
   first" line linking back to the science or overview.
8. **Reference.** Board pages in a fixed template, Kconfig, partitions,
   constants, the screen reachability matrix, C and Python API, glossary,
   source code. All generated or drift-checked.
9. **Project.** Changelog, design decisions, contributing (including this style
   guide), research notes like this one.

Sections 1 to 3 should never require sections 6 to 8 to make sense. Sections 6
to 8 may assume 1 to 3. Each page ends with two or three "Next" links that go
one layer deeper or sideways, never a long "See also" dump.

Following Procida, don't build this all at once. Rewrite the landing page and
the simulator tutorial first and publish them. Then work page by page, and let
the sidebar change when enough pages have moved.

## Contrarian Views And Risks

**Diátaxis can be applied too strictly.** Wayne compares it to the
five-paragraph essay: a good starting point that becomes a straitjacket. He
also points out that Django's explanation section is about triple the size of
its tutorial and how-to sections combined, and that Python's tutorial has to
explain as it goes
([Wayne](https://www.hillelwayne.com/post/problems-with-the-4doc-model/)). The
moon pipeline is closer to a framework than a tool: the science, the bake and
the display rules depend on each other. Forcing every page into one of four
boxes could split a coherent explanation across pages that each make no sense
alone. Use the types as a check on each page's purpose, and allow an
explanation page to show code when the code is the clearest way to explain.

**More pages can mean more fragmentation.** One practitioner reports that
Diátaxis leads to many smaller pages and finds that search and labels make up
for it ([dadand.dev](https://dadand.dev/article/2023/diataxis-thoughts/)). That
assumes good search and good labels. A Sphinx site with a deep sidebar can bury
pages. Keep the sidebar shallow and make each section's index page a real
landing page with one line per child.

**Oversimplification can mislead.** A school-level explanation that drops too
much can install the very misconception it should prevent, such as treating
phases as a shadow
([LPI](https://www.lpi.usra.edu/education/pre_service_edu/PhasesMisconceptions.shtml)).
A simple version has to be true, even if incomplete, and should say what it
leaves out ("This ignores the Moon's tilted orbit; see Libration").

**Plain language takes effort and can lose precision.** Firmware rules like
"one atomic whole-panel flush under the mutex" are precise for a reason.
Rewriting them for a newcomer can blur the invariant. Keep the precise form in
reference and developer explanation, and add the plain version alongside it
instead of replacing it.

**Readability metrics can be gamed.** Redish and Selzer showed that writing to
a formula makes text worse in ways the formula can't see. Don't add a Flesch
gate to CI. If anything is automated, flag specific patterns (the blacklist,
sentence length over about 40 words, "simply") for review instead of scoring
pages.

**AI-sign lists are unreliable as detectors, and keep changing.** Wikipedia
says so itself, and the em-dash entry is already marked historical. A writer
who mechanically removes every listed word can still produce flat, generic
prose. The positive rule, "say the specific thing", matters more than the
blacklist.

**Moving history out has a cost.** Maintainers use dated notes to remember why
something is the way it is. If the decisions page isn't kept up, that knowledge
disappears. The decisions page needs to be part of the same docs-with-code rule
as everything else.

**Maintenance cost grows with tutorials.** Diátaxis notes that tutorials need
more revision than any other type, because one change can cascade through the
whole learning path ([tutorials](https://diataxis.fr/tutorials/)). With three
boards and an active firmware, keep exactly one hardware tutorial (the S3) and
one software tutorial (the simulator), and handle the other boards as how-tos.

**AI may change how docs are read.** Tom Johnson suggests chat interfaces may
make information architecture less important
([I'd Rather Be Writing](https://idratherbewriting.com/blog/what-is-diataxis-documentation-framework)).
If true, clean single-purpose pages with clear headings still help, because
they are also easier for retrieval tools to quote correctly. This is
speculative.

## Open Questions

1. Who actually reads these docs today? No analytics or reader interviews were
   part of this research. Sequin's biggest gains came from watching users read;
   a few sessions with a newcomer and a firmware developer would test the
   proposed structure better than any guideline.
2. Should the docs be versioned (per release) or always describe `main`? Write
   the Docs notes that users on older versions need older docs. The history
   rules above assume "always `main`".
3. How much maths belongs in the school-level science pages? Concreteness
   fading suggests a path, but the right stopping point for this audience is a
   judgement call that should be tested on a real reader.
4. Should the source-code pages stay in the main sidebar? They are reference
   for developers but take a lot of space for everyone else.
5. Can parts of the blacklist and the history rules be checked by the existing
   drift gate (for example, flag "retired" or ISO dates in current-state
   pages)? It's probably worth doing, but the false-positive rate is unknown.
6. The 14-words/90% comprehension figure that GOV.UK cites could not be traced
   to its original study.

## Sources

Framework and page types

- https://diataxis.fr/ : Diátaxis home; four types, adoption quotes.
- https://diataxis.fr/compass/ : the two-question compass table.
- https://diataxis.fr/how-to-use-diataxis/ : incremental workflow; "don't
  create empty structures".
- https://diataxis.fr/tutorials/ : tutorial principles, language of tutorials,
  maintenance burden.
- https://diataxis.fr/how-to-guides/ : how-to principles; user perspective, not
  machinery.
- https://diataxis.fr/reference/ : reference as austere description that
  mirrors the product.
- https://diataxis.fr/explanation/ : explanation as bounded, opinionated "why".
- https://diataxis.fr/start-here/ : primer (skimmed).
- https://diataxis.fr/tutorials-how-to/ : tutorial vs how-to distinction
  (skimmed).
- https://ubuntu.com/blog/diataxis-a-new-foundation-for-canonical-documentation
  : Canonical's adoption.
- https://simonwillison.net/2021/Aug/21/diataxis/ : note on the Divio model
  getting its Diátaxis name.
- https://idratherbewriting.com/blog/what-is-diataxis-documentation-framework :
  Tom Johnson; DITA comparison, Procida's reply, AI-chat speculation.
- https://www.hillelwayne.com/post/problems-with-the-4doc-model/ : critique;
  conceptual overviews and examples; Divio origin.
- https://d-romero.com/blog/what-diataxis-is-missing/ : critique; audience,
  register, documentation brief.
- https://dadand.dev/article/2023/diataxis-thoughts/ : practitioner report;
  more, smaller pages.
- https://blog.sequinstream.com/we-fixed-our-documentation-with-the-diataxis-framework/
  : case study; quickstart first, observed users.
- https://www.thegooddocsproject.dev/template : Good Docs templates incl.
  troubleshooting, release notes.
- https://docs.oasis-open.org/dita/dita/v1.3/os/part2-tech-content/archSpec/technicalContent/dita-technicalContent-InformationTypes.html
  : DITA 1.3 topic types.
- https://en.wikipedia.org/wiki/Minimalism_(technical_communication) :
  Carroll's minimalism, task orientation (article flagged by Wikipedia for
  quality issues).

Reading behaviour and layering

- https://www.nngroup.com/articles/how-users-read-on-the-web/ : 79% scan; 124%
  usability for concise, scannable, objective text.
- https://www.nngroup.com/articles/inverted-pyramid/ : most important
  information first.
- https://www.nngroup.com/articles/progressive-disclosure/ : layering and its
  two design criteria.
- https://www.nngroup.com/articles/information-scent/ : how link labels guide
  navigation.
- https://www.nngroup.com/articles/plain-language-experts/ : experts prefer
  plain language.

Plain language and style guides

- https://www.plainlanguage.gov/guidelines/ : now points to digital.gov guides;
  archive on GitHub.
- https://digital.gov/guides/plain-language/writing : active voice, present
  tense, hidden verbs.
- https://digital.gov/guides/plain-language/writing/clear-short : sentence and
  paragraph length.
- https://publications.europa.eu/resource/cellar/725b7eb0-d92e-11e5-8fea-01aa75ed71a1.0004.03/DOC_1
  : EU "How to write clearly"; 20-word average, no elegant variation,
  informative headings.
- https://insidegovuk.blog.gov.uk/2014/08/04/sentence-length-why-25-words-is-our-limit/
  : GOV.UK 25-word limit; secondhand comprehension figures.
- https://developers.google.com/style/highlights : Google style highlights.
- https://developers.google.com/style/tone : voice and tone; words and habits
  to avoid.
- https://developers.google.com/style/voice : active voice and its exceptions.
- https://developers.google.com/style/translation : writing for a global
  audience.
- https://developers.google.com/style/timeless-documentation : avoid "new",
  "now", "currently".
- https://google.github.io/styleguide/docguide/best_practices.html : minimum
  viable docs, docs with code, delete dead docs.
- https://google.github.io/styleguide/docguide/READMEs.html : what a README
  must contain.
- https://learn.microsoft.com/en-us/style-guide/top-10-tips-style-voice :
  Microsoft top 10 tips.
- https://learn.microsoft.com/en-us/style-guide/global-communications/writing-tips
  : global English rules.
- https://contributors.home-assistant.io/writing/reference/style/ : HA style;
  em dashes, headings, emphasis, one sentence per line.
- https://www.writethedocs.org/guide/writing/docs-principles/ : ARID,
  skimmable, current, nearby.
- https://redish.net/wp-content/uploads/Redish_Selzer_1985.pdf : Redish and
  Selzer on readability formulas.
- https://en.wikipedia.org/wiki/Readability : history and limits of readability
  formulas.
- https://en.wikipedia.org/wiki/Flesch%E2%80%93Kincaid_readability_tests : what
  Flesch-Kincaid measures.

AI-style patterns

- https://en.wikipedia.org/wiki/Wikipedia:Signs_of_AI_writing : the catalogue
  of patterns, caveats, signs of human writing.

Hardware and maker exemplars

- https://cdn-learn.adafruit.com/downloads/pdf/creating-great-guides-for-the-adafruit-learning-system.pdf
  : Adafruit authoring guide.
- https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/index.html
  : ESP-IDF Get Started structure.
- https://docs.zephyrproject.org/latest/contribute/documentation/guidelines.html
  : Zephyr doc guidelines; board directive, Kconfig role.
- https://www.raspberrypi.com/documentation/ : Raspberry Pi documentation home.
- https://help.prusa3d.com/ : Prusa knowledge base home (light look only).
- https://www.makeareadme.com/ : README sections.

Sphinx and furo

- https://www.sphinx-doc.org/en/master/usage/restructuredtext/directives.html :
  toctree options, glossary, seealso, versionchanged.
- https://pradyunsg.me/furo/customisation/sidebar/ : furo sidebar
  (customisation marked unstable).
- https://pradyunsg.me/furo/reference/admonitions/ : furo admonitions.

History and maintenance

- https://keepachangelog.com/en/1.1.0/ : changelog principles and change types.
- https://opensourcesurvey.org/2017/ : GitHub Open Source Survey; 93% met bad
  docs; about a quarter not fluent in English.

Science teaching

- https://link.springer.com/article/10.1007/s10648-014-9249-3 : Fyfe et al.
  2014, concreteness fading review.
- https://www.lpi.usra.edu/education/pre_service_edu/PhasesMisconceptions.shtml
  : LPI list of moon-phase misconceptions and research.
- https://www.sciencelearn.org.nz/resources/2466-the-moon-and-its-misconceptions
  : Moon misconceptions resource (skimmed).

Project measurements

- Counts in this report (em dashes, dates, "retired", "simply") come from the
  68 hand-written RST files under `docs/`, excluding `_build/` and the
  generated `code/gen/` pages, at the commit this report was written on.

## Rerun Inputs

```
workflow: firecrawl-deep-research
topic: Best practices for technical documentation, applied to a full rewrite
       of the Moon Phase Clock Sphinx (furo) site for readers from school
       level to firmware developers: Diátaxis and critiques, progressive
       disclosure, plain language and readability, avoiding AI-sounding
       prose, hardware/maker doc exemplars, Sphinx/furo organisation,
       history vs current-state docs, contrarian views.
depth: exhaustive
output: markdown (docs/research/documentation-practices.md)
```
