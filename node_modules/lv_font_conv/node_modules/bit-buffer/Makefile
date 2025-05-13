.PHONY: lint test

all: lint test

lint:
	node node_modules/.bin/jshint bit-buffer.js

test:
	node node_modules/.bin/mocha --ui tdd
