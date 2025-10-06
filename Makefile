all:
	@mkdir -p build
	@gcc -g -Wall -Wextra -o build/acl src/acl.c -Wno-unused-function

clean:
	@rm -rf build