all:
	@mkdir -p build
	@gcc -g -Wall -Wextra -o build/acl src/acl.c

clean:
	@rm -rf build