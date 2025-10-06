all:
	@mkdir -p build
	@gcc -std=c11 -O2 -Wall -Wextra -fPIC -shared -o build/libacl.so src/acl.c
	@gcc -o build/acl src/test.c -Lbuild -lacl

clean:
	@rm -rf build