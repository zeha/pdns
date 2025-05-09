MAIN_MANS = pdns_server.1 \
	pdns_control.1 \
	pdnsutil.1 \
	zone2json.1 \
	zone2sql.1

MANPAGES_INSTALL = $(MAIN_MANS)

MANPAGES_DIST = $(MAIN_MANS) zone2ldap.1

if LDAP
MANPAGES_INSTALL += zone2ldap.1
endif

MANPAGES_TARGET_TOOLS = calidns.1 \
	dnspcap2calidns.1 \
	dnspcap2protobuf.1 \
	dnsgram.1 \
	dnsreplay.1 \
	dnsscan.1 \
	dnsscope.1 \
	dnswasher.1 \
	dumresp.1 \
	ixplore.1 \
	nproxy.1 \
	nsec3dig.1 \
	pdns_notify.1 \
	saxfr.1 \
	sdig.1

MANPAGES_TARGET_IXFRDIST = ixfrdist.1 \
	ixfrdist.yml.5

MANPAGES_DIST += $(MANPAGES_TARGET_TOOLS) \
	$(MANPAGES_TARGET_IXFRDIST) \
	dnsbulktest.1 \
	dnstcpbench.1

MANPAGES_INSTALL += dnsbulktest.1 \
	dnstcpbench.1

if TOOLS
MANPAGES_INSTALL += $(MANPAGES_TARGET_TOOLS)
endif

if IXFRDIST
MANPAGES_INSTALL += $(MANPAGES_TARGET_IXFRDIST)
endif

man_MANS = $(MANPAGES_INSTALL)

EXTRA_DIST = $(MANPAGES_DIST)

if HAVE_VENV
if !HAVE_MANPAGES
$(MANPAGES_DIST): %: mans/.complete manpages/%.rst
	mv "$(<D)/$@" "$@" && rm -f "$<"
endif # if !HAVE_MANPAGES

mans/.complete: manpages := $(addprefix manpages/,$(addsuffix .rst,$(MANPAGES_DIST)))
mans/.complete: .venv
	rm -rf "$(@D).tmp"
	(cd "${srcdir}" && $(CURDIR)/.venv/bin/python -msphinx -b man . "$(CURDIR)/$(@D).tmp" $(manpages)) && rm -rf "$(@D)" && mv "$(@D).tmp" "$(@D)"
	touch "$@"
	rm -rf "$(@D).tmp"

.venv: requirements.txt
	$(PYTHON) -m venv .venv
	.venv/bin/pip install -U pip setuptools
	.venv/bin/pip install -r ${srcdir}/requirements.txt

.NOTPARALLEL: \
	all-docs \
	upload-docs \
	html-docs \
	$(MANPAGES_DIST) \
	latex/PowerDNS-Authoritative.pdf \
	PowerDNS-Authoritative.pdf \
	html-docs.tar.bz2

clean-local:
if !HAVE_MANPAGES
# don't clean these files if they were present
# at 'configure' time (e.g. from a source dist)
	rm -f latex/PowerDNS-Authoritative.pdf $(MANPAGES_DIST) html-docs.tar.bz2
endif
	rm -rf mans

html-docs: common/** manpages/** .venv *.rst
	.venv/bin/python -msphinx -b html . html-docs

latex/PowerDNS-Authoritative.pdf: common/** manpages/** .venv *.rst
	.venv/bin/python -msphinx -M latexpdf . .

PowerDNS-Authoritative.pdf: latex/PowerDNS-Authoritative.pdf
	mv $< $@

html-docs.tar.bz2: html-docs
	tar cjf $@ $<

all-docs: PowerDNS-Authoritative.pdf html-docs html-docs.tar.bz2

upload-docs: all-docs
	rsync -crv --delete --no-p --chmod=g=rwX --exclude '*~' ./html-docs/ web1.powerdns.com:/srv/www/doc.powerdns.com/authoritative/
	rsync -crv --no-p --chmod=g=rwX --exclude '*~' ./html-docs.tar.bz2 web1.powerdns.com:/srv/www/doc.powerdns.com/authoritative/
	rsync -crv --no-p --chmod=g=rwX --exclude '*~' ./PowerDNS-Authoritative.pdf web1.powerdns.com:/srv/www/doc.powerdns.com/authoritative/

else # if HAVE_VENV
$(MANPAGES_DIST):
	@echo "You need Python 3 and the 'venv' module to generate the manpages"
	exit 1

PowerDNS-Authoritative.pdf:
	@echo "You need Python 3 and the 'venv' module to generate the PDF"
	exit 1

html-docs:
	@echo "You need Python 3 and the 'venv' module to generate the HTML docs"
	exit 1
endif
