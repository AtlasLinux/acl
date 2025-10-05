all:
	@mkdir -p build
	@gcc -g -Wall -Wextra -o build/acl src/acl.c

run: all
	@clear
	@./build/acl test/01

clean:
	@rm -rf build