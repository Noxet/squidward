
SQAPPS = coaps

all:

builds:
	mkdir -p binaries
	for app in $(SQAPPS) ; do \
		$(MAKE) -C src/$$app builds ; \
		cp -r src/$$app/binaries/* binaries/ ; \
	done

cleanbuilds:
	rm -rf binaries
