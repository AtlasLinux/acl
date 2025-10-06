all:
	@mkdir -p build
	@gcc -Wall -Wextra -o build/acl src/acl.c -Wno-unused-function -std=c11

clean:
	@rm -rf build